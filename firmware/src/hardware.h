#pragma once
#include "core/dispatch.h"

namespace hw {

class LedDriver {
 public:
  void begin();
  void apply(int32_t modeIdx, int32_t blinkHz);  // 0=off 1=on 2=blink
  void tick(uint32_t nowMs);

 private:
  int32_t  mode_ = 2;
  int32_t  hz_ = 2;
  uint32_t lastToggle_ = 0;
  bool     on_ = false;
  void write(bool on);
};

void begin();
void readTelemetry(core::Telemetry* t);
uint8_t buttonPressed();

}  // namespace hw
