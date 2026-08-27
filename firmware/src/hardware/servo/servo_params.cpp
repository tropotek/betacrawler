#include "hardware/servo/servo_params.h"

namespace servo {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

static const char* const kInvertOpts[] = {"normal", "reversed"};

static const ParamDef kParams[] = {
  // key            type             label    unit  min   max   opts    n  maxlen def       defStr group
  // Calibration ends. The bounds deliberately CANNOT cross -- min tops out
  // where max starts -- because core/ has no cross-parameter constraint
  // mechanism: ParamDef bounds are static and setNum validates one value in
  // isolation. An inverted span is made structurally impossible rather than
  // merely discouraged. They can still MEET, which clampUs handles.
  {"servo.min_us",  ParamType::U8,   "Min",   "\xc2\xb5s",  500,  1500, nullptr, 0, 0, 1000,    nullptr, nullptr},
  {"servo.max_us",  ParamType::U8,   "Max",   "\xc2\xb5s",  1500, 2500, nullptr, 0, 0, 2000,    nullptr, nullptr},
  // Linkage properties. Invert runs first, then trim, then the range clamp, so
  // trim always moves the horn the same physical direction either way round.
  {"servo.invert",  ParamType::Enum, "Invert", nullptr, 0, 0, kInvertOpts, 2, 0, INVERT_NORMAL, nullptr, nullptr},
  {"servo.trim_us", ParamType::U8,   "Trim",   "\xc2\xb5s", -250, 250, nullptr, 0, 0, 0, nullptr, nullptr},
};

// The commanded pulse width, or 0 when detached. There is no position feedback
// -- this is deliberately what was asked for, not what was achieved. It makes
// the min_us/max_us mapping checkable live instead of inferred from servo
// noise.
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
