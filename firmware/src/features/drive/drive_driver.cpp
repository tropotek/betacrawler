#include "features/drive/drive_driver.h"
#include "features/drive/drive_math.h"
#include "core/registry.h"
#include "config.h"

// Guards the body, not just the class, exactly like every other driver in
// this tree -- PlatformIO compiles every .cpp under src/ regardless of what
// includes it.
#if FEATURE_DRIVE

namespace drive {

// Standard RC convention. This module has no output-range calibration of
// its own -- motor0/motor1 already clamp any source into their own calibrated
// range, the same reasoning that already lets motor0.src point at literally
// any channel-shaped source. See the design doc's Params section.
constexpr int16_t  kCenterUs   = 1500;
constexpr uint16_t kMinUs      = 1000;
constexpr uint16_t kMaxUs      = 2000;

// No general per-channel deadband exists yet -- that's a later, separate
// piece (see the design doc's amendment). Mixing runs with no deadband for
// now; drive_math::mix() keeps the parameter for testability.
constexpr uint16_t kDeadbandUs = 0;

// driveOutputs slots. 0/1 are the two motor slots -- left/right in skid mode,
// both throttle in car mode -- and 2 is the steering slot a servo reads. 3 is
// the shared ARM switch state (1 armed, 0 not). motor0/motor1 duplicate the
// arm literal under their own name, the same convention kDriveSrcBase already
// establishes for the drive_* src options -- they know the slot number, not
// that drive exists.
constexpr uint8_t kSteerSlot = 2;
constexpr uint8_t kArmSlot   = 3;

// A stale rx link must never leave this module commanding motion. Mirrors
// MOTOR0_INPUT_STALE_MS/MOTOR1_INPUT_STALE_MS's own 500ms default.
constexpr uint32_t kRxStaleMs = 500;

void DriveDriver::attach(const core::Registry& reg, const core::Params& p) {
  (void)p;
  inputs_ = &reg.inputs();
}

void DriveDriver::apply(const core::Params& p) {
  mode_            = p.num(globalParam(P_MODE));
  throttleSrcIdx_  = (uint8_t)p.num(globalParam(P_THROTTLE_SRC));
  steerSrcIdx_     = (uint8_t)p.num(globalParam(P_STEER_SRC));
  forwardRatioPct_ = (uint8_t)p.num(globalParam(P_FORWARD_RATIO));
  reverseRatioPct_ = (uint8_t)p.num(globalParam(P_REVERSE_RATIO));
  steerRatioPct_   = (uint8_t)p.num(globalParam(P_STEER_RATIO));
  armSrcIdx_       = (uint8_t)p.num(globalParam(P_ARM_SRC));
  armMinUs_        = (uint16_t)p.num(globalParam(P_ARM_MIN));
  armMaxUs_        = (uint16_t)p.num(globalParam(P_ARM_MAX));
}

void DriveDriver::onParamChanged(uint8_t local, const core::Params& p) {
  (void)local;
  apply(p);
}

void DriveDriver::compute(uint32_t nowMs) {
  const bool rxFresh = linkFresh(inputs_->lastFreshMs(), nowMs, kRxStaleMs);

  MixResult r;
  if (rxFresh) {
    const int16_t throttleUs = inputs_->get(throttleSrcIdx_);
    const int16_t steerUs    = inputs_->get(steerSrcIdx_);
    r = (mode_ == MODE_CAR)
        ? carMix(throttleUs, steerUs, kCenterUs, kMinUs, kMaxUs,
                 forwardRatioPct_, reverseRatioPct_, steerRatioPct_, kDeadbandUs)
        : mix(throttleUs, steerUs, kCenterUs, kMinUs, kMaxUs,
              forwardRatioPct_, reverseRatioPct_, steerRatioPct_, kDeadbandUs);
  } else {
    r.leftUs  = (uint16_t)kCenterUs;
    r.rightUs = (uint16_t)kCenterUs;
    r.steerUs = (uint16_t)kCenterUs;
  }

  lastLeftUs_  = r.leftUs;
  lastRightUs_ = r.rightUs;
  // Slots 0/1 are this module's own motor-slot convention and slot 2 the
  // steering output; slot 3 (kArmSlot) is the shared ARM switch -- see
  // motor0/motor1's own notes on how they learn about either without
  // depending on this header.
  driveOutputs_.set(0, (int16_t)r.leftUs);
  driveOutputs_.set(1, (int16_t)r.rightUs);
  driveOutputs_.set(kSteerSlot, (int16_t)r.steerUs);

  const bool armSrcIsNone = (armSrcIdx_ == ARM_SRC_NONE);
  const int16_t armSrcUs = armSrcIsNone ? 0 : inputs_->get((uint8_t)(armSrcIdx_ - 1));
  const bool armed = computeArmed(rxFresh, armSrcIsNone, armSrcUs, (int16_t)armMinUs_, (int16_t)armMaxUs_);
  driveOutputs_.set(kArmSlot, armed ? 1 : 0);

  // Only mark fresh when rx itself is fresh -- a downstream motor0/motor1
  // reading this bus's lastFreshMs() must see staleness propagate, not a
  // bus that looks alive because THIS module is still ticking.
  if (rxFresh) driveOutputs_.markFresh(nowMs);
}

void DriveDriver::tick(uint32_t nowMs) {
  compute(nowMs);
}

void DriveDriver::readTelemetry(core::TlmValue* out) {
  out[T_LEFT].u  = lastLeftUs_;
  out[T_RIGHT].u = lastRightUs_;
}

}  // namespace drive

#endif  // FEATURE_DRIVE
