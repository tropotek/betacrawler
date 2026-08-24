#pragma once
#include "hardware/motor0/motor0_params.h"
#include "hardware/motor/output_stage.h"
#include "hardware/motor/motor_output.h"
#include "hardware/motor/hbridge_output.h"
#include "core/inputs.h"

// Forward-declared rather than including <HardwareTimer.h>: this header is
// pulled in by modules.cpp, and the Arduino timer header is heavy.
class HardwareTimer;

namespace motor0 {

// Requires MOTOR0_PIN/MOTOR0_TIMER, and MOTOR0_PIN_B for brushed mode, from the
// board header.
class MotorDriver : public core::Module {
 public:
  void attach(const core::Registry& reg, const core::Params& p) override;
  void begin() override;
  void tick(uint32_t nowMs) override;
  void onParamChanged(uint8_t local, const core::Params& p) override;
  void readTelemetry(core::TlmValue* out) override;

 private:
  void apply(const core::Params& p);

  const core::Inputs* inputs_ = nullptr;
  const core::Inputs* driveInputs_ = nullptr;
  HardwareTimer*        timer_      = nullptr;
  motor::MotorOutput*        escOut_     = nullptr;
  motor::HbridgeOutput*    hbridgeOut_ = nullptr;
  motor::OutputStage*      stage_      = nullptr;
  int32_t  type_       = motor::TYPE_BRUSHLESS;
  int32_t  mode_       = motor::MODE_OFF;
  uint16_t throttleUs_ = 1000;
  uint8_t  srcIdx_     = 0;
  uint16_t minUs_      = 1000;
  uint16_t maxUs_      = 2000;
  uint32_t armState_   = motor::ARM_OFF;
  uint32_t armT0_      = 0;
  uint16_t lastUs_     = 0;
  uint8_t  rateIdx_    = 0;
  uint32_t freqHz_     = 20000;
  bool     invert_     = false;
  bool     brake_      = false;
  uint32_t periodUs_   = 0;
};

}  // namespace motor0
