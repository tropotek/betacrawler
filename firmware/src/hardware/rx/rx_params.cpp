#include "hardware/rx/rx_params.h"

namespace rx {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

static const char* const kSources[] = {"uart", "sim"};

static const ParamDef kParams[] = {
  // key                type             label     unit  min  max   opts      n  maxlen def         defStr group
  // Defaults to uart, not sim: a board with no receiver wired must report a
  // link that is genuinely down rather than data that was invented. sim is
  // opt-in, and begin() says so in the boot log when it is on.
  {"rx.source",     ParamType::Enum, "Source",  nullptr, 0,   0,    kSources, 2, 0, SRC_UART, nullptr, nullptr},
  // TBS's own guidance is to wait ~1s before acting on a lost link, because
  // there is no "lost" signal -- only the absence of frames.
  {"rx.timeout_ms", ParamType::U8,   "Timeout", "ms",    100, 2000, nullptr,  0, 0, 1000,     nullptr, nullptr},
};

// Twelve channels in one group, link health in another: seventeen fields in a
// single card is unreadable, and TlmDef::group already exists for exactly
// this. `bar` plus lo/hi asks the browser to draw the proportion; a renderer
// that does not know the name falls back to the plain number, which is what
// the on-device panel does -- deliberately, it keeps its curated layout.
//
// The wire carries microseconds. That is a display choice, not a control one:
// phase 2's mapping reads the parser's raw ticks directly, so nothing here
// constrains control resolution.
static const char* const kChanGroup = "RC Channels";
static const char* const kLinkGroup = "RC Link";

static const TlmDef kTlm[T_COUNT] = {
  // key    label  unit  type          div dec fmt    group        lo    hi
  {"ch1",  "CH1",  "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch2",  "CH2",  "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch3",  "CH3",  "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch4",  "CH4",  "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch5",  "CH5",  "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch6",  "CH6",  "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch7",  "CH7",  "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch8",  "CH8",  "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch9",  "CH9",  "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch10", "CH10", "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch11", "CH11", "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch12", "CH12", "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  // 0/1, following btn's precedent that a boolean reading is just a number --
  // there is no string telemetry type and one field does not justify inventing
  // one.
  {"link", "Link", nullptr,     TlmType::U32, 0, 0, nullptr, kLinkGroup, 0, 0},
  {"lq",   "LQ",   "%",         TlmType::U32, 0, 0, nullptr, kLinkGroup, 0, 0},
  {"rssi", "RSSI", "dBm",       TlmType::I32, 0, 0, nullptr, kLinkGroup, 0, 0},
  {"rate", "Rate", "Hz",        TlmType::U32, 0, 0, nullptr, kLinkGroup, 0, 0},
  // Rejected frames: bad CRC and impossible lengths. This is the only way a
  // UART buffer overflow becomes visible -- an overflow tears the stream
  // mid-frame, which fails the CRC, so it needs no separate detection.
  {"err",  "Errors", nullptr,   TlmType::U32, 0, 0, nullptr, kLinkGroup, 0, 0},
};

const core::ModuleDesc kDesc = {
  "rx", "RX",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

}  // namespace rx
