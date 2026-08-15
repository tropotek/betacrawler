#include "hardware/system/system_driver.h"
#include "config.h"
#include "core/health.h"

// STM32-side body -- see system_esp32_driver.cpp for the ESP32 counterpart,
// and wifi_driver.cpp's own comment for why each architecture-specific file
// guards its own body rather than relying on a per-environment
// build_src_filter.
#if !FW_MCU_ESP32

#include <Arduino.h>

namespace sys {

// STM32F411 factory calibration (reference manual)
#define VREFINT_CAL  (*((uint16_t *)0x1FFF7A2AU))
#define TS_CAL1      (*((uint16_t *)0x1FFF7A2CU))
#define TS_CAL2      (*((uint16_t *)0x1FFF7A2EU))
static const int32_t CAL_VDDA_MV = 3300;
static const int32_t TS_CAL1_TEMP = 30;
static const int32_t TS_CAL2_TEMP = 110;

extern "C" char *sbrk(int incr);
static int freeRamBytes() {
  char top;
  return (int)(&top - (char *)sbrk(0));
}

static int32_t readVddaMv() {
  analogReadResolution(12);
  int32_t raw = analogRead(AVREF);
  if (raw <= 0) return CAL_VDDA_MV;
  return (CAL_VDDA_MV * (int32_t)VREFINT_CAL) / raw;
}

static float readTempC(int32_t vddaMv) {
  analogReadResolution(12);
  int32_t raw = analogRead(ATEMP);
  int32_t adj = (raw * vddaMv) / CAL_VDDA_MV;
  int32_t span = (int32_t)TS_CAL2 - (int32_t)TS_CAL1;
  if (span == 0) return 0.0f;
  return (float)(adj - (int32_t)TS_CAL1) * (TS_CAL2_TEMP - TS_CAL1_TEMP) / span
         + TS_CAL1_TEMP;
}

// Order must match kTlm in system_params.cpp.
void SystemDriver::readTelemetry(core::TlmValue* out) {
  int32_t vdd = readVddaMv();
  out[T_UP].u   = millis();
  out[T_CLK].u  = SystemCoreClock / 1000000UL;
  out[T_RAM].i  = freeRamBytes();
  out[T_TEMP].f = readTempC(vdd);
  out[T_VDD].i  = vdd;
  out[T_FAULT].u = (uint32_t)core::health().fault();
}

}  // namespace sys

#endif  // !FW_MCU_ESP32
