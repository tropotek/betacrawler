#pragma once
#include "core/module.h"

namespace crsf {

// --- wire constants ---------------------------------------------------------
// From the TBS CRSF specification. A frame is
//   [sync] [len] [type] [payload...] [crc8]
// where `len` counts type + payload + crc, i.e. it EXCLUDES the two header
// bytes. Max frame is 64 bytes including sync and crc, so len is 2..62.
constexpr uint8_t kSync           = 0xC8;   // frames addressed to the FC
constexpr uint8_t kTypeRcChannels = 0x16;
constexpr uint8_t kTypeLinkStats  = 0x14;
constexpr uint8_t kRcPayloadLen   = 22;     // 16 channels x 11 bits
constexpr uint8_t kLinkPayloadLen = 10;
constexpr uint8_t kMinLen         = 2;
constexpr uint8_t kMaxLen         = 62;
constexpr uint8_t kMaxTypePayload = kMaxLen - 1;   // what the CRC covers

// The frame always carries 16 slots. Crossfire transmits 12 channels over the
// air, so on a Nano RX the last four are padding and this module publishes
// only kUsedChannels. An ExpressLRS fork raises this to kWireChannels.
constexpr uint8_t kWireChannels = 16;
constexpr uint8_t kUsedChannels = 12;

// CRC-8, polynomial 0xD5 (DVB-S2), init 0, no reflection, no final xor.
// Covers type + payload only -- not the sync byte and not the length byte.
uint8_t crc8(const uint8_t* data, size_t n);

// CRSF tick -> microseconds. Deliberately does NOT clamp: a receiver may
// legally send outside 172..1811 and the firmware reports what arrived. The
// browser clamps its own bar drawing instead.
uint16_t ticksToUs(uint16_t ticks);

// 22 payload bytes -> 16 channels. `out` must have kWireChannels entries.
void unpackChannels(const uint8_t* payload, uint16_t* out);

struct LinkStats {
  uint8_t lq;        // uplink link quality, %
  int8_t  snr;       // uplink SNR, dB
  int16_t rssiDbm;   // uplink RSSI of the ACTIVE antenna, real (negative) dBm
  uint8_t antenna;   // which antenna the receiver reports as active
};

// 10 payload bytes of a 0x14 frame.
void decodeLinkStats(const uint8_t* payload, LinkStats* out);

// Byte-at-a-time framing. Fed one byte per call so the driver never needs a
// buffer of its own and a frame split across UART reads costs nothing.
class FrameParser {
 public:
  enum class Result : uint8_t { None, Frame, Rejected };

  Result feed(uint8_t b);

  uint8_t        type() const { return type_; }
  const uint8_t* payload() const { return buf_ + 1; }   // buf_[0] is the type
  uint8_t        payloadLen() const { return (uint8_t)(len_ - 2); }

 private:
  enum class State : uint8_t { SeekSync, Length, Payload };

  State   st_   = State::SeekSync;
  uint8_t len_  = 0;    // as read from the wire
  uint8_t got_  = 0;    // type+payload bytes accumulated so far
  uint8_t type_ = 0;
  uint8_t buf_[kMaxTypePayload] = {};
};

}  // namespace crsf
