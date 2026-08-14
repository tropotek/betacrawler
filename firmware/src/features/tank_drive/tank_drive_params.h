#pragma once
#include "core/module.h"

namespace tank_drive {

extern const core::ModuleDesc kDesc;

// Values of a tank_drive.arm_src parameter, in declaration order -- "none"
// (feature off) first, then ch1..ch12 map to core::Inputs slots 0..11 the
// same "index - 1 = slot" convention esc0.src/esc1.src's own kSrcNames use.
enum : int32_t { ARM_SRC_NONE = 0 };

// Parameter indices *within this module* -- what onParamChanged() receives.
enum : uint8_t {
  P_THROTTLE_SRC = 0, P_STEER_SRC = 1, P_REVERSE_RATIO = 2,
  P_ARM_SRC = 3, P_ARM_MIN = 4, P_ARM_MAX = 5,
};

// Telemetry indices within this module's slice of the frame.
enum : uint8_t { T_LEFT = 0, T_RIGHT = 1, T_COUNT = 2 };

}  // namespace tank_drive
