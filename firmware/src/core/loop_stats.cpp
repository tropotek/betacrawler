#include "core/loop_stats.h"

namespace core {

void LoopStats::mark(uint32_t nowUs) {
  // The first mark has no interval before it -- it only establishes the
  // baseline the next one measures against.
  if (!started_) {
    started_ = true;
    windowStartUs_ = nowUs;
    lastUs_ = nowUs;
    return;
  }

  // Unsigned throughout, so the ~71-minute micros() wrap costs nothing.
  const uint32_t gap = nowUs - lastUs_;
  lastUs_ = nowUs;
  if (gap > worstInWindowUs_) worstInWindowUs_ = gap;
  ++count_;

  const uint32_t elapsed = nowUs - windowStartUs_;
  if (elapsed < kWindowUs) return;

  hz_ = (uint32_t)(((uint64_t)count_ * kWindowUs) / elapsed);
  worstUs_ = worstInWindowUs_;
  windowStartUs_ = nowUs;
  count_ = 0;
  worstInWindowUs_ = 0;
}

void LoopStats::reset() {
  *this = LoopStats{};
}

LoopStats& loopStats() {
  static LoopStats s;
  return s;
}

}  // namespace core
