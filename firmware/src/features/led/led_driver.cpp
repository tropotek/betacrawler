#include <Arduino.h>
#include "features/led/led_driver.h"
#include "core/led_curve.h"
#include "config.h"

#ifndef LED_PIN
#error "FEATURE_LED is on but the board header defines no LED_PIN"
#endif

namespace led {

// Software PWM carrier for fade mode: 2000us (500Hz) -- well above the
// flicker-fusion threshold and far faster than even the fastest breathing
// cycle (50ms at hz=20, i.e. 25 carrier periods per breath). PC13 has no
// timer channel, so hardware PWM is not an option here.
static const uint32_t kFadeCarrierUs = 2000;

void LedDriver::write(bool on) {
#if LED_ACTIVE_LOW
  digitalWrite(LED_PIN, on ? LOW : HIGH);
#else
  digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
}

void LedDriver::begin() {
  pinMode(LED_PIN, OUTPUT);
  write(false);
}

void LedDriver::apply(int32_t modeIdx, int32_t rateHz) {
  mode_ = modeIdx;
  hz_ = rateHz < 1 ? 1 : rateHz;
  if (mode_ == MODE_OFF)     { on_ = false; write(false); }
  else if (mode_ == MODE_ON) { on_ = true;  write(true); }
}

void LedDriver::onParamChanged(uint8_t local, const core::Params& p) {
  // Either parameter changing re-applies both -- mode and rate are meaningless
  // apart. globalParam() maps this module's own indices onto wherever the
  // registry placed them, so enabling another module never breaks this.
  (void)local;
  apply(p.num(globalParam(P_MODE)), p.num(globalParam(P_RATE)));
}

void LedDriver::tick(uint32_t nowMs) {
  if (mode_ == MODE_BLINK) {
    uint32_t halfPeriod = 500u / (uint32_t)hz_;   // hz_ full cycles per second
    if (halfPeriod == 0) halfPeriod = 1;
    if (nowMs - lastToggle_ >= halfPeriod) {
      lastToggle_ = nowMs;
      on_ = !on_;
      write(on_);
    }
  } else if (mode_ >= MODE_FADE) {
    uint32_t periodMs = 1000u / (uint32_t)hz_;    // full breaths per second, same convention as blink
    if (periodMs == 0) periodMs = 1;
    uint32_t phaseMs = nowMs % periodMs;
    uint8_t duty = core::breathingDuty(phaseMs, periodMs);
    uint32_t carrierUs = micros() % kFadeCarrierUs;
    bool shouldBeOn = carrierUs < (kFadeCarrierUs * (uint32_t)duty) / 100u;
    if (shouldBeOn != on_) {
      on_ = shouldBeOn;
      write(on_);
    }
  }
}

}  // namespace led
