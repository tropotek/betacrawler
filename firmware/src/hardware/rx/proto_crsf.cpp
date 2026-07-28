#include "hardware/rx/proto_crsf.h"

namespace rx {

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

uint16_t txPowerMw(uint8_t idx) {
  static const uint16_t kMw[] = {0, 10, 25, 100, 500, 1000, 2000, 250, 50};
  return (idx < sizeof(kMw) / sizeof(kMw[0])) ? kMw[idx] : 0;
}

void decodeLinkStats(const uint8_t* payload, LinkStats* out) {
  out->antenna = payload[4];
  // Both antennas are always reported; the receiver says which one it is
  // actually using. A non-diversity Nano RX reports 0 and leaves ant2 unset.
  const uint8_t mag = (payload[4] == 1) ? payload[1] : payload[0];
  out->rssiDbm = (int16_t)(-(int16_t)mag);
  out->lq      = payload[2];
  out->snr     = (int8_t)payload[3];
  out->rfMode  = payload[5];
  out->txPower = payload[6];
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
  } else if (!up_) {
    // Coming back from a timeout. The window still open is the one the drop
    // froze, and it is as old as the outage was long -- so the next tick()
    // would find it overdue, close it immediately, and publish a count
    // spanning only the moment since frames resumed. Measured on real
    // hardware after a 10s dropout: "rate 1" beside "rfrate 150", for about
    // a second. Start a fresh window instead and leave rate_ at the 0 the
    // drop set, which is what "not measured yet" honestly looks like -- the
    // same reasoning that zeroes the rate with the link rather than at the
    // end of the window below. Counting from 0 here rather than discarding
    // this frame keeps THIS one, the frame that carried the link back up,
    // inside the window it starts.
    winStart_ = nowMs;
    winCount_ = 0;
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

}  // namespace rx
