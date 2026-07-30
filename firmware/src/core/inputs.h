#pragma once
#include <stdint.h>

namespace core {

// A small bus of RC-style control signals, in microseconds, that lets one
// module (rx) publish values for others (servo, and eventually an ESC
// module) to read -- without either module naming the other. Full design in
// _notes/spec-rx-mapping.md.
//
// Bounds-checked rather than asserting: an out-of-range index is a caller
// bug, and this discards/returns a safe default rather than corrupting
// adjacent memory in a loop that also drives real hardware.
constexpr uint8_t kInputCount = 16;

class Inputs {
 public:
  int16_t get(uint8_t i) const { return (i < kInputCount) ? vals_[i] : 0; }
  void    set(uint8_t i, int16_t v) { if (i < kInputCount) vals_[i] = v; }

  // Stamps "a frame was just decoded and applied" -- called once per
  // accepted frame by rx (the bus's sole producer, same constructor-wired
  // exception as set() itself), never once per tick and never once per
  // channel. Bus-wide rather than per-channel on purpose: every channel of
  // one CRSF/ELRS frame updates together, so per-channel granularity would
  // track nothing per-channel actually differs on. A consumer measuring
  // (nowMs - lastFreshMs()) learns "how long since the link last proved
  // itself alive" -- a channel VALUE holding steady (a throttle at its
  // mechanical stop, say) does not by itself mean the link is down, which a
  // naive "has the value changed" check cannot tell apart from a stalled
  // link. Named for what it publishes, not for rx or any consuming module --
  // core/ never names a feature. See docs/architecture.md's "core::Inputs
  // bus" section and _notes/spec-esc.md's Revision section.
  void     markFresh(uint32_t nowMs) { freshMs_ = nowMs; }
  uint32_t lastFreshMs() const { return freshMs_; }

 private:
  int16_t  vals_[kInputCount] = {};
  uint32_t freshMs_ = 0;
};

}  // namespace core
