#pragma once
#include "hardware/esc/output_stage.h"

class HardwareTimer;

namespace esc {

// Two-pin duty-cycle output stage -- two PWM channels of the same shared
// timer esc<N>'s EscOutput would otherwise use alone, driven in the "one
// pin carries the duty, the other stays low/high" pattern a DRV8833-class
// H-bridge expects. Used for esc<N>.type == TYPE_BRUSHED.
class HbridgeOutput : public OutputStage {
 public:
  HbridgeOutput(HardwareTimer* timer, uint32_t pinA, uint32_t pinB)
      : timer_(timer), pinA_(pinA), pinB_(pinB) {}

  void begin() override;
  void attachOutput() override;
  void detach() override;
  void setPeriodUs(uint32_t periodUs) override;
  void write(uint16_t us, uint16_t minUs, uint16_t maxUs, uint16_t neutralUs) override;
  void setInverted(bool inverted) override { inverted_ = inverted; }
  void setBrakeOnZero(bool brakeOnZero) override { brakeOnZero_ = brakeOnZero; }

 private:
  HardwareTimer* timer_;
  uint32_t pinA_;
  uint32_t pinB_;
  uint32_t chA_ = 0;
  uint32_t chB_ = 0;
  uint32_t periodUs_ = 0;
  bool     inverted_ = false;
  bool     brakeOnZero_ = false;
};

}  // namespace esc
