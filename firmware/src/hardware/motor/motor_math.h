#pragma once
#include <stdint.h>

namespace motor {

// Values of an motor<N>.mode parameter, in declaration order. Shared by every
// motor module instance (motor0, motor1, ...) -- see motor0_params.h / motor1_params.h.
enum : int32_t { MODE_OFF = 0, MODE_ARMED = 1, MODE_INPUT = 2 };

// Values of an motor<N>.type parameter, in declaration order -- which motor
// that instance drives, and so which output electronics sit between the two.
// motor<N>'s shared calibration (min_us/max_us/mode/src) means the
// same regardless; only the final step (turning a calibrated value into pin
// output) differs, which is what motor::OutputStage's two implementations
// (MotorOutput for an ESC, HbridgeOutput for an H-bridge) exist to isolate.
// TYPE_NONE is the default: a board that has not been told what it drives
// commands nothing, so the pin stays detached whatever the mode says.
enum : int32_t { TYPE_NONE = 0, TYPE_BRUSHLESS = 1, TYPE_BRUSHED = 2 };

// Values of an motor<N>.rate parameter, in declaration order -- the PWM frame
// rate the output runs at. 50Hz is what every analog ESC auto-detects; a
// BLHeli_S-class ESC handles the rest and cuts the 0-20ms wait for the next
// frame that dominates rx-to-ESC latency at 50Hz.
enum : int32_t { RATE_50 = 0, RATE_100 = 1, RATE_200 = 2, RATE_400 = 3 };

// Low period reserved between pulses, so the ESC always sees a pulse train
// rather than a line held high. Matters only at 400Hz, where the frame is
// 2500us and motor<N>.max_us is settable to exactly that.
constexpr uint16_t kMinLowUs = 125;

// Arm-hold state, and the exact value an motor<N> module's `arm` telemetry
// field carries -- a plain number, following rx's `link` field precedent
// that a status reading is just a number, extended to three states here.
enum : uint32_t { ARM_OFF = 0, ARM_ARMING = 1, ARM_ARMED = 2 };

// --- pure math ---------------------------------------------------------------
// Shared by every ESC module instance. Lives here, not in any one instance's
// driver, so `pio test -e native` covers the arm-hold state machine and the
// pulse clamp with no board attached and with no duplicated logic between
// motor0/motor1 -- the same split servo uses for its own pulse maths.

// Clamps a commanded/bus pulse width (microseconds, or 0 for "no signal yet")
// into the calibrated range.
uint16_t clampUs(int32_t us, uint16_t minUs, uint16_t maxUs);

// True when this output must drive nothing at all -- no type chosen, or the
// mode is off. Both mean the pin is detached rather than held at neutral.
bool outputDisabled(int32_t mode, int32_t type);

// True when an apply() moved this output from disabled to live. Drives the
// arm hold, so choosing a type costs the same neutral wait leaving off does.
bool enteringEnabled(int32_t prevMode, int32_t prevType, int32_t mode, int32_t type);

// One step of the arm-hold state machine -- deliberately independent of the
// shared TX ARM switch (see drive's design doc): that switch gates the
// OUTPUT pulse (motor0/motor1's callers clamp to neutralUs() when it's inactive,
// after this state machine has already run), not this state machine, so an
// ESC that has already completed its hold stays ARM_ARMED across the switch
// being flipped off and on -- no re-hold needed, matching a real ESC's own
// arm-once-then-just-follow-commands behavior. `enteringFromOff` is true
// exactly on the onParamChanged() call where mode left MODE_OFF -- the only
// event that (re)starts the hold, mirroring servo::apply()'s
// `if (prevMode == MODE_OFF) attachOutput()`. `modeIsOff` always wins
// outright, from any state. Otherwise ARMING holds until BOTH armHoldMs has
// elapsed since armT0Ms AND commandedIsLow is true, then becomes ARMED and
// stays there -- switching between armed and input without passing through
// off never resets it. commandedIsLow gates the PROMOTION only: the caller
// is responsible for restarting the hold (resetting armT0Ms) whenever
// commandedIsLow goes false while ARMING, the same way it already resets
// armT0Ms on enteringFromOff -- this function has no memory of previous
// calls beyond prevState, so it cannot do that restart itself.
uint32_t nextArmState(uint32_t prevState, bool modeIsOff, bool enteringFromOff,
                       uint32_t nowMs, uint32_t armT0Ms, uint32_t armHoldMs,
                       bool commandedIsLow);

// The safe/idle pulse width: the midpoint of min_us/max_us. Centre is stop,
// below is reverse, above is forward. Single source of truth for "where is
// safe" -- the arm-hold pulse, the failsafe value and the arm-switch-inactive
// clamp all take this.
uint16_t neutralUs(uint16_t minUs, uint16_t maxUs);

// True when the value that would be honoured on promotion to ARMED sits
// within lowMarginUs either side of neutralUs -- the arm-completion
// precondition. MODE_ARMED checks the bench throttle value directly;
// MODE_INPUT additionally requires inputFresh on top of a confirmed reading
// (inputUs > 0), so arming never completes against a link the module's own
// freshness check has already flagged as dead. Any other mode is defensively
// "not low". The band is two-sided because drifting either way from centre is
// a real hazard -- fast reverse or fast forward -- not a harmlessly clamped
// extreme.
bool isCommandedLow(int32_t mode, uint16_t throttleUs, int16_t inputUs, bool inputFresh,
                     uint16_t neutralUs, uint16_t lowMarginUs);

// True when the bus proved itself alive within staleMs of nowMs -- see
// core::Inputs::markFresh()'s doc comment for why this is measured at the
// bus (by rx, the sole producer) rather than approximated per-instance from
// whether the channel VALUE has changed. A throttle held at its mechanical
// endpoint has zero dither and would falsely read "stale forever" under a
// value-change heuristic; this does not have that failure mode.
bool isLinkFresh(uint32_t lastFreshMs, uint32_t nowMs, uint32_t staleMs);

// True when an already-ARMED input-mode session must drop back to ARMING
// because the link went stale. Without this, recovery from any failsafe
// would restore full commanded throttle instantly with no re-hold at all --
// this closes that gap by forcing a fresh, full arm-hold cycle once the
// link returns. Only meaningful for MODE_INPUT; MODE_ARMED has no bus input
// that can go stale.
bool inputLossDemotesArmed(uint32_t armState, int32_t mode, bool inputFresh);

// True when a channel-selection change (the `src` parameter) while an
// input-mode session is already ARMED must force a fresh arm-hold cycle, the
// same way a stale link does (inputLossDemotesArmed). Without this,
// switching source channels re-points the output at a different, unvetted
// channel with no gate at all -- the exact invariant
// isCommandedLow/inputLossDemotesArmed exist to hold. Only meaningful for
// MODE_INPUT; MODE_ARMED never reads the source channel at all.
bool srcChangeDemotesArmed(uint32_t armState, int32_t mode, bool srcChanged);

// Mirrors a pulse about the centre of the calibrated span, reversing which way
// the motor turns for a given command. min + max - us, so the range maps
// exactly onto itself and neutral maps to itself -- the arm-hold pulse, the
// failsafe value and the arm-switch clamp are all unaffected. Only meaningful
// for a centre-neutral controller, which is the only kind this firmware drives.
uint16_t mirrorAboutSpan(uint16_t us, uint16_t minUs, uint16_t maxUs);

// True when the shared ARM switch must force the output to neutral.
// driveModulePresent is what separates "no drive module on this board, so
// there is no switch to obey" from "the switch says not armed" -- including
// before any receiver frame has ever arrived, which is exactly when a bench
// board is most likely to be mis-wired.
bool armSwitchGates(bool driveModulePresent, int16_t armSlotValue);

// Frame period in microseconds for a RATE_* index. An unrecognised index
// answers 20000 (50Hz): an unknown value must never speed the output up past
// what the attached ESC is known to handle.
uint32_t frameUsForRate(uint8_t rateIdx);

// The largest pulse that still leaves kMinLowUs of low time inside one frame.
// This is where motor<N>.max_us and motor<N>.rate meet: both are independently
// valid parameters (core::Params validates each against its own min/max and
// has no cross-parameter seam), so the combination is resolved here, at the
// point of use, rather than by refusing one of them.
//
// Deliberately NOT applied to neutralUs(): clamping the neutral point would
// silently move where "stop" is when the rate changes, which is the last
// thing that should move.
uint16_t effectiveMaxUs(uint16_t maxUs, uint32_t frameUs);

// True when a frame-rate change must force an already-ARMED session back
// through a fresh arm-hold. A BLHeli_S-class ESC detects its input frame rate
// as it arms, so changing that rate underneath it needs the same re-sync a
// src change already gets (srcChangeDemotesArmed). Unlike that one this takes
// no mode: MODE_ARMED drives the same pin at the same new rate and needs the
// same treatment, and MODE_OFF can never be ARM_ARMED in the first place.
bool rateChangeDemotesArmed(uint32_t armState, bool rateChanged);

// The pulse width to write this tick, or 0 to mean "no update, hold the last
// pulse" -- the same 0 sentinel rx/servo already use for "no data yet" on
// core::Inputs. Anything other than ARM_ARMED always answers neutralUs: that
// is the arm-hold pulse, and it is also the correct fallback if mode is
// somehow neither armed nor input. In MODE_INPUT, inputStale forces
// neutralUs first (checked before the sentinel case below: a frozen
// non-zero reading must not fall through to "hold last pulse" -- it must
// actively force neutralUs). Otherwise inputUs <= 0 means the bus slot has
// never been written (or was invalidated) and the last real pulse holds,
// same as servo's input mode on the same bus.
uint16_t nextPulseUs(uint32_t armState, int32_t mode, uint16_t minUs, uint16_t maxUs,
                      uint16_t throttleUs, int16_t inputUs, bool inputStale, uint16_t neutralUs);

}  // namespace motor
