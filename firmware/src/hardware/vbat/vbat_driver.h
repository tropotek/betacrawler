#pragma once
#include "core/battery.h"
#include "core/module.h"
#include "hardware/vbat/vbat_math.h"
#include "hardware/vbat/vbat_params.h"

namespace vbat {

class VbatDriver : public core::Module {
 public:
  explicit VbatDriver(core::Battery& out) : out_(out) {}

  void attach(const core::Registry& reg, const core::Params& p) override;
  void begin() override;
  void tick(uint32_t nowMs) override;
  void onParamChanged(uint8_t local, const core::Params& p) override;
  void readTelemetry(core::TlmValue* out) override;

 private:
  void     publish(uint16_t packMv, uint32_t nowMs);
  uint16_t simMv(uint32_t nowMs);
  uint16_t adcMv();

  core::Battery& out_;            // this module's own bus, mutable
  int32_t  source_   = SRC_OFF;
  int32_t  scale_    = 11000;
  int32_t  cellsSel_ = CELLS_AUTO;
  uint16_t mv_       = 0;
  uint8_t  cells_    = 0;         // latched; never recomputed once non-zero
  CellLatch latch_;               // confirms a count before cells_ takes it
  uint8_t  pct_      = 0;
  uint32_t simT0_    = 0;
};

}  // namespace vbat
