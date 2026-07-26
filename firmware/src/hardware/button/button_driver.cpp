#include <Arduino.h>
#include "hardware/button/button_driver.h"
#include "config.h"

#ifndef BUTTON_PIN
#error "FEATURE_BUTTON is on but the board header defines no BUTTON_PIN"
#endif

namespace button {

void ButtonDriver::begin() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  idleLevel_ = digitalRead(BUTTON_PIN);   // assume not pressed at boot
}

void ButtonDriver::readTelemetry(core::TlmValue* out) {
  out[T_BTN].u = (digitalRead(BUTTON_PIN) != idleLevel_) ? 1u : 0u;
}

}  // namespace button
