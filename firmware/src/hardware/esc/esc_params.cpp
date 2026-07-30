#include "hardware/esc/esc_params.h"

namespace esc {

uint16_t clampUs(int32_t us, uint16_t minUs, uint16_t maxUs) {
  if (us < (int32_t)minUs) return minUs;
  if (us > (int32_t)maxUs) return maxUs;
  return (uint16_t)us;
}

uint16_t neutralUs(uint16_t minUs, uint16_t maxUs, bool bidirectional) {
  return bidirectional ? (uint16_t)(((uint32_t)minUs + maxUs) / 2) : minUs;
}

uint32_t nextArmState(uint32_t prevState, bool modeIsOff, bool enteringFromOff,
                       uint32_t nowMs, uint32_t armT0Ms, uint32_t armHoldMs,
                       bool commandedIsLow) {
  if (modeIsOff) return ARM_OFF;
  if (enteringFromOff) return ARM_ARMING;
  if (prevState == ARM_ARMING && commandedIsLow && (nowMs - armT0Ms) >= armHoldMs) {
    return ARM_ARMED;
  }
  return prevState;
}

bool isCommandedLow(int32_t mode, uint16_t throttleUs, int16_t inputUs, bool inputFresh,
                     uint16_t neutralUs, uint16_t lowMarginUs, bool bidirectional) {
  int32_t v;
  if (mode == MODE_ARMED) {
    v = throttleUs;
  } else if (mode == MODE_INPUT) {
    if (!inputFresh || inputUs <= 0) return false;
    v = inputUs;
  } else {
    return false;
  }
  if (bidirectional) {
    int32_t d = v - (int32_t)neutralUs;
    if (d < 0) d = -d;
    return d <= (int32_t)lowMarginUs;
  }
  return v <= (int32_t)neutralUs + (int32_t)lowMarginUs;
}

bool isLinkFresh(uint32_t lastFreshMs, uint32_t nowMs, uint32_t staleMs) {
  if (lastFreshMs == 0) return false;   // core::Inputs' own "never marked" default, not a real timestamp
  return (nowMs - lastFreshMs) < staleMs;
}

bool inputLossDemotesArmed(uint32_t armState, int32_t mode, bool inputFresh) {
  return armState == ARM_ARMED && mode == MODE_INPUT && !inputFresh;
}

bool srcChangeDemotesArmed(uint32_t armState, int32_t mode, bool srcChanged) {
  return armState == ARM_ARMED && mode == MODE_INPUT && srcChanged;
}

uint16_t nextPulseUs(uint32_t armState, int32_t mode, uint16_t minUs, uint16_t maxUs,
                      uint16_t throttleUs, int16_t inputUs, bool inputStale, uint16_t neutralUs) {
  if (armState != ARM_ARMED) return neutralUs;
  if (mode == MODE_ARMED) return clampUs(throttleUs, minUs, maxUs);
  if (mode == MODE_INPUT) {
    if (inputStale) return neutralUs;
    if (inputUs <= 0) return 0;
    return clampUs(inputUs, minUs, maxUs);
  }
  return 0;
}

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

// Order must match the MODE_* constants in esc_params.h -- the wire carries
// the name, the driver receives the index.
static const char* const kModes[] = {"off", "armed", "input"};

// Order must match core::Inputs' slot indices directly -- "ch1" is slot 0 --
// same convention servo.src already uses, named to match rx's own
// ch1..ch16 telemetry naming.
static const char* const kSrcNames[] = {
  "ch1", "ch2", "ch3", "ch4", "ch5", "ch6",
  "ch7", "ch8", "ch9", "ch10", "ch11", "ch12",
};

static const char* const kDirections[] = {"unidirectional", "bidirectional"};

static const ParamDef kParams[] = {
  // key              type             label       unit  min   max   opts     n  maxlen def       defStr group
  // Defaults to off: the board resets on every DFU flash, and nothing should
  // be commanded to an ESC until asked, same reasoning as servo.mode's
  // default. Saved settings ARE re-applied at boot by main.cpp's notify
  // pass, which is exactly where the arm-hold gate matters most -- see
  // esc_driver.cpp.
  {"esc.mode",         ParamType::Enum, "ESC",      nullptr, 0,    0,    kModes, 3, 0, MODE_OFF, nullptr, nullptr},
  // Direct microseconds, not a percentage: the wire and the param are the
  // same unit, so clampUs alone maps it, with no separate angle-style
  // mapping function needed.
  {"esc.throttle_us",  ParamType::U8,   "Throttle", "µs",    1000, 2000, nullptr, 0, 0, 1000,     nullptr, nullptr},
  // Calibration ends. The bounds deliberately CANNOT cross -- min tops out
  // where max starts -- because core/ has no cross-parameter constraint
  // mechanism: ParamDef bounds are static and setNum validates one value in
  // isolation. Same trick servo.min_us/max_us already uses.
  {"esc.min_us",       ParamType::U8,   "Min",      "µs",    500,  1500, nullptr, 0, 0, 1000,     nullptr, nullptr},
  {"esc.max_us",       ParamType::U8,   "Max",      "µs",    1500, 2500, nullptr, 0, 0, 2000,     nullptr, nullptr},
  // Defaults to unidirectional: every board already shipped assumes the low
  // end is stop, so nothing already deployed changes behaviour unless
  // explicitly switched over. Always shown, like min_us/max_us -- it changes
  // what "safe" and "low enough to arm" mean in every mode, so hiding it
  // behind a showIf would hide something load-bearing. See
  // _notes/spec-esc.md's "Amendment 2".
  {"esc.direction",    ParamType::Enum, "Direction", nullptr, 0, 0, kDirections, 2, 0, DIR_UNIDIRECTIONAL, nullptr, nullptr},
  // Meaningful only when esc.mode == input -- mode does the enabling, this
  // only selects which of core::Inputs' 12 published channels to follow.
  // showIf hides it from the UI otherwise; Terminal `set` and INI restore
  // still accept it regardless (showIf is display-only, never an access
  // rule). Defaults to ch3 (pitch -- right stick vertical), confirmed on the
  // bench, not ch1: self-centers, which is what makes bidirectional
  // throttle safe to release, unlike the (non-centering) throttle stick.
  // Paired deliberately with servo.src's own ch2 (roll) default -- see
  // _notes/spec-esc.md's "Amendment 2".
  {"esc.src",          ParamType::Enum, "Source",   nullptr, 0, 0, kSrcNames, 12, 0, 2, nullptr, nullptr, "esc.mode", "input"},
};

// The commanded pulse width, or 0 when off -- including minUs during the
// arm-hold window, same "commanded, not measured" honesty servo's srv field
// has. There is no RPM/current feedback on this wiring.
//
// arm is a plain number (ARM_OFF/ARM_ARMING/ARM_ARMED), not a string or a
// dedicated enum-tlm type -- core/ has neither, and rx's link field already
// sets the "a status reading is just a number" precedent this extends to
// three states.
static const TlmDef kTlm[T_COUNT] = {
  // key    label     unit  type          div  dec  fmt      group
  {"esc",  "ESC",    "µs",    TlmType::U32,  0,   0,  nullptr, nullptr},
  {"arm",  "Armed",  nullptr, TlmType::U32,  0,   0,  nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "esc", "ESC",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

}  // namespace esc
