#include "hardware/esc0/esc0_driver.h"
#include "core/registry.h"
#include "config.h"

// The body (not just the class) must be guarded: PlatformIO compiles every
// .cpp under src/ as its own translation unit no matter what includes it, so
// an unguarded file here would still demand ESC0_PIN/ESC0_TIMER -- and drag
// in the STM32-only <HardwareTimer.h> -- on any board that never defines
// them. Same trap wifi_driver.cpp documents its own guard against.
#if FEATURE_ESC0

#include <Arduino.h>
#include <HardwareTimer.h>
#include <new>

// stm32f4xx_hal_gpio.h (pulled in above via Arduino.h) #defines MODE_INPUT as
// a raw GPIO_MODER bit pattern; that's a plain preprocessor token, so it
// collides textually with esc::MODE_INPUT -- namespacing the enum doesn't
// protect it from a macro. This file never calls the HAL macro directly
// (attachOutput/detach go through Arduino's pinMode/timer_->setMode), so
// undefining it here is safe and confined to this one translation unit.
#undef MODE_INPUT

#ifndef ESC0_PIN
#error "FEATURE_ESC0 is on but the board header defines no ESC0_PIN"
#endif
#ifndef ESC0_TIMER
#error "FEATURE_ESC0 is on but the board header defines no ESC0_TIMER"
#endif

// 50Hz frame -- the universally-compatible default every analog-PWM ESC
// (BLHeli/BLHeli_S/BLHeli32 included) auto-detects. Overridable from a board
// header for an ESC that documents a faster refresh.
#ifndef ESC0_FRAME_US
#define ESC0_FRAME_US 20000
#endif

// Low-throttle hold before an armed/input command is honoured. This is a
// belt-and-suspenders gate on top of the ESC's own arming sequence, not a
// replacement for it.
#ifndef ESC0_ARM_HOLD_MS
#define ESC0_ARM_HOLD_MS 2000
#endif

// No core::Inputs::markFresh() call (i.e. no frame decoded by rx) for this
// long -> treated as a dead link and failed toward neutral (esc::neutralUs()'s
// result -- min_us when unidirectional, center when bidirectional),
// overriding whatever the last decoded value was. Measured at the bus, not
// per-channel.
#ifndef ESC0_INPUT_STALE_MS
#define ESC0_INPUT_STALE_MS 500
#endif

// "Low enough to arm" band around esc::neutralUs()'s result -- the
// precondition esc::nextArmState checks before promoting ARMING to ARMED.
#ifndef ESC0_ARM_LOW_MARGIN_US
#define ESC0_ARM_LOW_MARGIN_US 50
#endif

// "drive_left"/"drive_right" are appended after the 12 raw ch1..ch12
// options in esc0_params.cpp's kSrcNames -- index 12 is the first one. This
// is the one place esc0 knows anything about tank_drive's existence, and
// even this is just a slot-index convention, not a header dependency.
constexpr uint8_t kDriveSrcBase = 12;

// Slot 2 of driveOutputs -- the shared ARM switch (1 armed, 0 not), read
// unconditionally below regardless of what esc0.src currently selects. Same
// duplicated-literal convention as kDriveSrcBase just above; tank_drive_driver.cpp
// names this same value kArmSlot.
constexpr uint8_t kDriveArmSlot = 2;

