#include "features/tank_drive/tank_drive_params.h"

namespace tank_drive {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

// Order must match core::Inputs' slot indices directly -- "ch1" is slot 0 --
// same convention esc0.src/esc1.src use. A local copy, not shared with their
// tables: no cross-module sharing mechanism exists in this tree, and
// inventing one for three call sites isn't worth it (same reasoning esc0
// and esc1's own duplicate tables already establish).
static const char* const kSrcNames[] = {
  "ch1", "ch2", "ch3", "ch4", "ch5", "ch6",
  "ch7", "ch8", "ch9", "ch10", "ch11", "ch12",
};

// arm_src's own table: "none" first (ARM_SRC_NONE, index 0), then ch1..ch12
// -- a separate array from kSrcNames since throttle_src/steer_src have no
// "none" option and must always select a real channel.
static const char* const kArmSrcNames[] = {
  "none", "ch1", "ch2", "ch3", "ch4", "ch5", "ch6",
  "ch7", "ch8", "ch9", "ch10", "ch11", "ch12",
};

static const ParamDef kParams[] = {
  // key                          type             label            unit  min max opts       n  maxlen def defStr group
  // Default ch1/ch2: the conventional throttle/steer channel pair,
  // reassignable once connected, same as every other .src default in this
  // tree.
  {"tank_drive.throttle_src",  ParamType::Enum, "Throttle Src", nullptr, 0, 0, kSrcNames, 12, 0, 0, nullptr, nullptr, nullptr, nullptr},
  {"tank_drive.steer_src",     ParamType::Enum, "Steer Src",    nullptr, 0, 0, kSrcNames, 12, 0, 1, nullptr, nullptr, nullptr, nullptr},
  // Percent of full reverse power. Defaults to 100 (unscaled reverse) --
  // nothing already deployed changes behavior unless explicitly lowered.
  {"tank_drive.reverse_ratio", ParamType::U8,   "Reverse Ratio", "%",    0, 100, nullptr, 0, 0, 100, nullptr, nullptr, nullptr, nullptr},
  // Shared ARM switch, default none -- feature off, zero behavior change for
  // anyone not using it. arm_min/arm_max default to a high-side band
  // (1700-2000us), the conventional "switch flipped up" position on a
  // two-position TX switch; inert until arm_src selects a channel.
  //
  // No showIf here: showIf is a strict-equality display hint (see
  // core/params.h) and cannot express "shown when arm_src != none" -- these
  // two always render on the generic Configuration page. The Modes page
  // (a separate, later piece) hides/shows the range via its own widget
  // logic instead, not through this mechanism.
  {"tank_drive.arm_src", ParamType::Enum, "Arm Src", nullptr, 0, 0, kArmSrcNames, 13, 0, ARM_SRC_NONE, nullptr, nullptr, nullptr, nullptr},
  {"tank_drive.arm_min", ParamType::U8,   "Arm Min", "µs",    1000, 2000, nullptr, 0, 0, 1700, nullptr, nullptr, nullptr, nullptr},
  {"tank_drive.arm_max", ParamType::U8,   "Arm Max", "µs",    1000, 2000, nullptr, 0, 0, 2000, nullptr, nullptr, nullptr, nullptr},
};

// The computed output each side is currently commanding -- "commanded, not
// measured" honesty, same as esc0/esc1's own telemetry.
static const TlmDef kTlm[T_COUNT] = {
  // key      label    unit         type          div dec fmt      group
  {"drv_l",  "Left",  "\xc2\xb5s", TlmType::U32,  0,  0, nullptr, nullptr},
  {"drv_r",  "Right", "\xc2\xb5s", TlmType::U32,  0,  0, nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "tank_drive", "Tank Drive",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

}  // namespace tank_drive
