#include "features/drive/drive_params.h"

namespace drive {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

// Order must match core::Inputs' slot indices directly -- "ch1" is slot 0 --
// same convention motor0.src/motor1.src use. A local copy, not shared with their
// tables: no cross-module sharing mechanism exists in this tree, and
// inventing one for three call sites isn't worth it (same reasoning motor0
// and motor1's own duplicate tables already establish).
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

// Order must match drive::MODE_* in drive_params.h.
static const char* const kModes[] = {"skid", "car"};

// The board header states the vehicle layout; drive.mode is the runtime
// override. The #ifndef fallback is required regardless -- this descriptor TU
// is compiled by the native env, which has no board header.
#ifndef DRIVE_MODE_DEFAULT
#define DRIVE_MODE_DEFAULT drive::MODE_SKID
#endif

static const ParamDef kParams[] = {
  // Declared FIRST: DriveDriver::apply() re-reads every parameter on any one
  // changing, so declaration order is the order values become known during a
  // one-at-a-time apply sequence. skid mixes throttle and steer into left and
  // right tracks; car passes throttle to slot 0 and steer to slot 1 untouched.
  {"drive.mode", ParamType::Enum, "Drive Mode", nullptr, 0, 0, kModes, 2, 0, DRIVE_MODE_DEFAULT, nullptr, nullptr, nullptr, nullptr},
  // key                          type             label            unit  min max opts       n  maxlen def defStr group
  // Default throttle=ch2, steer=ch1: a Mode 2 handset puts elevator on ch2 and
  // aileron on ch1, so this is the pair that lands under the sticks a driver
  // expects. Reassignable once connected, same as every other .src default in
  // this tree.
  {"drive.throttle_src",  ParamType::Enum, "Throttle Src", nullptr, 0, 0, kSrcNames, 12, 0, 1, nullptr, nullptr, nullptr, nullptr},
  {"drive.steer_src",     ParamType::Enum, "Steer Src",    nullptr, 0, 0, kSrcNames, 12, 0, 0, nullptr, nullptr, nullptr, nullptr},
  // Percent of full power in each direction, and of full turn authority.
  // Independent knobs: capping forward leaves a zero-throttle pivot alone, and
  // capping steer leaves straight-line speed alone. All default to 100
  // (unscaled) -- nothing already deployed changes behavior unless lowered.
  // These cap at the MIXER, deliberately not via motor<N>.min_us/max_us: that
  // range is the ESC's calibration, and narrowing it also moves neutralUs().
  {"drive.forward_ratio", ParamType::U8,   "Forward Ratio", "%",    0, 100, nullptr, 0, 0, 100, nullptr, nullptr, nullptr, nullptr},
  {"drive.reverse_ratio", ParamType::U8,   "Reverse Ratio", "%",    0, 100, nullptr, 0, 0, 100, nullptr, nullptr, nullptr, nullptr},
  {"drive.steer_ratio",   ParamType::U8,   "Steer Ratio",   "%",    0, 100, nullptr, 0, 0, 100, nullptr, nullptr, nullptr, nullptr},
  // Shared ARM switch, default ch5 -- kArmSrcNames index 5 (none=0, ch1=1,
  // ..., ch5=5), the conventional switch channel on this build's TX. Still
  // reassignable to any other channel, or to "none" to turn the feature off,
  // once connected. arm_min/arm_max default to a high-side band
  // (1700-2000us), the conventional "switch flipped up" position on a
  // two-position TX switch.
  //
  // No showIf here: showIf is a strict-equality display hint (see
  // core/params.h) and cannot express "shown when arm_src != none" -- these
  // two always render on the Controller/Modes pages. The Modes page hides/
  // shows the range via its own widget logic instead, not through this
  // mechanism.
  {"drive.arm_src", ParamType::Enum, "Arm Src", nullptr, 0, 0, kArmSrcNames, 13, 0, 5, nullptr, nullptr, nullptr, nullptr},
  {"drive.arm_min", ParamType::U8,   "Arm Min", "µs",    1000, 2000, nullptr, 0, 0, 1700, nullptr, nullptr, nullptr, nullptr},
  {"drive.arm_max", ParamType::U8,   "Arm Max", "µs",    1000, 2000, nullptr, 0, 0, 2000, nullptr, nullptr, nullptr, nullptr},
};

// The outputs the mixer is currently commanding -- "commanded, not measured"
// honesty, same as motor0/motor1's own telemetry.
static const TlmDef kTlm[T_COUNT] = {
  // key      label    unit         type          div dec fmt      group
  {"drv_l",  "Left",  "\xc2\xb5s", TlmType::U32,  0,  0, nullptr, nullptr},
  {"drv_r",  "Right", "\xc2\xb5s", TlmType::U32,  0,  0, nullptr, nullptr},
  {"drv_s",  "Steer", "\xc2\xb5s", TlmType::U32,  0,  0, nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "drive", "Drive",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

}  // namespace drive
