#include "hardware/st7789_240x240/st7789_240x240_params.h"

namespace st7789 {

using core::ParamDef;
using core::ParamType;

// Order must match the MODE_*/PAGE_* constants in display_params.h -- the wire
// carries the name, the driver receives the index.
static const char* const kModes[] = {"off", "on"};
static const char* const kPages[] = {"info", "stats", "cycle"};

static const ParamDef kParams[] = {
  // key          type             label      unit  min max opts    n  maxlen def        defStr group
  {"disp.mode",   ParamType::Enum, "Display", nullptr, 0, 0, kModes, 2, 0, MODE_ON,   nullptr, nullptr},
  {"disp.page",   ParamType::Enum, "Page",    nullptr, 0, 0, kPages, 3, 0, PAGE_INFO, nullptr, nullptr},
  // Deliberately separate from tlm.rate: the panel refreshes whether or not
  // the host is streaming telemetry, and a dashboard wants a slower, steadier
  // cadence than a live graph does. Capped at 10Hz because each refresh costs
  // real SPI time.
  {"disp.rate",   ParamType::U8,   "Refresh", "Hz",  1, 10, nullptr, 0, 0, 2,         nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "st7789_240x240", "Display",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  nullptr, 0,     // the panel reports no telemetry -- it only renders it
};

}  // namespace st7789
