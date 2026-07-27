#include "hardware/servo/servo_params.h"
#include "core/led_curve.h"

namespace servo {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

// Order must match the MODE_* constants in servo_params.h -- the wire carries
// the name, the driver receives the index.
static const char* const kModes[] = {"off", "hold", "sweep"};

static const ParamDef kParams[] = {
  // key            type             label    unit  min   max   opts    n  maxlen def       defStr group
  // Defaults to off, not hold: the board resets on every DFU flash, and this
  // template cannot know what linkage the servo is attached to, so nothing is
  // commanded until asked. Saved settings ARE re-applied at boot by main.cpp's
  // notify pass -- that is the point of saving, and begin() detaches first, so
  // the servo settles once rather than twitching on the way.
  {"servo.mode",    ParamType::Enum, "Servo", nullptr, 0,    0,    kModes, 3, 0, MODE_OFF, nullptr, nullptr},
  {"servo.angle",   ParamType::U8,   "Angle", "°",     0,    180,  nullptr, 0, 0, 90,      nullptr, nullptr},
  // Seconds per FULL cycle, not Hz: a 1Hz sweep is 0->180->0 in one second,
  // which an SG90 cannot physically track, so an Hz range would have been
  // unusable end to end. Maps straight onto breathingDuty's periodMs.
  {"servo.sweep_s", ParamType::U8,   "Sweep", "s",     1,    30,   nullptr, 0, 0, 4,       nullptr, nullptr},
  // Calibration ends. The bounds deliberately CANNOT cross -- min tops out
  // where max starts -- because core/ has no cross-parameter constraint
  // mechanism: ParamDef bounds are static and setNum validates one value in
  // isolation. An inverted span is made structurally impossible rather than
  // merely discouraged. They can still MEET, which angleToUs handles.
  {"servo.min_us",  ParamType::U8,   "Min",   "µs",    500,  1500, nullptr, 0, 0, 1000,    nullptr, nullptr},
  {"servo.max_us",  ParamType::U8,   "Max",   "µs",    1500, 2500, nullptr, 0, 0, 2000,    nullptr, nullptr},
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
  // key    label    unit  type          div  dec  group
  {"srv",  "Servo", "µs", TlmType::U32,  0,   0,  nullptr},
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
  // breathingDuty is already the symmetric triangle wave a sweep needs, and
  // it is already natively tested -- a second implementation here would be
  // pure duplication.
  return (uint8_t)((uint32_t)core::breathingDuty(phaseMs, periodMs) * 180u / 100u);
}

}  // namespace servo
