#include "hardware/esc1/esc1_driver.h"
#include "core/registry.h"
#include "config.h"

// The body (not just the class) must be guarded: PlatformIO compiles every
// .cpp under src/ as its own translation unit no matter what includes it, so
// an unguarded file here would still demand ESC1_PIN/ESC1_TIMER -- and drag
// in the STM32-only <HardwareTimer.h> -- on any board that never defines
// them. Same trap wifi_driver.cpp documents its own guard against.
#if FEATURE_ESC1

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

#ifndef ESC1_PIN
#error "FEATURE_ESC1 is on but the board header defines no ESC1_PIN"
#endif
#ifndef ESC1_TIMER
#error "FEATURE_ESC1 is on but the board header defines no ESC1_TIMER"
#endif

#ifndef ESC1_FRAME_US
#define ESC1_FRAME_US 20000
#endif

#ifndef ESC1_ARM_HOLD_MS
#define ESC1_ARM_HOLD_MS 2000
#endif

#ifndef ESC1_INPUT_STALE_MS
#define ESC1_INPUT_STALE_MS 500
#endif

#ifndef ESC1_ARM_LOW_MARGIN_US
#define ESC1_ARM_LOW_MARGIN_US 50
#endif

// "drive_left"/"drive_right" are appended after the 12 raw ch1..ch12
// options in esc1_params.cpp's kSrcNames -- index 12 is the first one. This
// is the one place esc1 knows anything about tank_drive's existence, and
// even this is just a slot-index convention, not a header dependency.
constexpr uint8_t kDriveSrcBase = 12;

// Slot 2 of driveOutputs -- the shared ARM switch (1 armed, 0 not), read
// unconditionally below regardless of what esc1.src currently selects. Same
// duplicated-literal convention as kDriveSrcBase just above; tank_drive_driver.cpp
// names this same value kArmSlot.
constexpr uint8_t kDriveArmSlot = 2;

namespace esc1 {

// Storage for the one HardwareTimer, placement-new'd in begin(). Each
// instance's storage lives in its own translation unit (this one, and
// esc0's separate copy), so the two ESCs never share it -- see esc0_driver.cpp's
// identical comment for the full reasoning (no heap, no static-init-time
// HAL calls).
alignas(HardwareTimer) static uint8_t s_timerMem[sizeof(HardwareTimer)];

void EscDriver::begin() {
  timer_ = new (s_timerMem) HardwareTimer(ESC1_TIMER);
  ch_ = STM_PIN_CHANNEL(pinmap_function(digitalPinToPinName(ESC1_PIN), PinMap_PWM));
  timer_->setOverflow(ESC1_FRAME_US, MICROSEC_FORMAT);
  timer_->resume();
  detach();   // boot silent; main.cpp's notify pass applies any saved mode next
}

void EscDriver::attachOutput() {
  timer_->setMode(ch_, TIMER_OUTPUT_COMPARE_PWM1, ESC1_PIN);
  timer_->resumeChannel(ch_);
}

void EscDriver::detach() {
  timer_->pauseChannel(ch_);
  timer_->setCaptureCompare(ch_, 0, MICROSEC_COMPARE_FORMAT);
  pinMode(ESC1_PIN, OUTPUT);
  digitalWrite(ESC1_PIN, LOW);
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
                           esc::isLinkFresh(src->lastFreshMs(), now, ESC1_INPUT_STALE_MS);
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
                                                 ESC1_ARM_LOW_MARGIN_US, bidirectional);
  if (armState_ == esc::ARM_ARMING && !commandedLow) armT0_ = now;
  armState_ = esc::nextArmState(armState_, mode_ == esc::MODE_OFF, armSwitchInactive, startHold,
                                 now, armT0_, ESC1_ARM_HOLD_MS, commandedLow);

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
                           esc::isLinkFresh(src->lastFreshMs(), nowMs, ESC1_INPUT_STALE_MS);
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
                                                 ESC1_ARM_LOW_MARGIN_US, bidirectional);
  if (armState_ == esc::ARM_ARMING && !commandedLow) armT0_ = nowMs;
  armState_ = esc::nextArmState(armState_, false, armSwitchInactive, armReactivated, nowMs, armT0_,
                                 ESC1_ARM_HOLD_MS, commandedLow);

  const uint16_t us = esc::nextPulseUs(armState_, mode_, minUs_, maxUs_, throttleUs_, inputUs,
                                        inputStale, neutral);
  if (us > 0) writeUs(us);
}

void EscDriver::readTelemetry(core::TlmValue* out) {
  out[T_US].u  = lastUs_;
  out[T_ARM].u = armState_;
}

}  // namespace esc1

#endif  // FEATURE_ESC1
