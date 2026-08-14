#pragma once
#include <stdint.h>

namespace tank_drive {

// Snaps a reading to exactly centerUs when it's within deadbandUs of center,
// otherwise passes it through unchanged. Pure, so both the driver and this
// header's own tests can exercise it without a board.
int16_t deadbanded(int16_t us, int16_t centerUs, uint16_t deadbandUs);

// True when the bus proved itself alive within staleMs of nowMs. A small,
// deliberate duplication of hardware/esc/esc_math.h's own isLinkFresh rather
// than a new cross-module dependency on it -- this module stays
// self-contained, the same reasoning esc0/esc1 use for duplicating their own
// kSrcNames tables rather than sharing one. lastFreshMs == 0 is
// core::Inputs' own "never marked" default, not a real timestamp.
bool linkFresh(uint32_t lastFreshMs, uint32_t nowMs, uint32_t staleMs);

// The two computed track outputs, in microseconds.
struct MixResult {
  uint16_t leftUs;
  uint16_t rightUs;
};

// Arcade-style differential mix. Deadbands both inputs around centerUs, then
// -- if throttleUs is below center -- scales the throttle's DISTANCE from
// center by reverseRatioPct before mixing, so reverse power can be capped
// lower than forward without touching steering authority's own scale.
// left = throttle + (steer - center), right = throttle - (steer - center).
// If either result would fall outside [minUs, maxUs], BOTH are scaled down
// by the same factor (relative to centerUs) before being written -- this is
// what makes a hard-over turn at full throttle still turn, rather than one
// track clipping to its limit while the other keeps whatever value it had.
MixResult mix(int16_t throttleUs, int16_t steerUs, int16_t centerUs,
              uint16_t minUs, uint16_t maxUs,
              uint8_t reverseRatioPct, uint16_t deadbandUs);

// True when the vehicle is armed: the rx link is fresh, and either no arm
// switch is configured (armSrcIsNone) or the selected channel's raw value
// falls within [armMinUs, armMaxUs] inclusive. armSrcUs is ignored when
// armSrcIsNone. A stale link (rxFresh=false) always forces unarmed, the same
// failsafe reasoning left/right already get from linkFresh() above.
bool computeArmed(bool rxFresh, bool armSrcIsNone, int16_t armSrcUs, int16_t armMinUs,
                   int16_t armMaxUs);

}  // namespace tank_drive
