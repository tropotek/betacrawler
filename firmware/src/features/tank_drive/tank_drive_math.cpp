#include "features/tank_drive/tank_drive_math.h"

namespace tank_drive {

int16_t deadbanded(int16_t us, int16_t centerUs, uint16_t deadbandUs) {
  int32_t d = (int32_t)us - centerUs;
  if (d < 0) d = -d;
  return (d <= (int32_t)deadbandUs) ? centerUs : us;
}

bool linkFresh(uint32_t lastFreshMs, uint32_t nowMs, uint32_t staleMs) {
  if (lastFreshMs == 0) return false;
  return (nowMs - lastFreshMs) < staleMs;
}

// Scale numerator over a denominator of 100, clamped to [0,100]: 100 means
// "offset already within bounds, no scaling needed." Used identically for
// both the left and right offset; the smaller of the two results is what
// actually gets applied to both, which is the proportional-clamp property.
static int32_t scaleNumForOffset(int32_t offset, int32_t minOffset, int32_t maxOffset) {
  if (offset > maxOffset && offset != 0) return (maxOffset * 100) / offset;
  if (offset < minOffset && offset != 0) return (minOffset * 100) / offset;
  return 100;
}

static uint16_t clampToRange(int32_t v, uint16_t minUs, uint16_t maxUs) {
  if (v < (int32_t)minUs) return minUs;
  if (v > (int32_t)maxUs) return maxUs;
  return (uint16_t)v;
}

MixResult mix(int16_t throttleUs, int16_t steerUs, int16_t centerUs,
              uint16_t minUs, uint16_t maxUs,
              uint8_t reverseRatioPct, uint16_t deadbandUs) {
  int32_t throttle = deadbanded(throttleUs, centerUs, deadbandUs);
  const int32_t steer = deadbanded(steerUs, centerUs, deadbandUs);

  if (throttle < centerUs) {
    throttle = centerUs + (throttle - centerUs) * (int32_t)reverseRatioPct / 100;
  }

  const int32_t steerOffset = steer - centerUs;
  int32_t leftOffset  = (throttle - centerUs) + steerOffset;
  int32_t rightOffset = (throttle - centerUs) - steerOffset;

  const int32_t maxOffset = (int32_t)maxUs - centerUs;
  const int32_t minOffset = (int32_t)minUs - centerUs;  // <= 0

  int32_t scaleNum = scaleNumForOffset(leftOffset, minOffset, maxOffset);
  const int32_t rightScaleNum = scaleNumForOffset(rightOffset, minOffset, maxOffset);
  if (rightScaleNum < scaleNum) scaleNum = rightScaleNum;
  if (scaleNum > 100) scaleNum = 100;
  if (scaleNum < 0) scaleNum = 0;

  leftOffset  = (leftOffset  * scaleNum) / 100;
  rightOffset = (rightOffset * scaleNum) / 100;

  MixResult r;
  r.leftUs  = clampToRange(centerUs + leftOffset, minUs, maxUs);
  r.rightUs = clampToRange(centerUs + rightOffset, minUs, maxUs);
  return r;
}

}  // namespace tank_drive
