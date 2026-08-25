#include "hardware/servo/servo_params.h"
#include "core/triangle.h"

namespace servo {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

// Order must match the MODE_* constants in servo_params.h -- the wire carries
// the name, the driver receives the index.
static const char* const kModes[] = {"off", "hold", "sweep", "input"};

// ch1..ch12 index core::Inputs' slots directly -- "ch1" is slot 0 -- named to
// match the RX module's own ch1..ch16 telemetry fields. Indices 12/13 select
// the drive bus instead, and the driver subtracts kDriveSrcBase to reach its
// slot.
static const char* const kSrcNames[] = {
  "ch1", "ch2", "ch3", "ch4", "ch5", "ch6",
  "ch7", "ch8", "ch9", "ch10", "ch11", "ch12",
  // Indices 12/13/14: the drive module's own bus
  // (core::Registry::driveOutputs()), not a raw rx channel. Same convention
  // motor0.src/motor1.src use.
  "drive_left", "drive_right", "drive_steer",
};

static const char* const kInvertOpts[] = {"normal", "reversed"};

static const ParamDef kParams[] = {
  // key            type             label    unit  min   max   opts    n  maxlen def       defStr group
  // Defaults to off, not hold: the board resets on every DFU flash, and this
  // template cannot know what linkage the servo is attached to, so nothing is
  // commanded until asked. Saved settings ARE re-applied at boot by main.cpp's
  // notify pass -- that is the point of saving, and begin() detaches first, so
  // the servo settles once rather than twitching on the way.
  {"servo.mode",    ParamType::Enum, "Servo", nullptr, 0,    0,    kModes, 4, 0, MODE_OFF, nullptr, nullptr},
  {"servo.angle",   ParamType::U8,   "Angle", "°",     0,    180,  nullptr, 0, 0, 90,      nullptr, nullptr},
  // Seconds per FULL cycle, not Hz: a 1Hz sweep is 0->180->0 in one second,
  // which an SG90 cannot physically track, so an Hz range would have been
  // unusable end to end. Maps straight onto trianglePercent's periodMs.
  {"servo.sweep_s", ParamType::U8,   "Sweep", "s",     1,    30,   nullptr, 0, 0, 4,       nullptr, nullptr},
  // Calibration ends. The bounds deliberately CANNOT cross -- min tops out
  // where max starts -- because core/ has no cross-parameter constraint
  // mechanism: ParamDef bounds are static and setNum validates one value in
  // isolation. An inverted span is made structurally impossible rather than
  // merely discouraged. They can still MEET, which angleToUs handles.
  {"servo.min_us",  ParamType::U8,   "Min",   "µs",    500,  1500, nullptr, 0, 0, 1000,    nullptr, nullptr},
  {"servo.max_us",  ParamType::U8,   "Max",   "µs",    1500, 2500, nullptr, 0, 0, 2000,    nullptr, nullptr},
  // Meaningful only when servo.mode == input -- mode does the enabling,
  // this only selects which of core::Inputs' 12 published channels to
  // follow. showIf hides it from the UI otherwise; Terminal `set` and INI
  // restore still accept it regardless (showIf is display-only, never an
  // access rule).
  //
  // Defaults to the drive module's steering slot, so a car works on stock
  // settings: the mixer has already applied drive.steer_src and steer_ratio,
  // and the same slot carries steer in both drive modes.
  {"servo.src",     ParamType::Enum, "Source", nullptr, 0, 0, kSrcNames, 15, 0, 14, nullptr, nullptr},
  // Linkage properties, so they apply in every output mode -- hold, sweep and
  // input alike. Invert runs first, then trim, then the range clamp, so trim
  // always moves the horn the same physical direction either way round.
  {"servo.invert",  ParamType::Enum, "Invert", nullptr, 0, 0, kInvertOpts, 2, 0, INVERT_NORMAL, nullptr, nullptr},
  {"servo.trim_us", ParamType::U8,   "Trim",   "\xc2\xb5s", -250, 250, nullptr, 0, 0, 0, nullptr, nullptr},
};

// The commanded pulse width, or 0 when off. There is no position feedback --
// this is deliberately what was asked for, not what was achieved. It is the
// only visible truth in sweep mode, where servo.angle sits still while the
// output moves, and it makes the min_us/max_us mapping checkable live instead
// of inferred from servo noise.
//
// Key is a bare word: dotted keys are the *parameter* convention, and vdd
// sets the precedent that the unit lives in the TlmDef, not the key.
static const TlmDef kTlm[T_COUNT] = {
  // key    label    unit  type          div  dec  fmt      group
  {"srv",  "Servo", "µs", TlmType::U32,  0,   0,  nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "servo", "Servo",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

uint16_t angleToUs(uint8_t angle, uint16_t minUs, uint16_t maxUs) {
  if (angle > 180) angle = 180;
  int32_t span = (int32_t)maxUs - (int32_t)minUs;
  return (uint16_t)((int32_t)minUs + span * (int32_t)angle / 180);
}

uint8_t sweepAngle(uint32_t phaseMs, uint32_t periodMs) {
  // trianglePercent is already the symmetric triangle wave a sweep needs, and
  // it is already natively tested -- a second implementation here would be
  // pure duplication.
  return (uint8_t)((uint32_t)core::trianglePercent(phaseMs, periodMs) * 180u / 100u);
}

uint32_t rephase(uint32_t elapsedMs, uint32_t oldPeriodMs, uint32_t newPeriodMs) {
  if (oldPeriodMs == 0) return 0;
  uint32_t phase = elapsedMs % oldPeriodMs;
  // 64-bit intermediate: phase and newPeriodMs are both bounded by the
  // servo.sweep_s range (30s), so 32 bits would in fact do -- but the bound
  // lives in a ParamDef three files away, and this is not the place to depend
  // on it.
  return (uint32_t)((uint64_t)phase * newPeriodMs / oldPeriodMs);
}

uint16_t clampUs(int32_t us, uint16_t minUs, uint16_t maxUs) {
  if (us < (int32_t)minUs) return minUs;
  if (us > (int32_t)maxUs) return maxUs;
  return (uint16_t)us;
}

uint16_t applyInvert(uint16_t us, uint16_t minUs, uint16_t maxUs, bool inverted) {
  if (!inverted) return us;
  return clampUs((int32_t)minUs + (int32_t)maxUs - (int32_t)us, minUs, maxUs);
}

uint16_t applyTrim(uint16_t us, int32_t trimUs, uint16_t minUs, uint16_t maxUs) {
  return clampUs((int32_t)us + trimUs, minUs, maxUs);
}

}  // namespace servo
