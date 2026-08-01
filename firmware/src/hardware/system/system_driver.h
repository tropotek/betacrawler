#pragma once
#include "hardware/system/system_params.h"

namespace sys {

// MCU-intrinsic telemetry: uptime, core clock, free heap, die temperature and
// supply voltage on the STM32 body (system_driver.cpp). No parameters, no
// pins, no board header requirements. The ESP32 body (system_esp32_driver.cpp)
// has no equivalent sensor path and stubs temp/vdd to 0 by design -- see that
// file's own comment, and the note in boards/esp32_wroom32.h.
class SystemDriver : public core::Module {
 public:
  void readTelemetry(core::TlmValue* out) override;
};

}  // namespace sys
