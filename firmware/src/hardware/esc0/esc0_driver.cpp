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
#ifndef ESC0_PIN_B
#error "FEATURE_ESC0 is on but the board header defines no ESC0_PIN_B"
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

// Storage for the shared HardwareTimer and every OutputStage this instance
// might switch between at runtime. NOT `new`: this firmware allocates
// nothing on the heap. NOT file-scope `static` objects either -- their
// constructors touch the HAL, which is not up yet at static-init time. Each
// instance's storage lives in its own translation unit, so esc0 and esc1
// never share it.
alignas(HardwareTimer) static uint8_t s_timerMem[sizeof(HardwareTimer)];
alignas(esc::EscOutput) static uint8_t s_escOutMem[sizeof(esc::EscOutput)];
alignas(esc::HbridgeOutput) static uint8_t s_hbridgeOutMem[sizeof(esc::HbridgeOutput)];

void EscDriver::begin() {
  timer_ = new (s_timerMem) HardwareTimer(ESC0_TIMER);
  periodUs_ = ESC0_FRAME_US;
  timer_->setOverflow(periodUs_, MICROSEC_FORMAT);
  timer_->resume();
  escOut_ = new (s_escOutMem) esc::EscOutput(timer_, ESC0_PIN);
  hbridgeOut_ = new (s_hbridgeOutMem) esc::HbridgeOutput(timer_, ESC0_PIN, ESC0_PIN_B);
  escOut_->begin();
  hbridgeOut_->begin();
  stage_ = escOut_;   // type_ defaults to TYPE_ESC
  stage_->detach();   // boot silent; main.cpp's notify pass applies any saved mode next
}

void EscDriver::attach(const core::Registry& reg, const core::Params& p) {
  (void)p;
  inputs_      = &reg.inputs();
  driveInputs_ = &reg.driveOutputs();
}

