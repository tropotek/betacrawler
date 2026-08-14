#pragma once
#include "core/module.h"

namespace tank_drive {

extern const core::ModuleDesc kDesc;

// Parameter indices *within this module* -- what onParamChanged() receives.
enum : uint8_t { P_THROTTLE_SRC = 0, P_STEER_SRC = 1, P_REVERSE_RATIO = 2 };

// Telemetry indices within this module's slice of the frame.
enum : uint8_t { T_LEFT = 0, T_RIGHT = 1, T_COUNT = 2 };

}  // namespace tank_drive
