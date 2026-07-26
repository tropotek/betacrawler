#include "hardware/system/system_params.h"

namespace sys {

using core::TlmDef;
using core::TlmType;

// MCU-intrinsic readings: every board has an uptime, a clock, a heap and the
// F4's internal temperature/VREF channels. Always registered, no FEATURE_
// toggle -- a build with no system telemetry would have nothing to show on
// the Telemetry page.
//
// `div`/`dec` are display hints only. vdd stays millivolts on the wire (as
// docs/api.md specifies) and the browser divides by 1000 to show volts, so
// the firmware never has to serialize a float it doesn't need to.
static const TlmDef kTlm[T_COUNT] = {
  // key    label       unit   type            div   dec  group
  {"up",   "Uptime",   "ms",  TlmType::U32,     0,   0,  nullptr},
  {"clk",  "Clock",    "MHz", TlmType::U32,     0,   0,  nullptr},
  {"ram",  "Free RAM", "B",   TlmType::I32,     0,   0,  nullptr},
  {"temp", "Temp",     "°C",  TlmType::F32,     0,   1,  nullptr},
  {"vdd",  "VDD",      "V",   TlmType::I32,  1000,   2,  nullptr},
};

const core::ModuleDesc kDesc = {
  "system", "System",
  nullptr, 0,
  kTlm, T_COUNT,
};

}  // namespace sys
