#pragma once
#include <stdint.h>

class HardwareTimer;

namespace motor {

// Resolves `pin` to its PWM channel on `timer`, or 0 when the pin's channel
// belongs to a different timer -- the pinmap answers a pin's FIRST timer
// entry, which is not always the intended one, and a board header names an
// ALT alias (PA7_ALT1) to pick another. A 0 channel is never attached and
// never written, and one boot-log line names the pin.
uint32_t resolveChannel(HardwareTimer* timer, uint32_t pin);

// Hardware-facing interface each motor<N> instance dispatches to, selected at
// runtime by that instance's `type` param. One HardwareTimer per motor<N>
// instance is shared across every OutputStage it might switch between --
// passed in at construction, not owned here.
class OutputStage {
 public:
  virtual ~OutputStage() {}

  // One-time setup against the shared timer (resolving pin->channel maps).
  // Called once, from MotorDriver::begin(), on every possible stage -- not
  // just the initially-active one, since `type` can switch at runtime.
  virtual void begin() = 0;

  // Enables hardware output. Called when this stage becomes active (either
  // entering from MODE_OFF, or `type` just switched to it while already
  // live).
  virtual void attachOutput() = 0;

  // Disables hardware output and forces pins to a safe idle state. Called
  // when this stage stops being active (MODE_OFF, or `type` switching away
  // from it while live) -- also safe to call on a stage that was never
  // attached (e.g. every non-default stage at boot).
  virtual void detach() = 0;

  // Retunes the PWM period. MotorOutput's periodUs is an RC frame period
  // (from motor<N>.rate); HbridgeOutput's is a switching period (from
  // motor<N>.freq) -- different meanings, same units, so one method serves
  // both.
  virtual void setPeriodUs(uint32_t periodUs) = 0;

  // Writes one commanded value for this tick. `us` is the same calibrated
  // value motor<N> has always computed (see motor_math.h's clampUs/neutralUs),
  // already clamped into [minUs, maxUs]. minUs/maxUs/neutralUs are passed
  // through so a stage can rescale (HbridgeOutput maps this range to a
  // signed duty) without needing its own copy of calibration state.
  virtual void write(uint16_t us, uint16_t minUs, uint16_t maxUs, uint16_t neutralUs) = 0;

  // Hbridge-only configuration. No-op default: MotorOutput has no use for
  // either, matching core::Module's own "do-nothing default" convention.
  virtual void setInverted(bool inverted) { (void)inverted; }
  virtual void setBrakeOnZero(bool brakeOnZero) { (void)brakeOnZero; }
};

}  // namespace motor
