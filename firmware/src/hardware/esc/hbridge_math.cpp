#include "hardware/esc/hbridge_math.h"

namespace esc {

int16_t signedDutyPermille(uint16_t us, uint16_t minUs, uint16_t maxUs, uint16_t neutralUs) {
  if (us >= neutralUs) {
    const uint32_t span = (maxUs > neutralUs) ? (uint32_t)(maxUs - neutralUs) : 1u;
    uint32_t d = (uint32_t)(us - neutralUs) * 1000u / span;
    if (d > 1000u) d = 1000u;
    return (int16_t)d;
  }
  const uint32_t span = (neutralUs > minUs) ? (uint32_t)(neutralUs - minUs) : 1u;
  uint32_t d = (uint32_t)(neutralUs - us) * 1000u / span;
  if (d > 1000u) d = 1000u;
  return (int16_t)-(int32_t)d;
}

PinDuty splitPinDuty(int16_t duty, bool inverted, bool brakeOnZero) {
  if (duty == 0) {
    return brakeOnZero ? PinDuty{1000, 1000} : PinDuty{0, 0};
  }
  const uint16_t mag = (uint16_t)(duty > 0 ? duty : -duty);
  const bool forward = duty > 0;
  const bool aActive = forward != inverted;   // inverted flips which pin is "forward"
  return aActive ? PinDuty{mag, (uint16_t)0} : PinDuty{(uint16_t)0, mag};
}

}  // namespace esc
