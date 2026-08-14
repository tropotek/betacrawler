#pragma once
#include <stdint.h>
#include "config.h"

// Firmware health on the onboard LED, driven straight from main.cpp rather
// than through the registry: it must keep running when the registry is the
// thing that failed. Compiles to nothing when FEATURE_STATUS_LED is 0.

namespace status_led {

class StatusLed {
 public:
  void begin();
  void tick(uint32_t nowMs);

 private:
  bool on_ = false;
};

}  // namespace status_led
