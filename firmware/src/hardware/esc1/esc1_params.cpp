#include "hardware/esc1/esc1_params.h"

namespace esc1 {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

// Order must match esc::MODE_* -- the wire carries the name, the driver
// receives the index.
static const char* const kModes[] = {"off", "armed", "input"};

// Order must match core::Inputs' slot indices directly -- "ch1" is slot 0 --
// same convention esc0.src and servo.src use, named to match rx's own
// ch1..ch16 telemetry naming.
static const char* const kSrcNames[] = {
  "ch1", "ch2", "ch3", "ch4", "ch5", "ch6",
  "ch7", "ch8", "ch9", "ch10", "ch11", "ch12",
  // Indices 12/13: tank_drive's own bus (see core::Registry::driveOutputs()),
  // not a raw rx channel. esc1 doesn't otherwise know tank_drive exists --
  // this is the one place that convention is spelled out.
  "drive_left", "drive_right",
};

static const char* const kDirections[] = {"unidirectional", "bidirectional"};

// Per-field reasoning: see esc0_params.cpp -- identical here, just esc1's keys.
static const ParamDef kParams[] = {
  // key                type             label       unit  min   max   opts     n  maxlen def       defStr group
  {"esc1.direction",    ParamType::Enum, "Direction", nullptr, 0, 0, kDirections, 2, 0, esc::DIR_UNIDIRECTIONAL, nullptr, nullptr},
  {"esc1.mode",         ParamType::Enum, "ESC",      nullptr, 0,    0,    kModes, 3, 0, esc::MODE_OFF, nullptr, nullptr},
  {"esc1.throttle_us",  ParamType::U8,   "Throttle", "µs",    1000, 2000, nullptr, 0, 0, 1000,     nullptr, nullptr},
  {"esc1.min_us",       ParamType::U8,   "Min",      "µs",    500,  1500, nullptr, 0, 0, 1000,     nullptr, nullptr},
  {"esc1.max_us",       ParamType::U8,   "Max",      "µs",    1500, 2500, nullptr, 0, 0, 2000,     nullptr, nullptr},
  {"esc1.src",          ParamType::Enum, "Source",   nullptr, 0, 0, kSrcNames, 14, 0, 0, nullptr, nullptr, "esc1.mode", "off"},
};

static const TlmDef kTlm[T_COUNT] = {
  // key     label     unit  type          div  dec  fmt      group
  {"esc1",  "ESC 1",  "µs",    TlmType::U32,  0,   0,  nullptr, nullptr},
  {"arm1",  "Armed",  nullptr, TlmType::U32,  0,   0,  nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "esc1", "ESC 1",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

}  // namespace esc1
