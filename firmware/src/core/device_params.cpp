#include "core/device_params.h"
#include "config.h"

namespace device {

using core::ParamDef;
using core::ParamType;

static const ParamDef kParams[] = {
  // key           type              label          unit  min max opts n maxlen def   defStr           group
  {"device.name",  ParamType::Str,   "Device Name", nullptr, 0, 0, nullptr, 0, core::kMaxStrLen, 0, FW_PROJECT_NAME, nullptr},
  // Owned here (it configures the link, not any one peripheral) but grouped
  // under "Telemetry" in the UI, which is where a user looks for it.
  {"tlm.rate",     ParamType::U8,    "Rate",        "Hz",  1, 50, nullptr, 0, 0, 10, nullptr,          "Telemetry"},
};

const core::ModuleDesc kDesc = {
  "device", "Device",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  nullptr, 0,
};

}  // namespace device
