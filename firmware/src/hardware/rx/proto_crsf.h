#pragma once
// The CRSF wire protocol, split out of the module descriptor so a second
// protocol's framing has somewhere to go that is not rx_params.cpp. Pure:
// zero Arduino includes, because the native build compiles this.
//
// ExpressLRS speaks these same frames at this same baud -- an ELRS receiver
// talks CRSF to the flight controller. This file is therefore shared by both
// protocol table entries, not duplicated per protocol.
#include <stdint.h>
#include <stddef.h>

namespace rx {

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

// The frame always carries 16 slots. How many of them are MEANINGFUL is a
// property of the protocol, not of the frame: Crossfire transmits 12 and
// pads the rest, ELRS transmits 16. That count lives in Protocol::channels;
// this constant is the buffer size and the descriptor's field count.
constexpr uint8_t kWireChannels = 16;

// CRC-8, polynomial 0xD5 (DVB-S2), init 0, no reflection, no final xor.
// Covers type + payload only -- not the sync byte and not the length byte.
uint8_t crc8(const uint8_t* data, size_t n);

// CRSF tick -> microseconds. Deliberately does NOT clamp: a receiver may
// legally send outside 172..1811 and the firmware reports what arrived. The
// browser clamps its own bar drawing instead.
uint16_t ticksToUs(uint16_t ticks);

// Snaps a channel reading to exactly centerUs when it's within deadbandUs of
// center, otherwise passes it through unchanged. Duplicated from
// features/tank_drive/tank_drive_math.cpp's own deadbanded() rather than
// shared -- same self-containment precedent this module's other small pure
// helpers already follow.
int16_t deadbanded(int16_t us, int16_t centerUs, uint16_t deadbandUs);

// 22 payload bytes -> 16 channels. `out` must have kWireChannels entries.
void unpackChannels(const uint8_t* payload, uint16_t* out);

struct LinkStats {
  uint8_t lq;        // uplink link quality, %
  int8_t  snr;       // uplink SNR, dB
  int16_t rssiDbm;   // uplink RSSI of the ACTIVE antenna, real (negative) dBm
  uint8_t antenna;   // which antenna the receiver reports as active
  // Raw indices, decoded by the caller: the RF-mode numbering is the
  // protocol's own, so this layer must not pretend to know which protocol
  // sent the frame.
  uint8_t rfMode;
  uint8_t txPower;
};

// 10 payload bytes of a 0x14 frame.
void decodeLinkStats(const uint8_t* payload, LinkStats* out);

// TX power index -> milliwatts, from the CRSF specification. Shared by every
// protocol in this family: TBS and ExpressLRS both use CRSF's numbering here,
// unlike the RF-mode index, which each of them numbers its own way.
// Returns 0 for an index the table does not cover -- "unknown", not a guess.
uint16_t txPowerMw(uint8_t idx);

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

// Link health derived from the arrival of frames rather than from any field
// in them. This is forced by the protocol: with failsafe type "cut" a
// receiver simply STOPS sending when the link drops, so there is no bit on
// the wire that says "lost" and a timeout is the only signal there is.
//
// Clock-injected on purpose -- every branch here is testable natively against
// a fake nowMs, which is exactly the class of bug (timing, boot ordering) the
// native suites are otherwise blind to.
class LinkState {
 public:
  void onFrame(uint32_t nowMs);
  void onReject() { ++err_; }
  void tick(uint32_t nowMs, uint32_t timeoutMs);

  // Returns the link to its just-booted state: down, rate 0, no frame ever
  // seen. Deliberately does NOT touch err_ -- rejections stay countable
  // across a source change, exactly as they do across a timeout. Used by
  // rx.source changing away from sim, so a board with nothing wired to
  // uart cannot keep reporting sim's last "link up, rate ~143" after the
  // switch.
  void reset();

  bool     up() const     { return up_; }
  uint32_t rate() const   { return rate_; }
  uint32_t errors() const { return err_; }

 private:
  static const uint32_t kWindowMs = 1000;

  uint32_t lastMs_   = 0;
  uint32_t winStart_ = 0;
  uint32_t winCount_ = 0;
  uint32_t rate_     = 0;
  uint32_t err_      = 0;
  bool     seen_     = false;
  bool     up_       = false;
};

}  // namespace rx
