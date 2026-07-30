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

#include "hardware/esc/esc_driver.h"
#include "core/registry.h"
#include "config.h"

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
  mode_       = p.num(globalParam(P_MODE));
  throttleUs_ = (uint16_t)p.num(globalParam(P_THROTTLE_US));
  minUs_      = (uint16_t)p.num(globalParam(P_MIN_US));
  maxUs_      = (uint16_t)p.num(globalParam(P_MAX_US));
  srcIdx_     = (uint8_t)p.num(globalParam(P_SRC));

  const bool enteringFromOff = (prevMode == MODE_OFF && mode_ != MODE_OFF);
  const uint32_t now = millis();
  if (enteringFromOff) armT0_ = now;
  armState_ = nextArmState(armState_, mode_ == MODE_OFF, enteringFromOff, now, armT0_,
                            ESC_ARM_HOLD_MS);

  if (mode_ == MODE_OFF) { detach(); return; }
  if (enteringFromOff) attachOutput();

  const int16_t inputUs = (mode_ == MODE_INPUT) ? inputs_->get(srcIdx_) : (int16_t)0;
  const uint16_t us = nextPulseUs(armState_, mode_, minUs_, maxUs_, throttleUs_, inputUs);
  if (us > 0) writeUs(us);
}

void EscDriver::onParamChanged(uint8_t local, const core::Params& p) {
  (void)local;
  apply(p);
}

void EscDriver::tick(uint32_t nowMs) {
  if (mode_ == MODE_OFF) return;

  armState_ = nextArmState(armState_, false, false, nowMs, armT0_, ESC_ARM_HOLD_MS);

  const int16_t inputUs = (mode_ == MODE_INPUT) ? inputs_->get(srcIdx_) : (int16_t)0;
  const uint16_t us = nextPulseUs(armState_, mode_, minUs_, maxUs_, throttleUs_, inputUs);
  if (us > 0) writeUs(us);
}

void EscDriver::readTelemetry(core::TlmValue* out) {
  out[T_US].u  = lastUs_;
  out[T_ARM].u = armState_;
}

}  // namespace esc
