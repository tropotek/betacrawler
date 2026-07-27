#include "hardware/servo/servo_params.h"
#include "core/led_curve.h"

namespace servo {

uint16_t angleToUs(uint8_t angle, uint16_t minUs, uint16_t maxUs) {
  if (angle > 180) angle = 180;
  int32_t span = (int32_t)maxUs - (int32_t)minUs;
  return (uint16_t)((int32_t)minUs + span * (int32_t)angle / 180);
}

uint8_t sweepAngle(uint32_t phaseMs, uint32_t periodMs) {
  // breathingDuty is already the symmetric triangle wave a sweep needs, and
  // it is already natively tested -- a second implementation here would be
  // pure duplication.
  return (uint8_t)((uint32_t)core::breathingDuty(phaseMs, periodMs) * 180u / 100u);
}

}  // namespace servo
