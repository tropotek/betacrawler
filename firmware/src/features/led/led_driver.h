#pragma once
#include "features/led/led_params.h"

namespace led {

// Requires LED_PIN and LED_ACTIVE_LOW from the board header.
class LedDriver : public core::Module {
 public:
  void begin() override;
  void tick(uint32_t nowMs) override;
  void onParamChanged(uint8_t local, const core::Params& p) override;

 private:
  void apply(int32_t modeIdx, int32_t rateHz);
  void write(bool on);

  int32_t  mode_ = MODE_BLINK;
  int32_t  hz_ = 2;
  uint32_t lastToggle_ = 0;
  bool     on_ = false;
};

}  // namespace led
