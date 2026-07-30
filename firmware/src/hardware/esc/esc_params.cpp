#include "hardware/esc/esc_params.h"

namespace esc {

uint16_t clampUs(int32_t us, uint16_t minUs, uint16_t maxUs) {
  if (us < (int32_t)minUs) return minUs;
  if (us > (int32_t)maxUs) return maxUs;
  return (uint16_t)us;
}

uint32_t nextArmState(uint32_t prevState, bool modeIsOff, bool enteringFromOff,
                       uint32_t nowMs, uint32_t armT0Ms, uint32_t armHoldMs) {
  if (modeIsOff) return ARM_OFF;
  if (enteringFromOff) return ARM_ARMING;
  if (prevState == ARM_ARMING && (nowMs - armT0Ms) >= armHoldMs) return ARM_ARMED;
  return prevState;
}

uint16_t nextPulseUs(uint32_t armState, int32_t mode, uint16_t minUs, uint16_t maxUs,
                      uint16_t throttleUs, int16_t inputUs) {
  if (armState != ARM_ARMED) return minUs;
  if (mode == MODE_ARMED) return clampUs(throttleUs, minUs, maxUs);
  if (mode == MODE_INPUT) {
    if (inputUs <= 0) return 0;
    return clampUs(inputUs, minUs, maxUs);
  }
  return 0;
}

}  // namespace esc
