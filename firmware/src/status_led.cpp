#include <Arduino.h>
#include "status_led.h"
#include "core/health.h"
#include "core/led_pattern.h"

#if FEATURE_STATUS_LED
#ifndef LED_PIN
#error "FEATURE_STATUS_LED is on but the board header defines no LED_PIN"
#endif
#endif

namespace status_led {

#if FEATURE_STATUS_LED

// An even 1Hz heartbeat for healthy, an even 5Hz pulse for a fault. The blink
// is what proves the loop is still running, since a frozen board latches the
// pin instead. Both steps must stay wider than the slowest loop iteration or
// a slow pass straddles one and skips it.
static const uint16_t kHealthySteps[] = {500, 500};
static const uint16_t kFaultSteps[]   = {100, 100};

static void writePin(bool on) {
#if LED_ACTIVE_LOW
  digitalWrite(LED_PIN, on ? LOW : HIGH);
#else
  digitalWrite(LED_PIN, on ? HIGH : LOW);
#endif
}

void StatusLed::begin() {
  pinMode(LED_PIN, OUTPUT);
  on_ = true;
  writePin(true);
}

void StatusLed::tick(uint32_t nowMs) {
  core::Pattern p = core::health().ok()
      ? core::Pattern{kHealthySteps, 2}
      : core::Pattern{kFaultSteps, 2};
  bool want = core::patternState(p, nowMs);
  if (want != on_) {
    on_ = want;
    writePin(want);
  }
}

#else

void StatusLed::begin() {}
void StatusLed::tick(uint32_t nowMs) { (void)nowMs; }

#endif  // FEATURE_STATUS_LED

}  // namespace status_led

#if FEATURE_STATUS_LED && !FW_MCU_ESP32

// Overrides the weak HardFault_Handler so a crash blinks instead of freezing
// with the pin latched. HardFault runs at priority -1, which masks every
// interrupt that advances millis(), so delay() would hang here forever --
// the wait below must stay a bare counting loop.
extern "C" void HardFault_Handler(void) {
  pinMode(LED_PIN, OUTPUT);
  for (;;) {
    for (uint8_t i = 0; i < 2; ++i) {
#if LED_ACTIVE_LOW
      digitalWrite(LED_PIN, i == 0 ? LOW : HIGH);
#else
      digitalWrite(LED_PIN, i == 0 ? HIGH : LOW);
#endif
      for (volatile uint32_t n = 0; n < 400000UL; ++n) { __asm__ volatile("nop"); }
    }
  }
}

#endif
