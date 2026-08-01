#include "hardware/system/system_driver.h"
#include "config.h"

#if FW_MCU_ESP32

#include <Arduino.h>

namespace sys {

// Order must match kTlm in system_params.cpp. temp/vdd are stubbed to 0:
// the classic WROOM-32's internal temperature sensor is undocumented on
// original silicon (temperatureRead() is unofficial and chip-revision
// dependent) and there is no VDD reading to take here -- ESP32 runs its
// logic from a fixed onboard 3.3V regulator, not a measurable rail the way
// the STM32 boards' VREFINT trick reads.
void SystemDriver::readTelemetry(core::TlmValue* out) {
  out[T_UP].u   = millis();
  out[T_CLK].u  = (uint32_t)getCpuFrequencyMhz();
  out[T_RAM].i  = (int32_t)ESP.getFreeHeap();
  out[T_TEMP].f = 0.0f;
  out[T_VDD].i  = 0;
}

}  // namespace sys

#endif  // FW_MCU_ESP32
