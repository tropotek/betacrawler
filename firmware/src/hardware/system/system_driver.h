#pragma once
#include "hardware/system/system_params.h"

namespace sys {

// MCU-intrinsic telemetry: uptime, core clock, free heap, die temperature and
// supply voltage. No parameters, no pins, no board header requirements.
class SystemDriver : public core::Module {
 public:
  void readTelemetry(core::TlmValue* out) override;
};

}  // namespace sys
