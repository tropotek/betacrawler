#include "hardware/motor/hbridge_output.h"
#include "hardware/motor/hbridge_math.h"
#include <Arduino.h>
#include <HardwareTimer.h>

namespace motor {

void HbridgeOutput::begin() {
  chA_ = resolveChannel(timer_, pinA_);
  chB_ = resolveChannel(timer_, pinB_);
  // Two pins sharing one channel would leave the second write overwriting the
  // first and one pin driven by a peripheral this stage does not own.
  if (chA_ == chB_) { chA_ = 0; chB_ = 0; }
}

void HbridgeOutput::attachOutput() {
  if (!chA_ || !chB_) return;
  timer_->setMode(chA_, TIMER_OUTPUT_COMPARE_PWM1, pinA_);
  timer_->setMode(chB_, TIMER_OUTPUT_COMPARE_PWM1, pinB_);
  timer_->resumeChannel(chA_);
  timer_->resumeChannel(chB_);
}

void HbridgeOutput::detach() {
  if (chA_) {
    timer_->pauseChannel(chA_);
    timer_->setCaptureCompare(chA_, 0, MICROSEC_COMPARE_FORMAT);
  }
  if (chB_) {
    timer_->pauseChannel(chB_);
    timer_->setCaptureCompare(chB_, 0, MICROSEC_COMPARE_FORMAT);
  }
  pinMode(pinA_, OUTPUT);
  digitalWrite(pinA_, LOW);
  pinMode(pinB_, OUTPUT);
  digitalWrite(pinB_, LOW);
}

void HbridgeOutput::setPeriodUs(uint32_t periodUs) {
  timer_->setOverflow(periodUs, MICROSEC_FORMAT);
}

void HbridgeOutput::write(uint16_t us, uint16_t minUs, uint16_t maxUs, uint16_t neutralUs) {
  if (!chA_ || !chB_) return;
  const int16_t duty = signedDutyPermille(us, minUs, maxUs, neutralUs);
  const PinDuty pd = splitPinDuty(duty, inverted_, brakeOnZero_);
  // Ticks, not microseconds: a 20kHz period is 50us, so a microsecond compare
  // would quantise the duty to 2% (and to 5% at the 50kHz this param allows).
  // ARR+1 is the timer's own full scale, and is also what a permille of 1000
  // must reach for 100% -- the reference manual's "CCRx strictly greater than
  // ARR". ARR and CCRx are both preload registers that latch on the same
  // update event, so reading one to compute the other cannot tear.
  const uint32_t top = (uint32_t)__HAL_TIM_GET_AUTORELOAD(timer_->getHandle()) + 1u;
  timer_->setCaptureCompare(chA_, top * pd.a / 1000u, TICK_COMPARE_FORMAT);
  timer_->setCaptureCompare(chB_, top * pd.b / 1000u, TICK_COMPARE_FORMAT);
}

}  // namespace motor
