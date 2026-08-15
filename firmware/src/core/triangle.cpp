#include "core/triangle.h"

namespace core {

uint8_t trianglePercent(uint32_t phaseMs, uint32_t periodMs) {
  uint32_t half = periodMs / 2;
  if (half == 0) return 0;
  uint32_t t = phaseMs % periodMs;
  if (t < half) {
    return static_cast<uint8_t>((t * 100u) / half);
  }
  uint32_t down = t - half;
  uint32_t downSpan = periodMs - half;
  return static_cast<uint8_t>(100u - (down * 100u) / downSpan);
}

}  // namespace core
