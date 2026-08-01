#pragma once
#include "core/module.h"
#include "core/dispatch.h"
#include "hardware/wifi/wifi_params.h"

namespace wifi {

constexpr uint8_t kMaxScanResults = 10;

// One scanned network. Deliberately not proto_at.h's ScanResult: that struct
// lives in the AT-protocol file this driver has nothing to do with, and
// duplicating two fields here keeps this file buildable with zero
// dependency on the STM32/AT-only driver.
struct EspScanResult {
  char    ssid[33];   // 32 bytes is the WiFi spec's own SSID ceiling
  int16_t rssi;
};

// Onboard-radio counterpart to wifi::WifiDriver: same core::Module +
// core::WifiScanner interface, same wifi_params.h descriptor (ssid/password
// params, status/rssi/ip telemetry), but driven through the ESP32 Arduino
// core's own WiFi.h instead of AT commands over UART -- there is no
// external module to talk to, the radio is on this chip.
class WifiEsp32Driver : public core::Module, public core::WifiScanner {
 public:
  void attach(const core::Registry& reg, const core::Params& p) override;
  void begin() override;
  void tick(uint32_t nowMs) override;
  void onParamChanged(uint8_t local, const core::Params& p) override;
  void readTelemetry(core::TlmValue* out) override;
  size_t pollPush(char* out, size_t cap) override;

  // core::WifiScanner
  bool startScan() override;

 private:
  void beginJoin();

  char ssid_[core::kMaxStrLen + 1]     = {0};
  char password_[core::kMaxStrLen + 1] = {0};

  int32_t  status_       = STATUS_OFF;
  int16_t  rssi_         = 0;
  uint32_t ip_           = 0;
  uint32_t failedAt_     = 0;
  uint32_t joinStartedAt_ = 0;

  bool          scanning_        = false;
  bool          scanResultReady_ = false;
  uint32_t      scanStartedAt_   = 0;
  EspScanResult scanResults_[kMaxScanResults];
  uint8_t       scanCount_ = 0;
  // WiFi.scanComplete() settling on WIFI_SCAN_FAILED for a scan that, given
  // another attempt, succeeds is real-hardware-observed flakiness in the
  // ESP32 core's async scan path (roughly 1 in 3 attempts failed in
  // on-bench testing) -- not a wedge, not a permanent failure. Retried
  // in-place, bounded, before ever reporting empty results to the host.
  uint8_t       scanRetries_ = 0;
};

}  // namespace wifi
