#pragma once
#include "core/module.h"
#include "core/dispatch.h"
#include "hardware/wifi/proto_at.h"
#include "hardware/wifi/wifi_params.h"
#include <HardwareSerial.h>

namespace wifi {

constexpr uint8_t kMaxScanResults = 10;

class WifiDriver : public core::Module, public core::WifiScanner {
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
  enum class State : uint8_t { Init, Idle, Joining, Connected, Failed };

  void sendLine(const char* cmd);
  void handleLine(const char* line);
  void beginJoin();
  void drainUart(uint32_t nowMs);
  void finishScan();

  HardwareSerial* uart_ = nullptr;
  State    state_       = State::Init;
  uint32_t cmdSentAt_    = 0;     // 0 == no command currently in flight
  uint32_t nextInitStep_ = 0;     // index into the boot AT-command sequence
  uint32_t lastStatusPollMs_ = 0;
  uint32_t failedAt_     = 0;

  char ssid_[core::kMaxStrLen + 1]     = {0};
  char password_[core::kMaxStrLen + 1] = {0};

  int32_t  status_ = STATUS_OFF;
  int16_t  rssi_   = 0;
  uint32_t ip_     = 0;

  // Line assembly for the ESP-01's own \r\n-terminated stream -- separate
  // from core::LineReader, which is the host-link parser and assumes
  // \n-only, kMaxLineIn-sized lines.
  char   lineBuf_[128] = {0};
  size_t lineLen_ = 0;

  bool       scanning_        = false;
  bool       scanResultReady_ = false;
  uint32_t   scanStartedAt_   = 0;
  ScanResult scanResults_[kMaxScanResults];
  uint8_t    scanCount_ = 0;
  // Set when a WifiDisconnect URC's auto-rejoin arrives while scanning_ is
  // still true: sending AT+CWJAP here would interleave a second in-flight
  // command with AT+CWLAP's own still-outstanding reply, and scan
  // completion is detected as "whichever Ok/Error arrives next while
  // scanning_" -- there is no per-command correlation to tell the two
  // replies apart. finishScan() acts on this once the scan actually ends.
  bool       rejoinPending_   = false;
};

}  // namespace wifi
