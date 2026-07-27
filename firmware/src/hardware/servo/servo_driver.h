#pragma once
#include "hardware/servo/servo_params.h"

// Forward-declared rather than including <HardwareTimer.h>: this header is
// pulled in by modules.cpp, and the Arduino timer header is heavy.
class HardwareTimer;

namespace servo {

// Requires SERVO_TIMER and SERVO_PIN from the board header.
class ServoDriver : public core::Module {
 public:
  void begin() override;
  void tick(uint32_t nowMs) override;
  void onParamChanged(uint8_t local, const core::Params& p) override;
  void readTelemetry(core::TlmValue* out) override;

 private:
  void apply(const core::Params& p);
  void attachOutput();
  void detach();
  void writeUs(uint16_t us);

  HardwareTimer* timer_ = nullptr;
  uint32_t ch_       = 0;
  int32_t  mode_     = MODE_OFF;
  uint8_t  angle_    = 90;
  uint16_t minUs_    = 1000;
  uint16_t maxUs_    = 2000;
  uint32_t periodMs_ = 4000;
  uint32_t t0_       = 0;
  uint16_t lastUs_   = 0;
};

}  // namespace servo
