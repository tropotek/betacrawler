#include "hardware/motor1/motor1_params.h"
#include "config.h"

namespace motor1 {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

static const char* const kTypes[] = {"brushless", "brushed"};

// Order must match motor::MODE_* -- the wire carries the name, the driver
// receives the index.
static const char* const kModes[] = {"off", "armed", "input"};

// Order must match core::Inputs' slot indices directly -- "ch1" is slot 0 --
// same convention motor0.src and servo.src use, named to match rx's own
// ch1..ch16 telemetry naming.
static const char* const kSrcNames[] = {
  "ch1", "ch2", "ch3", "ch4", "ch5", "ch6",
  "ch7", "ch8", "ch9", "ch10", "ch11", "ch12",
  // Indices 12/13: tank_drive's own bus (see core::Registry::driveOutputs()),
  // not a raw rx channel. motor1 doesn't otherwise know tank_drive exists --
  // this is the one place that convention is spelled out.
  "drive_left", "drive_right",
};

// Order must match motor::RATE_*. Bare numbers, so Terminal `set motor1.rate 400`
// and an INI line read the way a user would write them.
static const char* const kRates[] = {"50", "100", "200", "400"};

static const char* const kInvertOpts[] = {"normal", "inverted"};
static const char* const kBrakeOpts[] = {"coast", "brake"};

// The board header states the hardware default; this param is the runtime
// override. Same #ifndef fallback rx_params.cpp uses for RX_BAUD, and for the
// same reason: this descriptor TU is compiled by the native env too, where no
// driver ever reads the value.
#ifndef MOTOR1_FRAME_US
#define MOTOR1_FRAME_US 20000
#endif
// The board header states which motor this build is wired for;
// motor1.type is the runtime override. Same #ifndef fallback the frame period
// above uses, and for the same reason: this descriptor TU is compiled by the
// native env too.
#ifndef MOTOR1_TYPE_DEFAULT
#define MOTOR1_TYPE_DEFAULT motor::TYPE_BRUSHLESS
#endif

static constexpr int32_t kDefaultRate =
    MOTOR1_FRAME_US <=  2500 ? motor::RATE_400 :
    MOTOR1_FRAME_US <=  5000 ? motor::RATE_200 :
    MOTOR1_FRAME_US <= 10000 ? motor::RATE_100 : motor::RATE_50;

static const ParamDef kParams[] = {
  // key                type             label       unit  min   max   opts     n  maxlen def       defStr group        showIfKey     showIfVal
  // Declared FIRST, ahead of motor1.mode -- not just cosmetic. MotorDriver::apply()
  // re-reads every one of this module's params from the shared Params store
  // on ANY of them changing, so declaration order is also the order values
  // become known during a one-at-a-time apply sequence (INI restore, or a
  // human typing Terminal set commands). Putting motor1.type first guarantees
  // it is always already known -- it decides which OutputStage runs at all,
  // even more foundational than motor1.rate below it.
  {"motor1.type",         ParamType::Enum, "Type",      nullptr, 0, 0, kTypes, 2, 0, MOTOR1_TYPE_DEFAULT, nullptr, nullptr},
  // Shown only for type=esc: meaningless for a straight duty-cycle output.
  {"motor1.rate",         ParamType::Enum, "PWM Rate", "Hz", 0, 0, kRates, 4, 0, kDefaultRate, nullptr, nullptr, "motor1.type", "brushless"},
  // Defaults to input: the shared ARM switch (tank_drive.arm_src, itself
  // defaulting to a real channel) clamps the output to neutral whenever the
  // link is stale or the switch is inactive, and the arm-hold state machine
  // below still requires a held commanded-low before promoting to armed --
  // both gates apply regardless of this default, so nothing moves on boot
  // just because mode is already input. Saved settings ARE re-applied at
  // boot by main.cpp's notify pass, which is exactly where those gates
  // matter most.
  {"motor1.mode",         ParamType::Enum, "Motor",    nullptr, 0,    0,    kModes, 3, 0, motor::MODE_INPUT, nullptr, nullptr},
  // Direct microseconds, not a percentage: the wire and the param are the
  // same unit, so motor::clampUs alone maps it.
  {"motor1.throttle_us",  ParamType::U8,   "Throttle", "µs",    1000, 2000, nullptr, 0, 0, 1500,     nullptr, nullptr},
  // Calibration ends. The bounds deliberately CANNOT cross -- min tops out
  // where max starts -- because core/ has no cross-parameter constraint
  // mechanism: ParamDef bounds are static and setNum validates one value in
  // isolation. Same trick motor0's own copy of this table uses.
  {"motor1.min_us",       ParamType::U8,   "Min",      "µs",    500,  1500, nullptr, 0, 0, 1000,     nullptr, nullptr},
  {"motor1.max_us",       ParamType::U8,   "Max",      "µs",    1500, 2500, nullptr, 0, 0, 2000,     nullptr, nullptr},
  // Only used when motor1.mode == input, but shown only when motor1.mode == off
  // -- the opposite of every other showIf in this codebase, deliberately:
  // this is a pre-arm configuration choice, not a live control, so it is
  // hidden once armed/input is live rather than left sitting in view where
  // an accidental change is easy to make. Terminal `set` and INI restore
  // still accept it regardless of mode (showIf is display-only, never an
  // access rule). Defaults to ch1, the conventional throttle channel.
  {"motor1.src",          ParamType::Enum, "Source",   nullptr, 0, 0, kSrcNames, 14, 0, 13, nullptr, nullptr, "motor1.mode", "off"},
  // Brushed-only, shown only for type=brushed.
  {"motor1.freq",         ParamType::U8,   "Switch Freq", "Hz", 1000, 50000, nullptr, 0, 0, 20000, nullptr, "H-Bridge", "motor1.type", "brushed"},
  {"motor1.invert",       ParamType::Enum, "Invert",   nullptr, 0, 0, kInvertOpts, 2, 0, 0, nullptr, "H-Bridge", "motor1.type", "brushed"},
  {"motor1.brake",        ParamType::Enum, "At Zero",  nullptr, 0, 0, kBrakeOpts, 2, 0, 0, nullptr, "H-Bridge", "motor1.type", "brushed"},
};

// The commanded pulse width, or 0 when off -- including neutralUs during the
// arm-hold window, same "commanded, not measured" honesty servo's srv field
// has. There is no RPM/current feedback on this wiring.
//
// arm is a plain number (motor::ARM_OFF/ARM_ARMING/ARM_ARMED), not a string or
// a dedicated enum-tlm type -- core/ has neither.
static const TlmDef kTlm[T_COUNT] = {
  // key     label     unit  type          div  dec  fmt      group
  {"motor1",  "Motor 1",  "µs",    TlmType::U32,  0,   0,  nullptr, nullptr},
  {"arm1",  "Armed",  nullptr, TlmType::U32,  0,   0,  nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "motor1", "Motor 1",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

}  // namespace motor1
