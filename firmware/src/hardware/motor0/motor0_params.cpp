#include "hardware/motor0/motor0_params.h"
#include "config.h"

namespace motor0 {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

static const char* const kTypes[] = {"brushless", "brushed"};

// Order must match motor::MODE_* -- the wire carries the name, the driver
// receives the index.
static const char* const kModes[] = {"off", "armed", "input"};

// Order must match core::Inputs' slot indices directly -- "ch1" is slot 0 --
// same convention motor1.src and servo.src use, named to match rx's own
// ch1..ch16 telemetry naming.
static const char* const kSrcNames[] = {
  "ch1", "ch2", "ch3", "ch4", "ch5", "ch6",
  "ch7", "ch8", "ch9", "ch10", "ch11", "ch12",
  // Indices 12/13: drive's own bus (see core::Registry::driveOutputs()),
  // not a raw rx channel. motor0 doesn't otherwise know drive exists --
  // this is the one place that convention is spelled out.
  "drive_left", "drive_right",
};

// Order must match motor::RATE_*. Bare numbers, so Terminal `set motor0.rate 400`
// and an INI line read the way a user would write them.
static const char* const kRates[] = {"50", "100", "200", "400"};

static const char* const kInvertOpts[] = {"normal", "inverted"};
static const char* const kBrakeOpts[] = {"coast", "brake"};

// The board header states the hardware default; this param is the runtime
// override. Same #ifndef fallback rx_params.cpp uses for RX_BAUD, and for the
// same reason: this descriptor TU is compiled by the native env too, where no
// driver ever reads the value.
#ifndef MOTOR0_FRAME_US
#define MOTOR0_FRAME_US 20000
#endif
// The board header states which motor this build is wired for;
// motor0.type is the runtime override. Same #ifndef fallback the frame period
// above uses, and for the same reason: this descriptor TU is compiled by the
// native env too.
#ifndef MOTOR0_TYPE_DEFAULT
#define MOTOR0_TYPE_DEFAULT motor::TYPE_BRUSHLESS
#endif

static constexpr int32_t kDefaultRate =
    MOTOR0_FRAME_US <=  2500 ? motor::RATE_400 :
    MOTOR0_FRAME_US <=  5000 ? motor::RATE_200 :
    MOTOR0_FRAME_US <= 10000 ? motor::RATE_100 : motor::RATE_50;

static const ParamDef kParams[] = {
  // key                type             label       unit  min   max   opts     n  maxlen def       defStr group        showIfKey     showIfVal
  // Declared FIRST, ahead of motor0.mode -- not just cosmetic. MotorDriver::apply()
  // re-reads every one of this module's params from the shared Params store
  // on ANY of them changing, so declaration order is also the order values
  // become known during a one-at-a-time apply sequence (INI restore, or a
  // human typing Terminal set commands). Putting motor0.type first guarantees
  // it is always already known -- it decides which OutputStage runs at all,
  // even more foundational than motor0.rate below it.
  {"motor0.type",         ParamType::Enum, "Type",      nullptr, 0, 0, kTypes, 2, 0, MOTOR0_TYPE_DEFAULT, nullptr, nullptr},
  // Shown only for type=esc: meaningless for a straight duty-cycle output.
  {"motor0.rate",         ParamType::Enum, "PWM Rate", "Hz", 0, 0, kRates, 4, 0, kDefaultRate, nullptr, nullptr, "motor0.type", "brushless"},
  // Defaults to off, like servo.mode and for the same reason: an unconfigured
  // board cannot know what is on the other end of the wire, so it commands
  // nothing until asked. Neutral is only safe for a controller that reads
  // pulses -- an H-bridge on a brushless-configured output reads a 1500us
  // pulse in a 5000us frame as 30% duty and runs the motor. Saved settings
  // ARE re-applied at boot by main.cpp's notify pass, so a configured board
  // still starts driving at power-on and its ESC still arms once, there.
  {"motor0.mode",         ParamType::Enum, "Motor",    nullptr, 0,    0,    kModes, 3, 0, motor::MODE_OFF, nullptr, nullptr},
  // Direct microseconds, not a percentage: the wire and the param are the
  // same unit, so motor::clampUs alone maps it.
  {"motor0.throttle_us",  ParamType::U8,   "Throttle", "µs",    1000, 2000, nullptr, 0, 0, 1500,     nullptr, nullptr},
  // Calibration ends. The bounds deliberately CANNOT cross -- min tops out
  // where max starts -- because core/ has no cross-parameter constraint
  // mechanism: ParamDef bounds are static and setNum validates one value in
  // isolation. Same trick motor1's own copy of this table uses.
  {"motor0.min_us",       ParamType::U8,   "Min",      "µs",    500,  1500, nullptr, 0, 0, 1000,     nullptr, nullptr},
  {"motor0.max_us",       ParamType::U8,   "Max",      "µs",    1500, 2500, nullptr, 0, 0, 2000,     nullptr, nullptr},
  // Only used when motor0.mode == input, but shown only when motor0.mode == off
  // -- the opposite of every other showIf in this codebase, deliberately:
  // this is a pre-arm configuration choice, not a live control, so it is
  // hidden once armed/input is live rather than left sitting in view where
  // an accidental change is easy to make. Terminal `set` and INI restore
  // still accept it regardless of mode (showIf is display-only, never an
  // access rule). Defaults to ch1, the conventional throttle channel.
  {"motor0.src",          ParamType::Enum, "Source",   nullptr, 0, 0, kSrcNames, 14, 0, 12, nullptr, nullptr, "motor0.mode", "off"},
  // Reverses which way this motor turns, for both output types: an H-bridge
  // swaps which pin is A/B, an ESC gets its pulse mirrored about neutral.
  // Useful when the motor is buried in an enclosed model and swapping two
  // phase wires means taking the shell off.
  {"motor0.invert",       ParamType::Enum, "Invert",   nullptr, 0, 0, kInvertOpts, 2, 0, 0, nullptr, nullptr},
  // Brushed-only, shown only for type=brushed.
  {"motor0.freq",         ParamType::U8,   "Switch Freq", "Hz", 1000, 50000, nullptr, 0, 0, 20000, nullptr, "H-Bridge", "motor0.type", "brushed"},
  {"motor0.brake",        ParamType::Enum, "At Zero",  nullptr, 0, 0, kBrakeOpts, 2, 0, 0, nullptr, "H-Bridge", "motor0.type", "brushed"},
};

// The commanded pulse width, or 0 when off -- including neutralUs during the
// arm-hold window, same "commanded, not measured" honesty servo's srv field
// has. There is no RPM/current feedback on this wiring.
//
// arm is a plain number (motor::ARM_OFF/ARM_ARMING/ARM_ARMED), not a string or
// a dedicated enum-tlm type -- core/ has neither.
static const TlmDef kTlm[T_COUNT] = {
  // key     label     unit  type          div  dec  fmt      group
  {"motor0",  "Motor 0",  "µs",    TlmType::U32,  0,   0,  nullptr, nullptr},
  {"arm0",  "Armed",  nullptr, TlmType::U32,  0,   0,  nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "motor0", "Motor 0",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

}  // namespace motor0
