#pragma once
#include "hardware/button/button_params.h"

namespace button {

// Requires BUTTON_PIN from the board header.
class ButtonDriver : public core::Module {
 public:
  void begin() override;
  void readTelemetry(core::TlmValue* out) override;

 private:
  // Sampled at boot rather than assumed, so the same driver works on a board
  // that wires the button active-high.
  bool idleLevel_ = true;
};

}  // namespace button
