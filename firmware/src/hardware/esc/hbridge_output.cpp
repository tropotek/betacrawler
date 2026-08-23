#include "hardware/esc/hbridge_output.h"
#include "hardware/esc/hbridge_math.h"
#include <Arduino.h>
#include <HardwareTimer.h>

namespace esc {

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
  periodUs_ = periodUs;
  timer_->setOverflow(periodUs_, MICROSEC_FORMAT);
}

void HbridgeOutput::write(uint16_t us, uint16_t minUs, uint16_t maxUs, uint16_t neutralUs) {
  if (!chA_ || !chB_) return;
  const int16_t duty = signedDutyPermille(us, minUs, maxUs, neutralUs);
  const PinDuty pd = splitPinDuty(duty, inverted_, brakeOnZero_);
  const uint32_t compareA = (uint32_t)periodUs_ * pd.a / 1000u;
  const uint32_t compareB = (uint32_t)periodUs_ * pd.b / 1000u;
  timer_->setCaptureCompare(chA_, compareA, MICROSEC_COMPARE_FORMAT);
  timer_->setCaptureCompare(chB_, compareB, MICROSEC_COMPARE_FORMAT);
}

}  // namespace esc
