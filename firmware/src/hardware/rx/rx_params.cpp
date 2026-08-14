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

// ExpressLRS's rf_mode index does NOT match the CRSF/TBS convention of "the
// index IS the profile" -- measured 2026-08-01 against a real receiver on
// ELRS 3.3.1 (ISM2G4, "e051b8"), EdgeTX handset: the wire byte was read raw
// (this function temporarily returned idx unmodified) while stepping through
// EVERY packet-rate option the TX offered, lowest to highest, and
// cross-checking each against the driver's OWN independently-measured `rate`
// telemetry (frame arrival counting, not this table). Every index the TX
// actually offered is a real reading -- 2, 4-5, 7-13 -- covering the whole
// list end to end (50Hz through F1000). Indices 0, 1, 3 and 6 never appeared
// in that list; rather than guess what they might mean (a different region's
// rate, a removed legacy entry, ...), they stay 0, the same policy as the
// out-of-range default below. D250/D500 (10/11) are ELRS's own
// link-adaptive modes; the value stored is their nominal ceiling rate, not a
// live figure -- `rate` is where the actual live throughput lives.
// kProtocols (below) is a plain value table compiled in every environment --
// native included, always against blackpill_f411ce.h, which defines this --
// and by every Arduino env regardless of FEATURE_RX, since this file (unlike
// rx_driver.cpp) carries no #if FEATURE_RX guard: descriptor TUs are meant to
// stay hardware-agnostic. rx_driver.cpp's own #ifndef RX_BAUD check is what
// actually enforces "the board header must define this when FEATURE_RX is
// on"; this fallback only covers a board with no receiver wired up at all
// (e.g. esp32_wroom32, FEATURE_RX off), where the value is never read by any
// running code.
#ifndef RX_BAUD
#define RX_BAUD 420000
#endif

static uint16_t elrsRfRateHz(uint8_t idx) {
  static const uint16_t kHz[] = {
    0,     // 0 -- not offered by this TX/RX pairing, unconfirmed
    0,     // 1 -- not offered by this TX/RX pairing, unconfirmed
    50,    // 2 -- 50Hz
    0,     // 3 -- not offered by this TX/RX pairing, unconfirmed
    100,   // 4 -- 100Hz Full
    150,   // 5 -- 150Hz
    0,     // 6 -- not offered by this TX/RX pairing, unconfirmed
    250,   // 7 -- 250Hz
    333,   // 8 -- 333Hz Full
    500,   // 9 -- 500Hz
    250,   // 10 -- D250 (dynamic, nominal ceiling)
    500,   // 11 -- D500 (dynamic, nominal ceiling)
    500,   // 12 -- F500 (FLRC)
    1000,  // 13 -- F1000 (FLRC), the fastest/last option this TX offers
  };
  return (idx < sizeof(kHz) / sizeof(kHz[0])) ? kHz[idx] : 0;
}

const Protocol kProtocols[] = {
  // name         baud     ch  timeoutParam         rfRateHz
  {"crossfire",   RX_BAUD, 12, P_CROSSFIRE_TIMEOUT, crossfireRfRateHz},
  {"elrs",        RX_BAUD, 16, P_ELRS_TIMEOUT,      elrsRfRateHz},
};

// The three statements of protocol identity -- kProtocolNames (what the wire
// and UI see), kProtocols (what the driver indexes) and kProtocolCount (a
// literal in rx_params.h, a different translation unit from both arrays it
// counts) -- are the same fact stated three times. These asserts are what
// stops them drifting silently: without them, bumping kProtocolCount while
// forgetting a row or a name compiles clean, and dispatch.cpp's
// d.optionCount-bounded loop over kProtocolNames reads a const char* past
// the end of the array.
static_assert(sizeof(kProtocols) / sizeof(kProtocols[0]) == kProtocolCount,
              "kProtocols and kProtocolCount disagree");
static_assert(sizeof(kProtocolNames) / sizeof(kProtocolNames[0]) == kProtocolCount,
              "kProtocolNames and kProtocolCount disagree");

static const ParamDef kParams[] = {
  // key                    type             label       unit  min   max   opts             n               maxlen def              defStr group        showIfKey      showIfVal
  {"rx.protocol",           ParamType::Enum, "Protocol", nullptr, 0,    0,    kProtocolNames, kProtocolCount, 0, PROTO_ELRS,      nullptr, nullptr,     nullptr,       nullptr},
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
// The wire carries microseconds, and phase 2's mapping (servo.src etc., see
// _notes/spec-rx-mapping.md) reuses this exact converted value rather than
// the parser's raw ticks -- ticksToUs's ~1.6x quantization is well beyond
// any hobby servo's real resolution, and reusing it means the bus and this
// telemetry can never disagree about the same frame.
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
