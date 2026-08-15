#pragma once
#include "features/tank_drive/tank_drive_params.h"
#include "core/inputs.h"

namespace tank_drive {

// Touches no hardware -- reads rx's bus, writes its own. The mutable
// core::Inputs& is constructor-injected, the same narrow exception rx's own
// RxDriver gets (see docs/architecture.md's "Inputs bus" section) rather
// than anything attach() grants generally.
class TankDriveDriver : public core::Module {
 public:
  explicit TankDriveDriver(core::Inputs& driveOutputs) : driveOutputs_(driveOutputs) {}

  void attach(const core::Registry& reg, const core::Params& p) override;
  void tick(uint32_t nowMs) override;
  void onParamChanged(uint8_t local, const core::Params& p) override;
  void readTelemetry(core::TlmValue* out) override;

 private:
  void apply(const core::Params& p);
  void compute(uint32_t nowMs);

  const core::Inputs* inputs_ = nullptr;    // rx's bus, const, read-only
  core::Inputs&        driveOutputs_;        // this module's own bus, mutable

  uint8_t  throttleSrcIdx_  = 0;
  uint8_t  steerSrcIdx_     = 1;
  uint8_t  forwardRatioPct_ = 100;
  uint8_t  reverseRatioPct_ = 100;
  uint8_t  steerRatioPct_   = 100;
  uint8_t  armSrcIdx_       = 0;      // ARM_SRC_NONE
  uint16_t armMinUs_        = 1700;
  uint16_t armMaxUs_        = 2000;
  uint16_t lastLeftUs_      = 1500;
  uint16_t lastRightUs_     = 1500;
};

}  // namespace tank_drive