namespace esc0 {

// Storage for the one HardwareTimer, placement-new'd in begin().
//
// NOT `new`: this firmware allocates nothing on the heap (see config.h).
// NOT a file-scope `static HardwareTimer` either -- its constructor enables
// the timer clock and calls into the HAL, which is not up yet at
// static-init time. Identical reasoning to servo's own s_timerMem, and to
// esc1's own copy of this same buffer -- each instance's storage lives in
// its own translation unit, so the two ESCs never share it.
alignas(HardwareTimer) static uint8_t s_timerMem[sizeof(HardwareTimer)];

void EscDriver::begin() {
  timer_ = new (s_timerMem) HardwareTimer(ESC0_TIMER);
  ch_ = STM_PIN_CHANNEL(pinmap_function(digitalPinToPinName(ESC0_PIN), PinMap_PWM));
  timer_->setOverflow(ESC0_FRAME_US, MICROSEC_FORMAT);
  timer_->resume();
  detach();   // boot silent; main.cpp's notify pass applies any saved mode next
}

void EscDriver::attachOutput() {
  timer_->setMode(ch_, TIMER_OUTPUT_COMPARE_PWM1, ESC0_PIN);
  timer_->resumeChannel(ch_);
}

void EscDriver::detach() {
  // A real detach, not a zero-width pulse: no pulse train at all while off.
  timer_->pauseChannel(ch_);
  // Also zero the compare register itself, not just pause the channel. STM32
  // PWM channels have the OCxPE preload bit set, so setCaptureCompare() below
  // (and writeUs()'s identical call) only ever writes a *shadow* register --
  // the counter, running or not, keeps whatever value was active until an
  // update event loads the shadow into the real CCR. Left alone, that stale
  // value survives a pauseChannel()/resumeChannel() cycle untouched. Without
  // this, re-arming (attachOutput() -> writeUs(neutral) in apply(), where
  // neutral is esc::neutralUs()'s result) could still emit one stale,
  // pre-detach pulse -- possibly a high throttle -- before the new neutral
  // value's own update event lands, up to one ESC0_FRAME_US frame later.
  // That is exactly the hazard the arm-hold gate exists to prevent. Zeroing
  // here is safe regardless of timing: the pin is already held LOW by
  // pinMode/digitalWrite below while detached.
  timer_->setCaptureCompare(ch_, 0, MICROSEC_COMPARE_FORMAT);
  pinMode(ESC0_PIN, OUTPUT);
  digitalWrite(ESC0_PIN, LOW);
  lastUs_ = 0;
}

void EscDriver::writeUs(uint16_t us) {
  timer_->setCaptureCompare(ch_, us, MICROSEC_COMPARE_FORMAT);
  lastUs_ = us;
}

void EscDriver::attach(const core::Registry& reg, const core::Params& p) {
  (void)p;
  inputs_      = &reg.inputs();
  driveInputs_ = &reg.driveOutputs();
}

void EscDriver::apply(const core::Params& p) {
  const int32_t prevMode = mode_;
  const uint8_t prevSrcIdx = srcIdx_;
  mode_       = p.num(globalParam(P_MODE));
  throttleUs_ = (uint16_t)p.num(globalParam(P_THROTTLE_US));
  minUs_      = (uint16_t)p.num(globalParam(P_MIN_US));
  maxUs_      = (uint16_t)p.num(globalParam(P_MAX_US));
  direction_  = p.num(globalParam(P_DIRECTION));
  srcIdx_     = (uint8_t)p.num(globalParam(P_SRC));

  const bool enteringFromOff = (prevMode == esc::MODE_OFF && mode_ != esc::MODE_OFF);
  const bool srcChanged = (srcIdx_ != prevSrcIdx);
  const uint32_t now = millis();
  const bool bidirectional = (direction_ == esc::DIR_BIDIRECTIONAL);
  const uint16_t neutral   = esc::neutralUs(minUs_, maxUs_, bidirectional);

  const bool usesDriveBus = (srcIdx_ >= kDriveSrcBase);
  const core::Inputs* src = usesDriveBus ? driveInputs_ : inputs_;
  const uint8_t srcSlot   = usesDriveBus ? (uint8_t)(srcIdx_ - kDriveSrcBase) : srcIdx_;

  const int16_t inputUs = (mode_ == esc::MODE_INPUT) ? src->get(srcSlot) : (int16_t)0;
  const bool inputFresh = (mode_ == esc::MODE_INPUT) &&
                           esc::isLinkFresh(src->lastFreshMs(), now, ESC0_INPUT_STALE_MS);
  const bool inputStale = (mode_ == esc::MODE_INPUT) && !inputFresh;

  // Read unconditionally, regardless of usesDriveBus above: arming is a
  // safety property that must hold whether this ESC is reading a raw rx
  // channel or tank_drive's mixed output. driveBusFresh distinguishes "no
  // tank_drive on this board" (never gate) from "switch says not armed"
  // (gate) -- see Registry::driveOutputs()'s empty-bus fallback.
  const bool driveBusFresh = driveInputs_->lastFreshMs() != 0;
  const bool armSwitchInactive = driveBusFresh && driveInputs_->get(kDriveArmSlot) == 0;
  // The switch reactivating after being off must restart the arm-hold cycle
  // exactly like mode leaving MODE_OFF does. nextArmState only clears the
  // ARM_OFF floor while armSwitchInactive is true; something has to pull
  // armState_ back out of ARM_OFF once it turns false again, or an ESC
  // already in MODE_INPUT/MODE_ARMED before the switch first went active
  // has no path to ever arm at all.
  const bool armReactivated = armWasInactive_ && !armSwitchInactive;
  armWasInactive_ = armSwitchInactive;
  const bool startHold = enteringFromOff || armReactivated;

  if (esc::inputLossDemotesArmed(armState_, mode_, inputFresh) ||
      esc::srcChangeDemotesArmed(armState_, mode_, srcChanged)) {
    armState_ = esc::ARM_ARMING;
    armT0_    = now;
  }

  if (startHold) armT0_ = now;
  const bool commandedLow = esc::isCommandedLow(mode_, throttleUs_, inputUs, inputFresh, neutral,
                                                 ESC0_ARM_LOW_MARGIN_US, bidirectional);
  if (armState_ == esc::ARM_ARMING && !commandedLow) armT0_ = now;
  armState_ = esc::nextArmState(armState_, mode_ == esc::MODE_OFF, armSwitchInactive, startHold,
                                 now, armT0_, ESC0_ARM_HOLD_MS, commandedLow);

  if (mode_ == esc::MODE_OFF) { detach(); return; }
  if (enteringFromOff) attachOutput();

  const uint16_t us = esc::nextPulseUs(armState_, mode_, minUs_, maxUs_, throttleUs_, inputUs,
                                        inputStale, neutral);
  if (us > 0) writeUs(us);
}

void EscDriver::onParamChanged(uint8_t local, const core::Params& p) {
  (void)local;
  apply(p);
}

void EscDriver::tick(uint32_t nowMs) {
  if (mode_ == esc::MODE_OFF) return;

  const bool bidirectional = (direction_ == esc::DIR_BIDIRECTIONAL);
  const uint16_t neutral   = esc::neutralUs(minUs_, maxUs_, bidirectional);

  const bool usesDriveBus = (srcIdx_ >= kDriveSrcBase);
  const core::Inputs* src = usesDriveBus ? driveInputs_ : inputs_;
  const uint8_t srcSlot   = usesDriveBus ? (uint8_t)(srcIdx_ - kDriveSrcBase) : srcIdx_;

  const int16_t inputUs = (mode_ == esc::MODE_INPUT) ? src->get(srcSlot) : (int16_t)0;
  const bool inputFresh = (mode_ == esc::MODE_INPUT) &&
                           esc::isLinkFresh(src->lastFreshMs(), nowMs, ESC0_INPUT_STALE_MS);
  const bool inputStale = (mode_ == esc::MODE_INPUT) && !inputFresh;

  const bool driveBusFresh = driveInputs_->lastFreshMs() != 0;
  const bool armSwitchInactive = driveBusFresh && driveInputs_->get(kDriveArmSlot) == 0;
  const bool armReactivated = armWasInactive_ && !armSwitchInactive;
  armWasInactive_ = armSwitchInactive;

  if (esc::inputLossDemotesArmed(armState_, mode_, inputFresh)) {
    armState_ = esc::ARM_ARMING;
    armT0_    = nowMs;
  }

  if (armReactivated) armT0_ = nowMs;
  const bool commandedLow = esc::isCommandedLow(mode_, throttleUs_, inputUs, inputFresh, neutral,
                                                 ESC0_ARM_LOW_MARGIN_US, bidirectional);
  if (armState_ == esc::ARM_ARMING && !commandedLow) armT0_ = nowMs;
  armState_ = esc::nextArmState(armState_, false, armSwitchInactive, armReactivated, nowMs, armT0_,
                                 ESC0_ARM_HOLD_MS, commandedLow);

  const uint16_t us = esc::nextPulseUs(armState_, mode_, minUs_, maxUs_, throttleUs_, inputUs,
                                        inputStale, neutral);
  if (us > 0) writeUs(us);
}

void EscDriver::readTelemetry(core::TlmValue* out) {
  out[T_US].u  = lastUs_;
  out[T_ARM].u = armState_;
}

}  // namespace esc0

#endif  // FEATURE_ESC0
