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

 private:
  int16_t vals_[kInputCount] = {};
};

}  // namespace core
