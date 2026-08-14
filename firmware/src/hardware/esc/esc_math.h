#pragma once
#include <stdint.h>

namespace esc {

// Values of an esc<N>.mode parameter, in declaration order. Shared by every
// ESC module instance (esc0, esc1, ...) -- see esc0_params.h / esc1_params.h.
enum : int32_t { MODE_OFF = 0, MODE_ARMED = 1, MODE_INPUT = 2 };

// Values of an esc<N>.direction parameter, in declaration order.
enum : int32_t { DIR_UNIDIRECTIONAL = 0, DIR_BIDIRECTIONAL = 1 };

// Arm-hold state, and the exact value an esc<N> module's `arm` telemetry
// field carries -- a plain number, following rx's `link` field precedent
// that a status reading is just a number, extended to three states here.
enum : uint32_t { ARM_OFF = 0, ARM_ARMING = 1, ARM_ARMED = 2 };

// --- pure math ---------------------------------------------------------------
// Shared by every ESC module instance. Lives here, not in any one instance's
// driver, so `pio test -e native` covers the arm-hold state machine and the
// pulse clamp with no board attached and with no duplicated logic between
// esc0/esc1 -- the same split servo uses for angleToUs/sweepAngle/rephase.

// Clamps a commanded/bus pulse width (microseconds, or 0 for "no signal yet")
// into the calibrated range.
uint16_t clampUs(int32_t us, uint16_t minUs, uint16_t maxUs);

// One step of the arm-hold state machine. `enteringFromOff` is true exactly
// on the onParamChanged() call where mode left MODE_OFF -- one event that
// (re)starts the hold, mirroring servo::apply()'s
// `if (prevMode == MODE_OFF) attachOutput()`. A caller may pass true here for
// another analogous transition too (esc0/esc1 also do so when the shared ARM
// switch goes from inactive to active -- see their own callers' comments);
// this function does not care which. `modeIsOff` always wins
// outright, from any state. armSwitchInactive gets the identical hard-reset
// treatment -- a shared TX-switch ARM gate that lives above this function,
// not inside it; see tank_drive's design doc for what sets it. Otherwise
// ARMING holds until BOTH armHoldMs has elapsed since armT0Ms AND
// commandedIsLow is true, then becomes ARMED and stays there -- switching
// between armed and input without passing through off never resets it.
// commandedIsLow gates the PROMOTION only: the caller is responsible for
// restarting the hold (resetting armT0Ms) whenever commandedIsLow goes false
// while ARMING, the same way it already resets armT0Ms on enteringFromOff --
// this function has no memory of previous calls beyond prevState, so it
// cannot do that restart itself.
uint32_t nextArmState(uint32_t prevState, bool modeIsOff, bool armSwitchInactive,
                       bool enteringFromOff, uint32_t nowMs, uint32_t armT0Ms,
                       uint32_t armHoldMs, bool commandedIsLow);

// The safe/idle pulse width for a given direction: min_us for a
// unidirectional ESC (the low end is stop; everything above it is
// forward-only), or the midpoint of min_us/max_us for a bidirectional one
// (center is stop; below is reverse, above is forward). Single source of
// truth for "where is safe" -- every place that used to hardcode min_us as
// the arm-hold/failsafe value now takes this instead.
uint16_t neutralUs(uint16_t minUs, uint16_t maxUs, bool bidirectional);

// True when the value that would be honoured on promotion to ARMED is at or
// near neutralUs (see neutralUs() above) -- the arm-completion precondition.
// The MODE_ARMED case checks the bench throttle value directly; MODE_INPUT
// additionally requires inputFresh on top of a CONFIRMED reading
// (inputUs > 0) -- arming must never complete against a link the module's
// own freshness check has already flagged as dead. Any other mode (only
// MODE_OFF in practice, handled elsewhere by the caller) is defensively
// "not low".
//
// `bidirectional` changes the SHAPE of the check, not just the reference
// point: unidirectional keeps a one-sided check (anything at or below
// neutralUs + lowMarginUs counts, since clampUs makes "further below" just
// as safe). Bidirectional needs a two-sided band around neutralUs, since
// drifting either direction away from center is a real hazard there (fast
// reverse or fast forward), not a clamped, harmless extreme. A single
// unified symmetric check was considered and rejected: it silently
// misclassifies a legal unidirectional configuration where min_us has been
// raised well above the throttle parameter's own 1000us range floor -- a low
// throttle value far below the raised min_us is still perfectly safe
// (clamped up to min_us regardless), but a symmetric distance check would
// wrongly reject it as "too far from neutral".
bool isCommandedLow(int32_t mode, uint16_t throttleUs, int16_t inputUs, bool inputFresh,
                     uint16_t neutralUs, uint16_t lowMarginUs, bool bidirectional);

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

}  // namespace esc
