#include "hardware/esc/esc_driver.h"
#include "core/registry.h"
#include "config.h"

// The body (not just the class) must be guarded: PlatformIO compiles every
// .cpp under src/ as its own translation unit no matter what includes it, so
// an unguarded file here would still demand ESC_PIN/ESC_TIMER -- and drag in
// the STM32-only <HardwareTimer.h> -- on any board that never defines them.
// Same trap wifi_driver.cpp documents its own guard against, first hit for
// real by esp32_wroom32 (FEATURE_ESC off, no ESC hardware on a bare
// WROOM-32 dev board, and no ESP32 timer-PWM counterpart written for this
// module).
#if FEATURE_ESC

#include <Arduino.h>
#include <HardwareTimer.h>
#include <new>

// stm32f4xx_hal_gpio.h (pulled in above via Arduino.h) #defines MODE_INPUT as
// a raw GPIO_MODER bit pattern; that's a plain preprocessor token, so it
// collides textually with esc's own MODE_INPUT enumerator declared in
// esc_params.h below -- namespacing the enum doesn't protect it from a
// macro. This file never calls the HAL macro directly (attachOutput/detach
// go through Arduino's pinMode/timer_->setMode), so undefining it here is
// safe and confined to this one translation unit.
#undef MODE_INPUT

#ifndef ESC_PIN
#error "FEATURE_ESC is on but the board header defines no ESC_PIN"
#endif
#ifndef ESC_TIMER
#error "FEATURE_ESC is on but the board header defines no ESC_TIMER"
#endif

// 50Hz frame -- the universally-compatible default every analog-PWM ESC
// (BLHeli/BLHeli_S/BLHeli32 included) auto-detects. Overridable from a board
// header for an ESC that documents a faster refresh.
#ifndef ESC_FRAME_US
#define ESC_FRAME_US 20000
#endif

// Low-throttle hold before an armed/input command is honoured. This is a
// belt-and-suspenders gate on top of the ESC's own arming sequence, not a
// replacement for it -- see _notes/spec-esc.md, "Arming is a firmware-level
// gate".
#ifndef ESC_ARM_HOLD_MS
#define ESC_ARM_HOLD_MS 2000
#endif

// No core::Inputs::markFresh() call (i.e. no frame decoded by rx) for this
// long -> treated as a dead link and failed toward neutral (neutralUs()'s
// result -- min_us when unidirectional, center when bidirectional),
// overriding whatever the last decoded value was. Measured at the bus, not
// per-channel:
// rx stamps core::Inputs::lastFreshMs() once per accepted frame, and esc
// compares nowMs against it via isLinkFresh(). This deliberately replaced an
// earlier per-channel "value unchanged for this long" heuristic, which
// falsely read a throttle held at a mechanical endpoint (zero stick dither)
// as a dead link -- see _notes/spec-esc.md's Revision section.
#ifndef ESC_INPUT_STALE_MS
#define ESC_INPUT_STALE_MS 500
#endif

// "Low enough to arm" band around neutralUs()'s result -- the precondition
// nextArmState checks before promoting ARMING to ARMED. One-sided above
// neutral when unidirectional (neutral is the low end, so only "too high"
// is a hazard); two-sided around neutral when bidirectional (drifting
// either way off center is a hazard there). Small enough to still require a
// genuinely low/centered throttle, large enough to tolerate stick/
// calibration slop.
#ifndef ESC_ARM_LOW_MARGIN_US
#define ESC_ARM_LOW_MARGIN_US 50
#endif

