#pragma once
#include <stdint.h>

namespace core {

// How fast the main loop is actually running, and the longest single pass in
// the last window. A singleton for the same reason Health is one: exactly one
// destination, and no plumbing through every module.
//
// mark() takes the timestamp rather than reading the clock itself, so the
// whole thing is pure and native-testable -- the same bargain Module::tick()
// already makes with nowMs. Microseconds, not milliseconds: at tens of kHz a
// millisecond-resolution worst-pass reads 0 and measures nothing.
class LoopStats {
 public:
  static constexpr uint32_t kWindowUs = 1000000;

  void mark(uint32_t nowUs);

  // The last COMPLETED window's figures, so they hold steady while the next
  // one fills. Both 0 until the first window closes.
  uint32_t hz() const { return hz_; }
  uint32_t worstUs() const { return worstUs_; }

  void reset();

 private:
  bool     started_ = false;
  uint32_t windowStartUs_ = 0;
  uint32_t lastUs_ = 0;
  uint32_t count_ = 0;
  uint32_t worstInWindowUs_ = 0;
  uint32_t hz_ = 0;
  uint32_t worstUs_ = 0;
};

LoopStats& loopStats();

}  // namespace core
