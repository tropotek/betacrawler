#pragma once
#include "core/module.h"

namespace esc {

extern const core::ModuleDesc kDesc;

// Parameter indices *within this module* -- what onParamChanged() receives.
// Local, so nothing outside esc/ depends on where these landed in the
// global table, and adding a module elsewhere can never shift them.
enum : uint8_t { P_MODE = 0, P_THROTTLE_US = 1, P_MIN_US = 2, P_MAX_US = 3, P_SRC = 4 };

// Values of the esc.mode enum, in declaration order.
enum : int32_t { MODE_OFF = 0, MODE_ARMED = 1, MODE_INPUT = 2 };

// Telemetry indices within this module's slice of the frame.
enum : uint8_t { T_US = 0, T_ARM = 1, T_COUNT = 2 };

// Arm-hold state, and the exact value the `arm` telemetry field carries --
// a plain number, following rx's `link` field precedent that a status
// reading is just a number, extended to three states here.
enum : uint32_t { ARM_OFF = 0, ARM_ARMING = 1, ARM_ARMED = 2 };

// --- pure math ---------------------------------------------------------------
// Lives here, not in the driver, so `pio test -e native` covers the arm-hold
// state machine and the pulse clamp with no board attached -- the same split
// servo uses for angleToUs/sweepAngle/rephase.

// Clamps a commanded/bus pulse width (microseconds, or 0 for "no signal yet")
// into the calibrated range. Same shape as servo::clampUs, duplicated rather
// than shared: modules stay isolated by design, so hardware/esc must not
// depend on hardware/servo.
uint16_t clampUs(int32_t us, uint16_t minUs, uint16_t maxUs);

// One step of the arm-hold state machine. `enteringFromOff` is true exactly
// on the onParamChanged() call where mode left MODE_OFF -- the only event
// that (re)starts the hold, mirroring servo::apply()'s
// `if (prevMode == MODE_OFF) attachOutput()`. `modeIsOff` always wins
// outright, from any state. Otherwise ARMING holds until armHoldMs has
// elapsed since armT0Ms, then becomes ARMED and stays there -- switching
// between armed and input without passing through off never resets it.
uint32_t nextArmState(uint32_t prevState, bool modeIsOff, bool enteringFromOff,
                       uint32_t nowMs, uint32_t armT0Ms, uint32_t armHoldMs);

// The pulse width to write this tick, or 0 to mean "no update, hold the last
// pulse" -- the same 0 sentinel rx/servo already use for "no data yet" on
// core::Inputs. Anything other than ARM_ARMED always answers minUs: that is
// the arm-hold pulse, and it is also the correct fallback if mode is
// somehow neither armed nor input. In MODE_INPUT, inputUs <= 0 means the bus
// slot has never been written (or was invalidated) and the last real pulse
// holds, same as servo's input mode on the same bus.
uint16_t nextPulseUs(uint32_t armState, int32_t mode, uint16_t minUs, uint16_t maxUs,
                      uint16_t throttleUs, int16_t inputUs);

}  // namespace esc
