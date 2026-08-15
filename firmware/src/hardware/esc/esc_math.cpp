#include "hardware/esc/esc_math.h"

namespace esc {

uint16_t clampUs(int32_t us, uint16_t minUs, uint16_t maxUs) {
  if (us < (int32_t)minUs) return minUs;
  if (us > (int32_t)maxUs) return maxUs;
  return (uint16_t)us;
}

uint16_t neutralUs(uint16_t minUs, uint16_t maxUs, bool bidirectional) {
  return bidirectional ? (uint16_t)(((uint32_t)minUs + maxUs) / 2) : minUs;
}

uint32_t nextArmState(uint32_t prevState, bool modeIsOff, bool enteringFromOff,
                       uint32_t nowMs, uint32_t armT0Ms, uint32_t armHoldMs,
                       bool commandedIsLow) {
  if (modeIsOff) return ARM_OFF;
  if (enteringFromOff) return ARM_ARMING;
  if (prevState == ARM_ARMING && commandedIsLow && (nowMs - armT0Ms) >= armHoldMs) {
    return ARM_ARMED;
  }
  return prevState;
}

bool isCommandedLow(int32_t mode, uint16_t throttleUs, int16_t inputUs, bool inputFresh,
                     uint16_t neutralUs, uint16_t lowMarginUs, bool bidirectional) {
  int32_t v;
  if (mode == MODE_ARMED) {
    v = throttleUs;
  } else if (mode == MODE_INPUT) {
    if (!inputFresh || inputUs <= 0) return false;
    v = inputUs;
  } else {
    return false;
  }
  if (bidirectional) {
    int32_t d = v - (int32_t)neutralUs;
    if (d < 0) d = -d;
    return d <= (int32_t)lowMarginUs;
  }
  return v <= (int32_t)neutralUs + (int32_t)lowMarginUs;
}

bool isLinkFresh(uint32_t lastFreshMs, uint32_t nowMs, uint32_t staleMs) {
  if (lastFreshMs == 0) return false;   // core::Inputs' own "never marked" default, not a real timestamp
  return (nowMs - lastFreshMs) < staleMs;
}

bool inputLossDemotesArmed(uint32_t armState, int32_t mode, bool inputFresh) {
  return armState == ARM_ARMED && mode == MODE_INPUT && !inputFresh;
}

bool srcChangeDemotesArmed(uint32_t armState, int32_t mode, bool srcChanged) {
  return armState == ARM_ARMED && mode == MODE_INPUT && srcChanged;
}

uint32_t frameUsForRate(uint8_t rateIdx) {
  static const uint32_t kFrameUs[] = {20000, 10000, 5000, 2500};
  if (rateIdx >= sizeof(kFrameUs) / sizeof(kFrameUs[0])) return kFrameUs[RATE_50];
  return kFrameUs[rateIdx];
}

uint16_t effectiveMaxUs(uint16_t maxUs, uint32_t frameUs) {
  if (frameUs <= kMinLowUs) return 0;
  const uint32_t room = frameUs - kMinLowUs;
  return (maxUs > room) ? (uint16_t)room : maxUs;
}

bool rateChangeDemotesArmed(uint32_t armState, bool rateChanged) {
  return armState == ARM_ARMED && rateChanged;
}

uint16_t nextPulseUs(uint32_t armState, int32_t mode, uint16_t minUs, uint16_t maxUs,
                      uint16_t throttleUs, int16_t inputUs, bool inputStale, uint16_t neutralUs) {
  if (armState != ARM_ARMED) return neutralUs;
  if (mode == MODE_ARMED) return clampUs(throttleUs, minUs, maxUs);
  if (mode == MODE_INPUT) {
    if (inputStale) return neutralUs;
    if (inputUs <= 0) return 0;
    return clampUs(inputUs, minUs, maxUs);
  }
  return 0;
}

}  // namespace esc
