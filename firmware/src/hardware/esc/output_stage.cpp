#include "hardware/esc/output_stage.h"
#include "core/boot_log.h"
#include <Arduino.h>
#include <HardwareTimer.h>
#include <stdio.h>

namespace esc {

uint32_t resolveChannel(HardwareTimer* timer, uint32_t pin) {
  const PinName name = digitalPinToPinName(pin);
  if (pinmap_peripheral(name, PinMap_TIM) != (void*)timer->getHandle()->Instance) {
    char msg[core::BootLog::kMaxLen];
    snprintf(msg, sizeof(msg), "P%c%u is not a channel of its own timer -- output disabled",
             (char)('A' + STM_PORT(name)), (unsigned)STM_PIN(name));
    core::bootLog().add(msg);
    return 0;
  }
  return STM_PIN_CHANNEL(pinmap_function(name, PinMap_TIM));
}

}  // namespace esc
