#include "core/led_pattern.h"

namespace core {

bool patternState(const Pattern& p, uint32_t elapsedMs) {
  if (!p.stepsMs || p.count == 0) return false;

  uint32_t total = 0;
  for (uint8_t i = 0; i < p.count; ++i) total += p.stepsMs[i];
  if (total == 0) return false;

  uint32_t phase = elapsedMs % total;
  for (uint8_t i = 0; i < p.count; ++i) {
    if (phase < p.stepsMs[i]) return (i % 2) == 0;
    phase -= p.stepsMs[i];
  }
  return false;
}

}  // namespace core
