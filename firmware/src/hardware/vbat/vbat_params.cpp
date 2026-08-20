#include "hardware/vbat/vbat_params.h"
#include "config.h"

namespace vbat {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

// Order must match SRC_*. Defaults to off: a board with no divider fitted
// must not publish a reading, the same reasoning rx.source defaults to uart.
static const char* const kSourceNames[] = {"off", "adc", "sim"};

// Bare numbers so Terminal `set vbat.cells 4` and an INI line read the way a
// user would write them, same convention esc0.rate uses. Index 0 is auto.
static const char* const kCellNames[] = {"auto", "2", "3", "4", "5", "6"};

static const ParamDef kParams[] = {
  // key           type             label     unit     min   max    opts          n  maxlen def         defStr   group
  {"vbat.source",  ParamType::Enum, "Source", nullptr, 0,    0,     kSourceNames, 3, 0,     SRC_OFF,    nullptr, nullptr},
  // x1000 multiplier from tap millivolts to pack millivolts. 9393 is the
  // 47k/5k6 divider: ratio 0.106464, so 1/0.106464 = 9.3929. Calibrated from
  // the Configurator against a multimeter.
  {"vbat.scale",   ParamType::U8,   "Scale",  nullptr, 1000, 30000, nullptr,      0, 0,     9393,       nullptr, nullptr},
  {"vbat.cells",   ParamType::Enum, "Cells",  nullptr, 0,    0,     kCellNames,   6, 0,     CELLS_AUTO, nullptr, nullptr},
};

// Pack millivolts on the wire; div/dec are display hints only, exactly as
// sys.vdd does it. cells is the LATCHED count, 0 before detection -- it
// exists so a misdetection is visible rather than silently skewing percent.
static const TlmDef kTlm[T_COUNT] = {
  // key      label      unit     type          div   dec  fmt      group
  {"vbat",   "Battery", "V",     TlmType::U32, 1000, 2,   nullptr, nullptr},
  {"cells",  "Cells",   nullptr, TlmType::U32, 0,    0,   nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "vbat", "Battery",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

}  // namespace vbat
