#include "hardware/vbat/vbat_params.h"
#include "config.h"

namespace vbat {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

// Order must match SRC_*. Defaults to adc: a board carrying this module is
// expected to have the divider fitted, so reading the pin is the useful
// default and needs no setup. off is for a board built without one; sim
// fabricates a pack and is reachable from the Terminal, not the app, since
// it exists to exercise the CRSF uplink before a divider is built.
static const char* const kSourceNames[] = {"off", "adc", "sim"};

// Bare numbers so Terminal `set vbat.cells 4` and an INI line read the way a
// user would write them, same convention esc0.rate uses. Index 0 is auto.
static const char* const kCellNames[] = {"auto", "2", "3", "4", "5", "6"};

// The board header states the divider fitted to this hardware; vbat.scale is
// the runtime override, set by calibrating. Same #ifndef fallback
// esc0_params.cpp uses for ESC0_FRAME_US, and for the same reason: this
// descriptor TU is compiled by the native env too, where no board header
// value is meaningful.
#ifndef VBAT_SCALE_DEFAULT
#define VBAT_SCALE_DEFAULT 11000
#endif

static const ParamDef kParams[] = {
  // key           type             label     unit     min   max    opts          n  maxlen def         defStr   group
  {"vbat.source",  ParamType::Enum, "Source", nullptr, 0,    0,     kSourceNames, 3, 0,     SRC_ADC,    nullptr, nullptr},
  // x1000 multiplier from tap millivolts to pack millivolts, so a 1:10
  // divider is 10000. Whatever the board header states is only the starting
  // point -- calibrating against a multimeter is what makes it right.
  {"vbat.scale",   ParamType::U8,   "Scale",  nullptr, 1000, 30000, nullptr,      0, 0,     VBAT_SCALE_DEFAULT, nullptr, nullptr},
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
