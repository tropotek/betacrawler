#include "hardware/crsf/crsf_params.h"

namespace crsf {

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
  {"crsf.source",     ParamType::Enum, "Source",  nullptr, 0,   0,    kSources, 2, 0, SRC_UART, nullptr, nullptr},
  // TBS's own guidance is to wait ~1s before acting on a lost link, because
  // there is no "lost" signal -- only the absence of frames.
  {"crsf.timeout_ms", ParamType::U8,   "Timeout", "ms",    100, 2000, nullptr,  0, 0, 1000,     nullptr, nullptr},
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
  "crsf", "CRSF",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  kTlm, T_COUNT,
};

// Bitwise rather than a 256-byte lookup table. At 150 frames/s a 26-byte
// frame costs ~200 shift iterations, i.e. ~31k operations per second on a
// 100MHz part -- immeasurable, and it saves a table of magic numbers no
// reviewer can check by eye.
uint8_t crc8(const uint8_t* data, size_t n) {
  uint8_t crc = 0;
  for (size_t i = 0; i < n; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
  }
  return crc;
}

uint16_t ticksToUs(uint16_t ticks) {
  // us = 1500 + 5/8 * (ticks - 992), rounded to nearest with ties going up.
  //
  // The rounding is load-bearing, not incidental. C's division truncates
  // toward zero, which rounds the two halves of the range in opposite
  // directions and puts the channel minimum at 987 instead of the published
  // 988. Adding 4 and flooring rounds consistently toward +infinity, which is
  // what makes 172 -> 988 and 1811 -> 2012 both come out exactly.
  const int32_t d = (int32_t)ticks - 992;
  const int32_t n = 5 * d + 4;
  const int32_t q = (n >= 0) ? (n / 8) : -(((-n) + 7) / 8);
  return (uint16_t)(1500 + q);
}

void unpackChannels(const uint8_t* payload, uint16_t* out) {
  // 11-bit fields, LSB-first, little endian. `acc` never holds more than
  // 10 + 8 = 18 bits, so 32 is ample.
  uint32_t acc = 0;
  uint8_t  bits = 0;
  uint8_t  ch = 0;
  for (uint8_t i = 0; i < kRcPayloadLen; ++i) {
    acc |= (uint32_t)payload[i] << bits;
    bits = (uint8_t)(bits + 8);
    while (bits >= 11 && ch < kWireChannels) {
      out[ch++] = (uint16_t)(acc & 0x7FF);
      acc >>= 11;
      bits = (uint8_t)(bits - 11);
    }
  }
}

void decodeLinkStats(const uint8_t* payload, LinkStats* out) {
  out->antenna = payload[4];
  // Both antennas are always reported; the receiver says which one it is
  // actually using. A non-diversity Nano RX reports 0 and leaves ant2 unset.
  const uint8_t mag = (payload[4] == 1) ? payload[1] : payload[0];
  out->rssiDbm = (int16_t)(-(int16_t)mag);
  out->lq      = payload[2];
  out->snr     = (int8_t)payload[3];
}

FrameParser::Result FrameParser::feed(uint8_t b) {
  switch (st_) {
    case State::SeekSync:
      if (b == kSync) st_ = State::Length;
      return Result::None;

    case State::Length:
      if (b < kMinLen || b > kMaxLen) {
        st_ = State::SeekSync;
        return Result::Rejected;
      }
      len_ = b;
      got_ = 0;
      st_ = State::Payload;
      return Result::None;

    case State::Payload:
      if (got_ < (uint8_t)(len_ - 1)) {
        buf_[got_++] = b;
        return Result::None;
      }
      // This byte is the CRC, over everything accumulated.
      st_ = State::SeekSync;
      if (crc8(buf_, (size_t)(len_ - 1)) != b) return Result::Rejected;
      type_ = buf_[0];
      return Result::Frame;
  }
  return Result::None;
}

void LinkState::onFrame(uint32_t nowMs) {
  if (!seen_) {
    seen_ = true;
    winStart_ = nowMs;
  }
  lastMs_ = nowMs;
  up_ = true;
  ++winCount_;
}

void LinkState::tick(uint32_t nowMs, uint32_t timeoutMs) {
  // Nothing has ever arrived: stay down and count nothing. Silence on an
  // unwired port is not a timeout.
  if (!seen_) return;

  // Unsigned subtraction, deliberately: it is correct across the millis()
  // wraparound at ~49.7 days, where a "now < last" comparison would declare a
  // permanent link loss.
  if ((uint32_t)(nowMs - lastMs_) > timeoutMs) {
    if (up_) {
      up_ = false;
      // Zero the rate with the link rather than at the end of the window --
      // "link 0, rate 150" would be untrue for up to a second.
      rate_ = 0;
      winCount_ = 0;
      winStart_ = nowMs;
    }
    return;
  }

  if ((uint32_t)(nowMs - winStart_) >= kWindowMs) {
    rate_ = winCount_;
    winCount_ = 0;
    winStart_ = nowMs;
  }
}

void LinkState::reset() {
  seen_     = false;
  up_       = false;
  rate_     = 0;
  winCount_ = 0;
  lastMs_   = 0;
  winStart_ = 0;
  // err_ deliberately untouched.
}

}  // namespace crsf
