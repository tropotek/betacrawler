#include "features/led/led_params.h"

namespace led {

using core::ParamDef;
using core::ParamType;

// Order must match the MODE_* constants in led_params.h -- the wire carries
// the name, the driver receives the index.
static const char* const kModes[] = {"off", "on", "blink", "fade"};

static const ParamDef kParams[] = {
  // key            type             label       unit  min max opts    n  maxlen def defStr group
  {"led.mode",      ParamType::Enum, "LED Mode", nullptr, 0, 0, kModes, 4, 0, MODE_BLINK, nullptr, nullptr},
  // Shared by blink and fade: cycles per second in both cases. Labelled
  // "Rate" rather than "Blink Rate" because it drives the breathing speed in
  // fade mode too.
  {"led.blink_hz",  ParamType::U8,   "Rate",     "Hz",  1, 20, nullptr, 0, 0, 2, nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "led", "LED",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  nullptr, 0,     // the LED reports no telemetry -- its state is a parameter
};

}  // namespace led
