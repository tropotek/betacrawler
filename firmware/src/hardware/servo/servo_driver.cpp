#include <Arduino.h>
#include <HardwareTimer.h>
#include <new>

// stm32f4xx_hal_gpio.h (pulled in above via Arduino.h) #defines MODE_INPUT as
// a raw GPIO_MODER bit pattern; that's a plain preprocessor token, so it
// collides textually with servo's own MODE_INPUT enumerator declared in
// servo_params.h below -- namespacing the enum doesn't protect it from a
// macro. This file never calls the HAL macro directly (attachOutput/detach
// go through Arduino's pinMode/timer_->setMode), so undefining it here is
// safe and confined to this one translation unit.
#undef MODE_INPUT

#include "hardware/servo/servo_driver.h"
#include "core/registry.h"
#include "config.h"

#ifndef SERVO_PIN
#error "FEATURE_SERVO is on but the board header defines no SERVO_PIN"
#endif
#ifndef SERVO_TIMER
#error "FEATURE_SERVO is on but the board header defines no SERVO_TIMER"
#endif

// 50Hz frame. Overridable from a board header for a digital servo that wants
// a faster one; analogue servos expect 20ms.
#ifndef SERVO_FRAME_US
#define SERVO_FRAME_US 20000
#endif

namespace servo {

// Storage for the one HardwareTimer, placement-new'd in begin().
//
// NOT `new`: this firmware allocates nothing on the heap (see config.h), and
// this is the module the ESC and receiver modules will be copied from, so the
// precedent would cost more than the allocation.
//
// NOT a file-scope `static HardwareTimer` either -- the display driver's
// statics are safe because they "only record pins", whereas HardwareTimer's
// constructor enables the timer clock and calls into the HAL. At static-init
// time that would run before HAL_Init() and the clock configuration.
alignas(HardwareTimer) static uint8_t s_timerMem[sizeof(HardwareTimer)];

void ServoDriver::begin() {
  timer_ = new (s_timerMem) HardwareTimer(SERVO_TIMER);
  // The timer instance is named by the board header (explicit and greppable,
  // which matters when the next modules also want timers); only the channel
  // is derived from the pin, being a pure lookup with nothing to construct.
  ch_ = STM_PIN_CHANNEL(pinmap_function(digitalPinToPinName(SERVO_PIN), PinMap_PWM));
  timer_->setOverflow(SERVO_FRAME_US, MICROSEC_FORMAT);
  timer_->resume();
  detach();   // boot silent; main.cpp's notify pass applies any saved mode next
}

void ServoDriver::attachOutput() {
  // setMode reclaims the pin for the timer's alternate function, which
  // detach() gave back to the GPIO peripheral.
  timer_->setMode(ch_, TIMER_OUTPUT_COMPARE_PWM1, SERVO_PIN);
  timer_->resumeChannel(ch_);
}

void ServoDriver::detach() {
  // A real detach, not a zero-width pulse: the servo relaxes and stops drawing
  // holding current, which matters when the whole board runs off USB.
  timer_->pauseChannel(ch_);
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);
  lastUs_ = 0;
}

void ServoDriver::writeUs(uint16_t us) {
  timer_->setCaptureCompare(ch_, us, MICROSEC_COMPARE_FORMAT);
  lastUs_ = us;
}

void ServoDriver::attach(const core::Registry& reg, const core::Params& p) {
  (void)p;
  inputs_ = &reg.inputs();
}

void ServoDriver::apply(const core::Params& p) {
  const int32_t  prevMode   = mode_;
  const uint32_t prevPeriod = periodMs_;
  mode_     = p.num(globalParam(P_MODE));
  angle_    = (uint8_t)p.num(globalParam(P_ANGLE));
  minUs_    = (uint16_t)p.num(globalParam(P_MIN_US));
  maxUs_    = (uint16_t)p.num(globalParam(P_MAX_US));
  periodMs_ = (uint32_t)p.num(globalParam(P_SWEEP_S)) * 1000u;
  srcIdx_   = (uint8_t)p.num(globalParam(P_SRC));

  if (mode_ == MODE_OFF) { detach(); return; }
  if (prevMode == MODE_OFF) attachOutput();

  if (mode_ == MODE_SWEEP) {
    const uint32_t now = millis();
    if (prevMode != MODE_SWEEP) {
      t0_ = now;                       // entering sweep: start from a known end
    } else if (periodMs_ != prevPeriod) {
      // Changing speed mid-sweep. Leaving t0_ alone is NOT enough: position is
      // (elapsed % period), so a new modulus lands somewhere unrelated. Found
      // on hardware -- sweep_s 4 -> 10 moved the pulse 1916 -> 1050us in one
      // step. Re-anchor so the same fraction through the cycle is preserved.
      t0_ = now - rephase(now - t0_, prevPeriod, periodMs_);
    }
    return;   // tick() owns the compare register from here
  }
  if (mode_ == MODE_INPUT) {
    return;   // tick() owns the compare register from here, same as sweep
  }
  writeUs(angleToUs(angle_, minUs_, maxUs_));   // MODE_HOLD
}

void ServoDriver::onParamChanged(uint8_t local, const core::Params& p) {
  // Every parameter feeds the same recompute -- mode, angle and calibration
  // are meaningless apart. globalParam() maps this module's own indices onto
  // wherever the registry placed them, so enabling another module never
  // breaks this.
  (void)local;
  apply(p);
}

void ServoDriver::tick(uint32_t nowMs) {
  if (mode_ == MODE_SWEEP) {
    // Phase from elapsed time, never accumulated per tick, so loop jitter
    // cannot drift the sweep rate.
    writeUs(angleToUs(sweepAngle(nowMs - t0_, periodMs_), minUs_, maxUs_));
  } else if (mode_ == MODE_INPUT) {
    const int16_t v = inputs_->get(srcIdx_);
    // 0 is rx's established "this slot carries no data" sentinel -- never
    // written, a channel this protocol does not transmit, or invalidated by a
    // protocol/source switch -- and is not a reachable ticksToUs() output
    // (880..2159). Hold the last pulse rather than actuating to min_us.
    if (v > 0) writeUs(clampUs(v, minUs_, maxUs_));
  }
}

void ServoDriver::readTelemetry(core::TlmValue* out) {
  out[T_US].u = lastUs_;
}

}  // namespace servo
