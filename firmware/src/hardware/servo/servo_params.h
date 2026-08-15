#pragma once
#include "core/module.h"

namespace servo {

extern const core::ModuleDesc kDesc;

// Parameter indices *within this module* -- what onParamChanged() receives.
// Local, so nothing outside servo/ depends on where these landed in the
// global table, and adding a module elsewhere can never shift them.
enum : uint8_t { P_MODE = 0, P_ANGLE = 1, P_SWEEP_S = 2, P_MIN_US = 3, P_MAX_US = 4, P_SRC = 5 };

// Values of the servo.mode enum, in declaration order.
enum : int32_t { MODE_OFF = 0, MODE_HOLD = 1, MODE_SWEEP = 2, MODE_INPUT = 3 };

// Telemetry indices within this module's slice of the frame.
enum : uint8_t { T_US = 0, T_COUNT = 1 };

// --- pure math ---------------------------------------------------------------
// Lives here, not in the driver, so `pio test -e native` covers it with no
// board attached -- the same split that keeps core::trianglePercent testable
// while the LED driver stays a thin shell.

// Maps a 0-180 angle onto the calibrated pulse range. Multiplies before
// dividing so integer truncation costs at most 1us, and divides by 180 -- a
// constant -- never by (maxUs - minUs), so a degenerate min == max span is
// safe rather than a division by zero. `angle` is clamped defensively even
// though Params has already range-checked it.
uint16_t angleToUs(uint8_t angle, uint16_t minUs, uint16_t maxUs);

// Sweep position at `phaseMs` into a `periodMs` cycle. Returns 0-180.
// Resolution is inherited from trianglePercent's 0-100 return, i.e. 1.8 degrees
// -- far below what any hobby servo resolves, and hold mode is unaffected.
uint8_t sweepAngle(uint32_t phaseMs, uint32_t periodMs);

// Re-anchors the sweep phase when the period changes mid-sweep. Returns the
// elapsed value, measured against newPeriodMs, that sits the same FRACTION
// through the cycle as elapsedMs did through oldPeriodMs.
//
// Without this the servo jumps: position is (elapsed % period), so changing
// the modulus lands at an unrelated point in the travel. Measured on hardware
// at 866us -- about 156 degrees -- going from sweep_s 4 to 10. Simply not
// resetting the epoch avoids jumping to the START of the sweep, which is what
// an earlier version of this claimed to be sufficient; it is not.
uint32_t rephase(uint32_t elapsedMs, uint32_t oldPeriodMs, uint32_t newPeriodMs);

// Clamps a bus/channel value (microseconds, or 0 for "no signal yet") into
// the calibrated pulse range. See angleToUs's comment for why the
// multiply-before-divide isn't needed here -- there is no divide at all,
// only a range check -- but the degenerate-span safety (min == max) applies
// the same way.
uint16_t clampUs(int32_t us, uint16_t minUs, uint16_t maxUs);

}  // namespace servo
