#pragma once
#include "hardware/esc/output_stage.h"

class HardwareTimer;

namespace esc {

// Single-pin RC-pulse output stage -- one PWM channel on a shared timer,
// used for esc<N>.type == TYPE_BRUSHLESS. Owns no timer of its own: constructed
// with the pin its esc<N> instance already claims in the board header.
class EscOutput : public OutputStage {
 public:
  EscOutput(HardwareTimer* timer, uint32_t pin) : timer_(timer), pin_(pin) {}

  void begin() override;
  void attachOutput() override;
  void detach() override;
  void setPeriodUs(uint32_t periodUs) override;
  void write(uint16_t us, uint16_t minUs, uint16_t maxUs, uint16_t neutralUs) override;

 private:
  HardwareTimer* timer_;
  uint32_t pin_;
  uint32_t ch_ = 0;
};

}  // namespace esc
