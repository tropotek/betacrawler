#include "hardware/button/button_driver.h"
#include "config.h"

// The body (not just the class) must be guarded: PlatformIO compiles every
// .cpp under src/ as its own translation unit no matter what includes it, so
// an unguarded file here would still demand BUTTON_PIN on any board that
// never defines it -- same trap wifi_driver.cpp documents its own guard
// against, first hit for real by esp32_wroom32 (FEATURE_BUTTON off, no
// BUTTON_PIN: no physical button on a bare WROOM-32 dev board).
#if FEATURE_BUTTON

#include <Arduino.h>

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

#endif  // FEATURE_BUTTON
