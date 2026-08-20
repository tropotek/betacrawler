#pragma once
#include "core/module.h"

namespace vbat {

extern const core::ModuleDesc kDesc;

// Parameter indices within this module -- what onParamChanged() receives.
enum : uint8_t { P_SOURCE = 0, P_SCALE = 1, P_CELLS = 2 };

// Order must match kSourceNames in vbat_params.cpp.
enum : uint8_t { SRC_OFF = 0, SRC_ADC = 1, SRC_SIM = 2 };

// vbat.cells option 0 is "auto"; options 1..5 are literal counts 2..6, so a
// selected index i > 0 means i + 1 cells.
enum : uint8_t { CELLS_AUTO = 0 };

// Telemetry indices within this module's slice of the frame.
enum : uint8_t { T_MV = 0, T_CELLS = 1, T_COUNT = 2 };

}  // namespace vbat
