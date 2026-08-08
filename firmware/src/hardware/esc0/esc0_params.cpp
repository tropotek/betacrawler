#include "hardware/esc0/esc0_params.h"

namespace esc0 {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

// Order must match esc::MODE_* -- the wire carries the name, the driver
// receives the index.
static const char* const kModes[] = {"off", "armed", "input"};

// Order must match core::Inputs' slot indices directly -- "ch1" is slot 0 --
// same convention esc1.src and servo.src use, named to match rx's own
// ch1..ch16 telemetry naming.
static const char* const kSrcNames[] = {
  "ch1", "ch2", "ch3", "ch4", "ch5", "ch6",
  "ch7", "ch8", "ch9", "ch10", "ch11", "ch12",
};

static const char* const kDirections[] = {"unidirectional", "bidirectional"};

static const ParamDef kParams[] = {
  // key                type             label       unit  min   max   opts     n  maxlen def       defStr group
  // Declared FIRST, ahead of esc0.mode -- not just cosmetic. EscDriver::apply()
  // re-reads every one of this module's params from the shared Params store
  // on ANY of them changing, so declaration order is also the order values
  // become known during a one-at-a-time apply sequence (INI restore, or a
  // human typing Terminal `set` commands). Putting esc0.direction first
  // guarantees it is always already known -- never still at its stale prior
  // value -- by the time esc0.mode or esc0.throttle_us can cause a pulse to
  // be computed and armed against. Defaults to unidirectional: nothing
  // already deployed changes behaviour unless explicitly switched over.
  {"esc0.direction",    ParamType::Enum, "Direction", nullptr, 0, 0, kDirections, 2, 0, esc::DIR_UNIDIRECTIONAL, nullptr, nullptr},
  // Defaults to off: the board resets on every DFU flash, and nothing should
  // be commanded to an ESC until asked, same reasoning as servo.mode's
  // default. Saved settings ARE re-applied at boot by main.cpp's notify
  // pass, which is exactly where the arm-hold gate matters most.
  {"esc0.mode",         ParamType::Enum, "ESC",      nullptr, 0,    0,    kModes, 3, 0, esc::MODE_OFF, nullptr, nullptr},
  // Direct microseconds, not a percentage: the wire and the param are the
  // same unit, so esc::clampUs alone maps it.
  {"esc0.throttle_us",  ParamType::U8,   "Throttle", "µs",    1000, 2000, nullptr, 0, 0, 1000,     nullptr, nullptr},
  // Calibration ends. The bounds deliberately CANNOT cross -- min tops out
  // where max starts -- because core/ has no cross-parameter constraint
  // mechanism: ParamDef bounds are static and setNum validates one value in
  // isolation. Same trick esc1's own copy of this table uses.
  {"esc0.min_us",       ParamType::U8,   "Min",      "µs",    500,  1500, nullptr, 0, 0, 1000,     nullptr, nullptr},
  {"esc0.max_us",       ParamType::U8,   "Max",      "µs",    1500, 2500, nullptr, 0, 0, 2000,     nullptr, nullptr},
  // Only used when esc0.mode == input, but shown only when esc0.mode == off
  // -- the opposite of every other showIf in this codebase, deliberately:
  // this is a pre-arm configuration choice, not a live control, so it is
  // hidden once armed/input is live rather than left sitting in view where
  // an accidental change is easy to make. Terminal `set` and INI restore
  // still accept it regardless of mode (showIf is display-only, never an
  // access rule). Defaults to ch1, the conventional throttle channel.
  {"esc0.src",          ParamType::Enum, "Source",   nullptr, 0, 0, kSrcNames, 12, 0, 0, nullptr, nullptr, "esc0.mode", "off"},
};

// The commanded pulse width, or 0 when off -- including neutralUs during the
// arm-hold window, same "commanded, not measured" honesty servo's srv field
// has. There is no RPM/current feedback on this wiring.
//
// arm is a plain number (esc::ARM_OFF/ARM_ARMING/ARM_ARMED), not a string or
// a dedicated enum-tlm type -- core/ has neither.
static const TlmDef kTlm[T_COUNT] = {
  // key     label     unit  type          div  dec  fmt      group
  {"esc0",  "ESC 0",  "µs",    TlmType::U32,  0,   0,  nullptr, nullptr},
  {"arm0",  "Armed",  nullptr, TlmType::U32,  0,   0,  nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "esc0", "ESC 0",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

}  // namespace esc0
