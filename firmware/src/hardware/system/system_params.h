#pragma once
#include "core/module.h"

namespace sys {

extern const core::ModuleDesc kDesc;

// Telemetry indices within this module -- the order readTelemetry() fills.
enum : uint8_t { T_UP = 0, T_CLK, T_RAM, T_TEMP, T_VDD, T_FAULT, T_COUNT };

}  // namespace sys
