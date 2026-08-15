#include "hardware/esc0/esc0_params.h"
#include "config.h"

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
  // Indices 12/13: tank_drive's own bus (see core::Registry::driveOutputs()),
  // not a raw rx channel. esc0 doesn't otherwise know tank_drive exists --
  // this is the one place that convention is spelled out.
  "drive_left", "drive_right",
};

static const char* const kDirections[] = {"unidirectional", "bidirectional"};

// Order must match esc::RATE_*. Bare numbers, so Terminal `set esc0.rate 400`
// and an INI line read the way a user would write them.
static const char* const kRates[] = {"50", "100", "200", "400"};

// The board header states the hardware default; this param is the runtime
// override. Same #ifndef fallback rx_params.cpp uses for RX_BAUD, and for the
// same reason: this descriptor TU is compiled by the native env too, where no
// driver ever reads the value.
#ifndef ESC0_FRAME_US
#define ESC0_FRAME_US 20000
#endif
static constexpr int32_t kDefaultRate =
    ESC0_FRAME_US <=  2500 ? esc::RATE_400 :
    ESC0_FRAME_US <=  5000 ? esc::RATE_200 :
    ESC0_FRAME_US <= 10000 ? esc::RATE_100 : esc::RATE_50;

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
  {"esc0.direction",    ParamType::Enum, "Direction", nullptr, 0, 0, kDirections, 2, 0, esc::DIR_BIDIRECTIONAL, nullptr, nullptr},
  // Ahead of esc0.mode for exactly the reason esc0.direction is: the frame
  // rate must already be known by the time a pulse can be computed and armed
  // against, since a BLHeli_S-class ESC frame-detects as it arms.
  {"esc0.rate",         ParamType::Enum, "PWM Rate", "Hz", 0, 0, kRates, 4, 0, kDefaultRate, nullptr, nullptr},
  // Defaults to input: the shared ARM switch (tank_drive.arm_src, itself
  // defaulting to a real channel) clamps the output to neutral whenever the
  // link is stale or the switch is inactive, and the arm-hold state machine
  // below still requires a held commanded-low before promoting to armed --
  // both gates apply regardless of this default, so nothing moves on boot
  // just because mode is already input. Saved settings ARE re-applied at
  // boot by main.cpp's notify pass, which is exactly where those gates
  // matter most.
  {"esc0.mode",         ParamType::Enum, "ESC",      nullptr, 0,    0,    kModes, 3, 0, esc::MODE_INPUT, nullptr, nullptr},
  // Direct microseconds, not a percentage: the wire and the param are the
  // same unit, so esc::clampUs alone maps it.
  {"esc0.throttle_us",  ParamType::U8,   "Throttle", "µs",    1000, 2000, nullptr, 0, 0, 1500,     nullptr, nullptr},
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
  {"esc0.src",          ParamType::Enum, "Source",   nullptr, 0, 0, kSrcNames, 14, 0, 12, nullptr, nullptr, "esc0.mode", "off"},
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
