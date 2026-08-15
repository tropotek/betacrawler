#pragma once
#include "hardware/esc0/esc0_params.h"
#include "core/inputs.h"

// Forward-declared rather than including <HardwareTimer.h>: this header is
// pulled in by modules.cpp, and the Arduino timer header is heavy.
class HardwareTimer;

namespace esc0 {

// Requires ESC0_PIN and ESC0_TIMER from the board header.
class EscDriver : public core::Module {
 public:
  void attach(const core::Registry& reg, const core::Params& p) override;
  void begin() override;
  void tick(uint32_t nowMs) override;
  void onParamChanged(uint8_t local, const core::Params& p) override;
  void readTelemetry(core::TlmValue* out) override;

 private:
  void apply(const core::Params& p);
  void attachOutput();
  void detach();
  void writeUs(uint16_t us);
  void setFrameUs(uint32_t frameUs);

  const core::Inputs* inputs_ = nullptr;
  const core::Inputs* driveInputs_ = nullptr;
  HardwareTimer* timer_ = nullptr;
  uint32_t ch_         = 0;
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
  uint32_t frameUs_    = 0;
};

}  // namespace esc0
