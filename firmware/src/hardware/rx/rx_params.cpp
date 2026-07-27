#include "hardware/rx/rx_params.h"
#include "config.h"

namespace rx {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

static const char* const kProtocolNames[] = {"crossfire", "elrs"};
static const char* const kSources[]       = {"uart", "sim"};

// TBS Crossfire profiles. Three rates, and the index IS the profile.
static uint16_t crossfireRfRateHz(uint8_t idx) {
  static const uint16_t kHz[] = {4, 50, 150};
  return (idx < sizeof(kHz) / sizeof(kHz[0])) ? kHz[idx] : 0;
}

// ExpressLRS numbers its air rates with its own enum, descending from the
// fastest. This is the classic 8-entry table; ELRS 3.x adds faster rates
// above it and has renumbered before, which is precisely why an index off the
// end returns 0 rather than the nearest neighbour. Check `rfrate` against
// what the handset reports before trusting this against a new ELRS release.
static uint16_t elrsRfRateHz(uint8_t idx) {
  static const uint16_t kHz[] = {500, 250, 200, 150, 100, 50, 25, 4};
  return (idx < sizeof(kHz) / sizeof(kHz[0])) ? kHz[idx] : 0;
}

const Protocol kProtocols[] = {
  // name         baud     ch  timeoutParam         rfRateHz
  {"crossfire",   RX_BAUD, 12, P_CROSSFIRE_TIMEOUT, crossfireRfRateHz},
  {"elrs",        RX_BAUD, 16, P_ELRS_TIMEOUT,      elrsRfRateHz},
};

static const ParamDef kParams[] = {
  // key                    type             label       unit  min   max   opts             n               maxlen def              defStr group        showIfKey      showIfVal
  {"rx.protocol",           ParamType::Enum, "Protocol", nullptr, 0,    0,    kProtocolNames, kProtocolCount, 0, PROTO_CROSSFIRE, nullptr, nullptr,     nullptr,       nullptr},
  // Defaults to uart, not sim: a board with no receiver wired must report a
  // link that is genuinely down rather than data that was invented. sim is
  // opt-in, and begin() says so in the boot log when it is on. Kept
  // orthogonal to rx.protocol so sim exercises either protocol.
  {"rx.source",             ParamType::Enum, "Source",   nullptr, 0,    0,    kSources,       2,              0, SRC_UART,        nullptr, nullptr,     nullptr,       nullptr},
  // TBS's own guidance is to wait ~1s before acting on a lost link, because
  // there is no "lost" signal -- only the absence of frames.
  {"crossfire.timeout_ms",  ParamType::U8,   "Timeout",  "ms",    100,  2000, nullptr,        0,              0, 1000,            nullptr, "Crossfire", "rx.protocol", "crossfire"},
  // Far shorter, because this module is a MONITOR: 200ms is 10 lost frames at
  // ELRS's slowest 50Hz and 100 at 500Hz. The industry's 1000ms is a
  // failsafe-ACTUATION figure and belongs to phase 2 on its own terms.
  {"elrs.timeout_ms",       ParamType::U8,   "Timeout",  "ms",    50,   2000, nullptr,        0,              0, 200,             nullptr, "ELRS",      "rx.protocol", "elrs"},
};

// Sixteen channels in one group, link health in another: twenty-one fields in
// a single card is unreadable, and TlmDef::group already exists for exactly
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
  {"ch13", "CH13", "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch14", "CH14", "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch15", "CH15", "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
  {"ch16", "CH16", "\xC2\xB5s", TlmType::U32, 0, 0, "bar", kChanGroup, 988, 2012},
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
  {"rfrate", "RF Rate",  "Hz", TlmType::U32, 0, 0, nullptr, kLinkGroup, 0, 0},
  {"pwr",    "TX Power", "mW", TlmType::U32, 0, 0, nullptr, kLinkGroup, 0, 0},
};

const core::ModuleDesc kDesc = {
  "rx", "RX",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

}  // namespace rx
