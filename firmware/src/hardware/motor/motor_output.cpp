#include "hardware/motor/motor_output.h"
#include <Arduino.h>
#include <HardwareTimer.h>

namespace motor {

void EscOutput::begin() {
  ch_ = resolveChannel(timer_, pin_);
}

void EscOutput::attachOutput() {
  if (!ch_) return;
  timer_->setMode(ch_, TIMER_OUTPUT_COMPARE_PWM1, pin_);
  timer_->resumeChannel(ch_);
}

void EscOutput::detach() {
  if (!ch_) return;
  // A real detach, not a zero-width pulse: no pulse train at all while off.
  timer_->pauseChannel(ch_);
  // STM32 PWM channels have the OCxPE preload bit set, so setCaptureCompare()
  // only ever writes a shadow register -- the counter keeps whatever value
  // was active until an update event loads the shadow in. Zeroing here (not
  // just pausing) means a later attachOutput()+write() cannot emit one stale
  // pre-detach pulse before the new value's own update event lands.
  timer_->setCaptureCompare(ch_, 0, MICROSEC_COMPARE_FORMAT);
  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
}

void EscOutput::setPeriodUs(uint32_t periodUs) {
  timer_->setOverflow(periodUs, MICROSEC_FORMAT);
}

void EscOutput::write(uint16_t us, uint16_t, uint16_t, uint16_t) {
  if (!ch_) return;
  timer_->setCaptureCompare(ch_, us, MICROSEC_COMPARE_FORMAT);
}

}  // namespace motor