void EscDriver::apply(const core::Params& p) {
  const int32_t prevMode = mode_;
  const uint8_t prevSrcIdx = srcIdx_;
  const uint8_t prevRateIdx = rateIdx_;
  const int32_t prevType = type_;
  type_       = p.num(globalParam(P_TYPE));
  mode_       = p.num(globalParam(P_MODE));
  throttleUs_ = (uint16_t)p.num(globalParam(P_THROTTLE_US));
  minUs_      = (uint16_t)p.num(globalParam(P_MIN_US));
  maxUs_      = (uint16_t)p.num(globalParam(P_MAX_US));
  direction_  = p.num(globalParam(P_DIRECTION));
  srcIdx_     = (uint8_t)p.num(globalParam(P_SRC));
  rateIdx_    = (uint8_t)p.num(globalParam(P_RATE));
  freqHz_     = (uint32_t)p.num(globalParam(P_FREQ));
  invert_     = p.num(globalParam(P_INVERT)) != 0;
  brake_      = p.num(globalParam(P_BRAKE)) != 0;

  const bool typeChanged = (type_ != prevType);
  if (typeChanged) {
    esc::OutputStage* nextStage = (type_ == esc::TYPE_HBRIDGE)
        ? static_cast<esc::OutputStage*>(hbridgeOut_)
        : static_cast<esc::OutputStage*>(escOut_);
    // Switching electronics type while live: detach the old stage's pins
    // before the new one drives anything -- never both stages driving at
    // once. Safe to call unconditionally even from MODE_OFF (detach() on an
    // already-detached stage is a no-op in substance).
    stage_->detach();
    stage_ = nextStage;
  }
  stage_->setInverted(invert_);
  stage_->setBrakeOnZero(brake_);

  periodUs_ = (type_ == esc::TYPE_HBRIDGE)
      ? (1000000u / (freqHz_ ? freqHz_ : 1u))
      : esc::frameUsForRate(rateIdx_);
  stage_->setPeriodUs(periodUs_);

  const bool enteringFromOff = (prevMode == esc::MODE_OFF && mode_ != esc::MODE_OFF);
  const bool srcChanged = (srcIdx_ != prevSrcIdx);
  const bool rateChanged = (rateIdx_ != prevRateIdx);
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

  if (esc::inputLossDemotesArmed(armState_, mode_, inputFresh) ||
      esc::srcChangeDemotesArmed(armState_, mode_, srcChanged) ||
      esc::rateChangeDemotesArmed(armState_, rateChanged || typeChanged)) {
    armState_ = esc::ARM_ARMING;
    armT0_    = now;
  }

  if (enteringFromOff) armT0_ = now;
  const bool commandedLow = esc::isCommandedLow(mode_, throttleUs_, inputUs, inputFresh, neutral,
                                                 ESC0_ARM_LOW_MARGIN_US, bidirectional);
  if (armState_ == esc::ARM_ARMING && !commandedLow) armT0_ = now;
  armState_ = esc::nextArmState(armState_, mode_ == esc::MODE_OFF, enteringFromOff, now, armT0_,
                                 ESC0_ARM_HOLD_MS, commandedLow);

  if (mode_ == esc::MODE_OFF) { stage_->detach(); lastUs_ = 0; return; }
  if (enteringFromOff || typeChanged) stage_->attachOutput();

  uint16_t us = esc::nextPulseUs(armState_, mode_, minUs_, maxUs_, throttleUs_, inputUs,
                                  inputStale, neutral);
  // The shared ARM switch is a pure output gate, deliberately outside the
  // arm-hold state machine above: once this ESC has completed its own hold
  // it stays ARM_ARMED regardless of the switch, and the switch just forces
  // the written pulse to neutral -- instantly, no hold delay either way --
  // whenever it's inactive, no matter what armState_/mode_/the rx say.
  // driveBusFresh distinguishes "no tank_drive on this board" (never gate)
  // from "switch says not armed" (gate) -- see Registry::driveOutputs()'s
  // empty-bus fallback.
  const bool driveBusFresh = driveInputs_->lastFreshMs() != 0;
  const bool armSwitchInactive = driveBusFresh && driveInputs_->get(kDriveArmSlot) == 0;
  if (armSwitchInactive) us = neutral;
  // Last, so no route to the pin can outrun the frame. 0 means "hold the last
  // pulse" and must never be clamped up into a real command.
  const uint16_t effMax = (type_ == esc::TYPE_HBRIDGE) ? maxUs_ : esc::effectiveMaxUs(maxUs_, periodUs_);
  if (us > effMax) us = effMax;
  if (us > 0) { stage_->write(us, minUs_, maxUs_, neutral); lastUs_ = us; }
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

  if (esc::inputLossDemotesArmed(armState_, mode_, inputFresh)) {
    armState_ = esc::ARM_ARMING;
    armT0_    = nowMs;
  }

  const bool commandedLow = esc::isCommandedLow(mode_, throttleUs_, inputUs, inputFresh, neutral,
                                                 ESC0_ARM_LOW_MARGIN_US, bidirectional);
  if (armState_ == esc::ARM_ARMING && !commandedLow) armT0_ = nowMs;
  armState_ = esc::nextArmState(armState_, false, false, nowMs, armT0_, ESC0_ARM_HOLD_MS, commandedLow);

  uint16_t us = esc::nextPulseUs(armState_, mode_, minUs_, maxUs_, throttleUs_, inputUs,
                                  inputStale, neutral);
  const bool driveBusFresh = driveInputs_->lastFreshMs() != 0;
  const bool armSwitchInactive = driveBusFresh && driveInputs_->get(kDriveArmSlot) == 0;
  if (armSwitchInactive) us = neutral;
  const uint16_t effMax = (type_ == esc::TYPE_HBRIDGE) ? maxUs_ : esc::effectiveMaxUs(maxUs_, periodUs_);
  if (us > effMax) us = effMax;
  if (us > 0) { stage_->write(us, minUs_, maxUs_, neutral); lastUs_ = us; }
}

void EscDriver::readTelemetry(core::TlmValue* out) {
  out[T_US].u  = lastUs_;
  out[T_ARM].u = armState_;
}

}  // namespace esc0

#endif  // FEATURE_ESC0
