#include "hardware/system/system_params.h"

namespace sys {

using core::TlmDef;
using core::TlmType;

// MCU-intrinsic readings: every board has an uptime, a clock, a heap and the
// F4's internal temperature/VREF channels. Always registered, no FEATURE_
// toggle -- a build with no system telemetry would have nothing to show on
// the Telemetry page.
//
// `div`/`dec`/`fmt` are display hints only. vdd stays millivolts on the wire
// (as docs/api.md specifies) and the browser divides by 1000 to show volts, so
// the firmware never has to serialize a float it doesn't need to. Uptime is
// the same bargain in a different shape: milliseconds on the wire, HH:MM:SS on
// screen -- and no unit, because "01:23:45 ms" would be a lie.
//
// Free RAM is a heap figure in the tens of kilobytes. Bytes is a digit of
// precision nobody reading a dashboard can act on, so it is declared as kB to
// one decimal. Note kB here is 1024 bytes, the embedded convention, not 1000.
static const TlmDef kTlm[T_COUNT] = {
  // key    label       unit   type            div   dec  fmt      group
  {"up",   "Uptime",   nullptr, TlmType::U32,   0,   0,  "hms",   nullptr},
  {"clk",  "Clock",    "MHz", TlmType::U32,     0,   0,  nullptr, nullptr},
  {"ram",  "Free RAM", "kB",  TlmType::I32,  1024,   1,  nullptr, nullptr},
  {"temp", "Temp",     "°C",  TlmType::F32,     0,   1,  nullptr, nullptr},
  {"vdd",  "VDD",      "V",   TlmType::I32,  1000,   2,  nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "system", "System",
  nullptr, 0,
  kTlm, T_COUNT,
};

}  // namespace sys
