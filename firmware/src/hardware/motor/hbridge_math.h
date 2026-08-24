#pragma once
#include <stdint.h>

namespace motor {

// Signed duty in permille (-1000..1000): 0 is stop, positive/negative pick
// direction. `us` must already be clamped into [minUs, maxUs] -- same
// contract esc_math.h's clampUs callers already follow.
int16_t signedDutyPermille(uint16_t us, uint16_t minUs, uint16_t maxUs, uint16_t neutralUs);

struct PinDuty { uint16_t a; uint16_t b; };

// Splits a signed duty into the two pin duties (permille, 0..1000) a
// DRV8833-class H-bridge input pair expects. Zero command: coast (0,0) or
// brake (1000,1000) depending on brakeOnZero. Nonzero: the active pin
// carries |duty|, the other stays 0 -- "whichever pin carries the duty
// cycle decides direction". `inverted` swaps which pin is A/B without
// touching the sign math above, fixing a wired-backwards motor from the app.
PinDuty splitPinDuty(int16_t signedDutyPermille, bool inverted, bool brakeOnZero);

}  // namespace motor
