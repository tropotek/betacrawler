#pragma once
#include "core/module.h"
#include "hardware/esc/esc_math.h"

namespace esc0 {

extern const core::ModuleDesc kDesc;

// Parameter indices *within this module* -- what onParamChanged() receives.
// Local, so nothing outside esc0/ depends on where these landed in the
// global table, and adding a module elsewhere can never shift them.
enum : uint8_t { P_DIRECTION = 0, P_RATE = 1, P_MODE = 2, P_THROTTLE_US = 3, P_MIN_US = 4, P_MAX_US = 5, P_SRC = 6 };

// Telemetry indices within this module's slice of the frame.
enum : uint8_t { T_US = 0, T_ARM = 1, T_COUNT = 2 };

}  // namespace esc0
