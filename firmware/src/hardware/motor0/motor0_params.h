#pragma once
#include "core/module.h"
#include "hardware/motor/motor_math.h"

namespace motor0 {

extern const core::ModuleDesc kDesc;

// Parameter indices *within this module* -- what onParamChanged() receives.
// Local, so nothing outside motor0/ depends on where these landed in the
// global table, and adding a module elsewhere can never shift them.
enum : uint8_t {
  P_TYPE = 0, P_DIRECTION = 1, P_RATE = 2, P_MODE = 3, P_THROTTLE_US = 4,
  P_MIN_US = 5, P_MAX_US = 6, P_SRC = 7, P_FREQ = 8, P_INVERT = 9, P_BRAKE = 10,
};

// Telemetry indices within this module's slice of the frame.
enum : uint8_t { T_US = 0, T_ARM = 1, T_COUNT = 2 };

}  // namespace motor0
