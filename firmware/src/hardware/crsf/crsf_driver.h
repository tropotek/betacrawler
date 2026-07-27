#pragma once
#include "hardware/crsf/crsf_params.h"

// Forward-declared rather than including <HardwareSerial.h>: this header is
// pulled in by modules.cpp, and the Arduino serial header is heavy.
class HardwareSerial;

namespace crsf {

// Requires CRSF_RX_PIN, CRSF_TX_PIN and CRSF_BAUD from the board header.
class CrsfDriver : public core::Module {
 public:
  void begin() override;
  void tick(uint32_t nowMs) override;
  void onParamChanged(uint8_t local, const core::Params& p) override;
  void readTelemetry(core::TlmValue* out) override;

 private:
  void drainUart(uint32_t nowMs);
  void runSim(uint32_t nowMs);
  void applyRcFrame(uint32_t nowMs);

  HardwareSerial* uart_ = nullptr;
  FrameParser     parser_;
  LinkState       link_;
  LinkStats       stats_ = {};
  uint16_t        us_[kUsedChannels] = {};
  int32_t         source_    = SRC_UART;
  uint32_t        timeoutMs_ = 1000;
  uint32_t        simT0_     = 0;
};

}  // namespace crsf
