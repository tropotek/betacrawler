#pragma once
#include "core/module.h"

namespace servo {

extern const core::ModuleDesc kDesc;

// Parameter indices *within this module* -- what onParamChanged() receives.
// Local, so nothing outside servo/ depends on where these landed in the
// global table, and adding a module elsewhere can never shift them.
enum : uint8_t {
  P_MIN_US = 0, P_MAX_US = 1, P_INVERT = 2, P_TRIM_US = 3,
};

// Values of the servo.invert enum, in declaration order.
enum : int32_t { INVERT_NORMAL = 0, INVERT_REVERSED = 1 };

// Telemetry indices within this module's slice of the frame.
enum : uint8_t { T_US = 0, T_COUNT = 1 };

// --- pure math ---------------------------------------------------------------
// Lives here, not in the driver, so `pio test -e native` covers it with no
// board attached -- the same split that keeps core::trianglePercent testable
// while the LED driver stays a thin shell.

// Clamps a bus value (microseconds, or 0 for "no signal yet") into the
// calibrated pulse range. A degenerate min == max span is a range check with
// nothing to divide, so it safely answers that one value.
uint16_t clampUs(int32_t us, uint16_t minUs, uint16_t maxUs);

// Mirrors a pulse about the midpoint of the calibrated span, so a servo horn
// fitted the other way round travels the right way. A degenerate min == max
// span answers that one value.
uint16_t applyInvert(uint16_t us, uint16_t minUs, uint16_t maxUs, bool inverted);

// Offsets a pulse by a signed microsecond trim and clamps back into the
// calibrated range, so a mechanically off-centre linkage can be dialled
// straight. A large trim legitimately eats travel at one end.
uint16_t applyTrim(uint16_t us, int32_t trimUs, uint16_t minUs, uint16_t maxUs);

}  // namespace servo
