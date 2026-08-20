#include "hardware/adc/adc_vref.h"
#include "config.h"

// Guards the body, not just the declaration: PlatformIO compiles every .cpp
// under src/ regardless of what includes it, and this one is STM32-only.
#if !FW_MCU_ESP32 && FW_TARGET_ARDUINO

#include <Arduino.h>

namespace adcref {

// STM32F411 factory calibration (reference manual)
#define VREFINT_CAL (*((uint16_t *)0x1FFF7A2AU))
static const int32_t CAL_VDDA_MV = 3300;

int32_t vddaMv() {
  analogReadResolution(12);
  int32_t raw = analogRead(AVREF);
  if (raw <= 0) return CAL_VDDA_MV;
  return (CAL_VDDA_MV * (int32_t)VREFINT_CAL) / raw;
}

}  // namespace adcref

#endif
