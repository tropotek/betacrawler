#pragma once
#include "core/module.h"

namespace led {

extern const core::ModuleDesc kDesc;

// Parameter indices *within this module* -- what onParamChanged() receives.
// Local, so nothing outside led/ depends on where these landed in the global
// table, and adding a module elsewhere can never shift them.
enum : uint8_t { P_MODE = 0, P_RATE = 1 };

// Values of the led.mode enum, in declaration order.
enum : int32_t { MODE_OFF = 0, MODE_ON = 1, MODE_BLINK = 2, MODE_FADE = 3 };

}  // namespace led