namespace esc {

// Storage for the one HardwareTimer, placement-new'd in begin().
//
// NOT `new`: this firmware allocates nothing on the heap (see config.h).
// NOT a file-scope `static HardwareTimer` either -- its constructor enables
// the timer clock and calls into the HAL, which is not up yet at
// static-init time. Identical reasoning to servo's own s_timerMem.
alignas(HardwareTimer) static uint8_t s_timerMem[sizeof(HardwareTimer)];

void EscDriver::begin() {
  timer_ = new (s_timerMem) HardwareTimer(ESC_TIMER);
  ch_ = STM_PIN_CHANNEL(pinmap_function(digitalPinToPinName(ESC_PIN), PinMap_PWM));
  timer_->setOverflow(ESC_FRAME_US, MICROSEC_FORMAT);
  timer_->resume();
  detach();   // boot silent; main.cpp's notify pass applies any saved mode next
}

void EscDriver::attachOutput() {
  timer_->setMode(ch_, TIMER_OUTPUT_COMPARE_PWM1, ESC_PIN);
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
  // neutral is neutralUs()'s result -- min_us when unidirectional, center
  // when bidirectional) could still emit one stale, pre-detach pulse --
  // possibly a high throttle -- before the new neutral value's own update
  // event lands, up to one
  // ESC_FRAME_US frame later. That is exactly the hazard the arm-hold gate
  // exists to prevent. Zeroing here is safe regardless of timing: the pin is
  // already held LOW by pinMode/digitalWrite below while detached, so it does
  // not matter that this new value also only reaches the shadow register on
  // the next update event.
  timer_->setCaptureCompare(ch_, 0, MICROSEC_COMPARE_FORMAT);
  pinMode(ESC_PIN, OUTPUT);
  digitalWrite(ESC_PIN, LOW);
  lastUs_ = 0;
}

void EscDriver::writeUs(uint16_t us) {
  timer_->setCaptureCompare(ch_, us, MICROSEC_COMPARE_FORMAT);
  lastUs_ = us;
}

void EscDriver::attach(const core::Registry& reg, const core::Params& p) {
  (void)p;
  inputs_ = &reg.inputs();
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

  const bool enteringFromOff = (prevMode == MODE_OFF && mode_ != MODE_OFF);
  const bool srcChanged = (srcIdx_ != prevSrcIdx);
  const uint32_t now = millis();
  const bool bidirectional = (direction_ == DIR_BIDIRECTIONAL);
  const uint16_t neutral   = neutralUs(minUs_, maxUs_, bidirectional);

  const int16_t inputUs = (mode_ == MODE_INPUT) ? inputs_->get(srcIdx_) : (int16_t)0;
  const bool inputFresh = (mode_ == MODE_INPUT) &&
                           isLinkFresh(inputs_->lastFreshMs(), now, ESC_INPUT_STALE_MS);
  const bool inputStale = (mode_ == MODE_INPUT) && !inputFresh;

  if (inputLossDemotesArmed(armState_, mode_, inputFresh) ||
      srcChangeDemotesArmed(armState_, mode_, srcChanged)) {
    armState_ = ARM_ARMING;
    armT0_    = now;
  }

  if (enteringFromOff) armT0_ = now;
  const bool commandedLow = isCommandedLow(mode_, throttleUs_, inputUs, inputFresh, neutral,
                                            ESC_ARM_LOW_MARGIN_US, bidirectional);
  if (armState_ == ARM_ARMING && !commandedLow) armT0_ = now;
  armState_ = nextArmState(armState_, mode_ == MODE_OFF, enteringFromOff, now, armT0_,
                            ESC_ARM_HOLD_MS, commandedLow);

  if (mode_ == MODE_OFF) { detach(); return; }
  if (enteringFromOff) attachOutput();

  const uint16_t us = nextPulseUs(armState_, mode_, minUs_, maxUs_, throttleUs_, inputUs,
                                   inputStale, neutral);
  if (us > 0) writeUs(us);
}

void EscDriver::onParamChanged(uint8_t local, const core::Params& p) {
  (void)local;
  apply(p);
}

void EscDriver::tick(uint32_t nowMs) {
  if (mode_ == MODE_OFF) return;

  const bool bidirectional = (direction_ == DIR_BIDIRECTIONAL);
  const uint16_t neutral   = neutralUs(minUs_, maxUs_, bidirectional);

  const int16_t inputUs = (mode_ == MODE_INPUT) ? inputs_->get(srcIdx_) : (int16_t)0;
  const bool inputFresh = (mode_ == MODE_INPUT) &&
                           isLinkFresh(inputs_->lastFreshMs(), nowMs, ESC_INPUT_STALE_MS);
  const bool inputStale = (mode_ == MODE_INPUT) && !inputFresh;

  if (inputLossDemotesArmed(armState_, mode_, inputFresh)) {
    armState_ = ARM_ARMING;
    armT0_    = nowMs;
  }

  const bool commandedLow = isCommandedLow(mode_, throttleUs_, inputUs, inputFresh, neutral,
                                            ESC_ARM_LOW_MARGIN_US, bidirectional);
  if (armState_ == ARM_ARMING && !commandedLow) armT0_ = nowMs;
  armState_ = nextArmState(armState_, false, false, nowMs, armT0_, ESC_ARM_HOLD_MS, commandedLow);

  const uint16_t us = nextPulseUs(armState_, mode_, minUs_, maxUs_, throttleUs_, inputUs,
                                   inputStale, neutral);
  if (us > 0) writeUs(us);
}

void EscDriver::readTelemetry(core::TlmValue* out) {
  out[T_US].u  = lastUs_;
  out[T_ARM].u = armState_;
}

}  // namespace esc

#endif  // FEATURE_ESC
