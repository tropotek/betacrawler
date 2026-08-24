#pragma once
#include "core/module.h"

namespace drive {

extern const core::ModuleDesc kDesc;

// Values of a drive.arm_src parameter, in declaration order -- "none"
// (feature off) first, then ch1..ch12 map to core::Inputs slots 0..11 the
// same "index - 1 = slot" convention motor0.src/motor1.src's own kSrcNames use.
enum : int32_t { ARM_SRC_NONE = 0 };

// Values of the drive.mode enum, in declaration order. skid, not tank: the
// differential mix is identical on tracks, on a 4WD rover and on a 2WD
// skid-steer rover.
enum : int32_t { MODE_SKID = 0, MODE_CAR = 1 };

// Parameter indices *within this module* -- what onParamChanged() receives.
enum : uint8_t {
  P_MODE = 0,
  P_THROTTLE_SRC = 1, P_STEER_SRC = 2,
  P_FORWARD_RATIO = 3, P_REVERSE_RATIO = 4, P_STEER_RATIO = 5,
  P_ARM_SRC = 6, P_ARM_MIN = 7, P_ARM_MAX = 8,
};

// Telemetry indices within this module's slice of the frame.
enum : uint8_t { T_LEFT = 0, T_RIGHT = 1, T_COUNT = 2 };

}  // namespace drive
