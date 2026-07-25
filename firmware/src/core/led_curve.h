#pragma once
#include <stdint.h>

namespace core {

// Symmetric triangle wave, 0-100 (%). Ramps 0->100 across the first half of
// periodMs, then 100->0 across the second half. phaseMs wraps modulo
// periodMs internally. Returns 0 if periodMs leaves no room for a ramp.
uint8_t breathingDuty(uint32_t phaseMs, uint32_t periodMs);

}  // namespace core
