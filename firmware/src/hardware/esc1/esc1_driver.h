#pragma once
#include "hardware/esc1/esc1_params.h"
#include "hardware/esc/output_stage.h"
#include "hardware/esc/esc_output.h"
#include "hardware/esc/hbridge_output.h"
#include "core/inputs.h"

// Forward-declared rather than including <HardwareTimer.h>: this header is
// pulled in by modules.cpp, and the Arduino timer header is heavy.
class HardwareTimer;

namespace esc1 {

// Requires ESC1_PIN/ESC1_TIMER, and ESC1_PIN_B for brushed mode, from the
// board header.
class EscDriver : public core::Module {
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
  esc::EscOutput*        escOut_     = nullptr;
  esc::HbridgeOutput*    hbridgeOut_ = nullptr;
  esc::OutputStage*      stage_      = nullptr;
  int32_t  type_       = esc::TYPE_BRUSHLESS;
  int32_t  mode_       = esc::MODE_OFF;
  uint16_t throttleUs_ = 1000;
  uint8_t  srcIdx_     = 0;
  uint16_t minUs_      = 1000;
  uint16_t maxUs_      = 2000;
  int32_t  direction_  = esc::DIR_UNIDIRECTIONAL;
  uint32_t armState_   = esc::ARM_OFF;
  uint32_t armT0_      = 0;
  uint16_t lastUs_     = 0;
  uint8_t  rateIdx_    = 0;
  uint32_t freqHz_     = 20000;
  bool     invert_     = false;
  bool     brake_      = false;
  uint32_t periodUs_   = 0;
};

}  // namespace esc1
