#include "features/tank_drive/tank_drive_driver.h"
#include "features/tank_drive/tank_drive_math.h"
#include "core/registry.h"
#include "config.h"

// Guards the body, not just the class, exactly like every other driver in
// this tree -- PlatformIO compiles every .cpp under src/ regardless of what
// includes it.
#if FEATURE_TANK_DRIVE

namespace tank_drive {

// Standard RC convention. This module has no output-range calibration of
// its own -- esc0/esc1 already clamp any source into their own calibrated
// range, the same reasoning that already lets esc0.src point at literally
// any channel-shaped source. See the design doc's Params section.
constexpr int16_t  kCenterUs   = 1500;
constexpr uint16_t kMinUs      = 1000;
constexpr uint16_t kMaxUs      = 2000;

// No general per-channel deadband exists yet -- that's a later, separate
// piece (see the design doc's amendment). Mixing runs with no deadband for
// now; tank_drive_math::mix() keeps the parameter for testability.
constexpr uint16_t kDeadbandUs = 0;

// A stale rx link must never leave this module commanding motion. Mirrors
// ESC0_INPUT_STALE_MS/ESC1_INPUT_STALE_MS's own 500ms default.
constexpr uint32_t kRxStaleMs = 500;

void TankDriveDriver::attach(const core::Registry& reg, const core::Params& p) {
  (void)p;
  inputs_ = &reg.inputs();
}

void TankDriveDriver::apply(const core::Params& p) {
  throttleSrcIdx_  = (uint8_t)p.num(globalParam(P_THROTTLE_SRC));
  steerSrcIdx_     = (uint8_t)p.num(globalParam(P_STEER_SRC));
  reverseRatioPct_ = (uint8_t)p.num(globalParam(P_REVERSE_RATIO));
}

void TankDriveDriver::onParamChanged(uint8_t local, const core::Params& p) {
  (void)local;
  apply(p);
}

void TankDriveDriver::compute(uint32_t nowMs) {
  const bool rxFresh = linkFresh(inputs_->lastFreshMs(), nowMs, kRxStaleMs);

  MixResult r;
  if (rxFresh) {
    const int16_t throttleUs = inputs_->get(throttleSrcIdx_);
    const int16_t steerUs    = inputs_->get(steerSrcIdx_);
    r = mix(throttleUs, steerUs, kCenterUs, kMinUs, kMaxUs, reverseRatioPct_, kDeadbandUs);
  } else {
    r.leftUs  = (uint16_t)kCenterUs;
    r.rightUs = (uint16_t)kCenterUs;
  }

  lastLeftUs_  = r.leftUs;
  lastRightUs_ = r.rightUs;
  // Slots 0/1 are this module's own left/right convention -- see Task 4's
  // notes on how esc0/esc1 learn about it without depending on this header.
  driveOutputs_.set(0, (int16_t)r.leftUs);
  driveOutputs_.set(1, (int16_t)r.rightUs);
  // Only mark fresh when rx itself is fresh -- a downstream esc0/esc1
  // reading this bus's lastFreshMs() must see staleness propagate, not a
  // bus that looks alive because THIS module is still ticking.
  if (rxFresh) driveOutputs_.markFresh(nowMs);
}

void TankDriveDriver::tick(uint32_t nowMs) {
  compute(nowMs);
}

void TankDriveDriver::readTelemetry(core::TlmValue* out) {
  out[T_LEFT].u  = lastLeftUs_;
  out[T_RIGHT].u = lastRightUs_;
}

}  // namespace tank_drive

#endif  // FEATURE_TANK_DRIVE
