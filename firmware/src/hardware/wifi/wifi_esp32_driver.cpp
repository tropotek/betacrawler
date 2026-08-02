#include "hardware/wifi/wifi_esp32_driver.h"
#include "config.h"

// Onboard-ESP32 counterpart to wifi_driver.cpp -- see that file's own header
// comment for why each is guarded to compile to no driver logic on the
// other architecture rather than being excluded by a build_src_filter.
#if FEATURE_WIFI && FW_MCU_ESP32

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <string.h>

// Backoff before retrying a failed join, matching WIFI_FAIL_BACKOFF_MS's
// default on the STM32/AT driver. Not board-header-overridable here: unlike
// that driver, nothing about this timing depends on board wiring.
static const uint32_t kFailBackoffMs = 5000;

// Watchdog for a join attempt that never reaches WL_CONNECTED,
// WL_CONNECT_FAILED or WL_NO_SSID_AVAIL -- the ESP32 WiFi stack can also
// settle on other wl_status_t values (e.g. WL_DISCONNECTED) while a join is
// failing, and enumerating all of them is a losing game. A flat deadline
// catches every case that isn't one of the two explicit failure signals.
static const uint32_t kJoinTimeoutMs = 15000;

namespace wifi {

// joinStartedAt_ is stamped with a fresh millis() call from beginJoin(),
// which can run mid-loop-iteration (a `set wifi.ssid` param change) -- AFTER
// main.cpp's own `nowMs` for that same iteration was already captured at
// the top of loop(). A plain `nowMs - startedAt` then underflows to a huge
// unsigned value on the very first tick() that follows, making every
// elapsed-time check against it fire one tick early.
static inline uint32_t elapsedMs(uint32_t nowMs, uint32_t startedAt) {
  return (nowMs >= startedAt) ? (nowMs - startedAt) : 0;
}

void WifiEsp32Driver::attach(const core::Registry& reg, const core::Params& p) {
  (void)reg;
  strncpy(ssid_,     p.str(globalParam(P_SSID)),     sizeof(ssid_) - 1);
  strncpy(password_, p.str(globalParam(P_PASSWORD)), sizeof(password_) - 1);
}

void WifiEsp32Driver::beginJoin() {
  WiFi.begin(ssid_, password_);
  status_ = STATUS_CONNECTING;
  joinStartedAt_ = millis();
}

void WifiEsp32Driver::begin() {
  WiFi.mode(WIFI_STA);
  // Not a no-op despite nothing being connected yet: real-hardware testing
  // showed WiFi.scanNetworks() called shortly after WiFi.mode(WIFI_STA) with
  // no disconnect() in between can leave the STA interface not fully
  // settled, so scans complete instantly with zero (sometimes stale/cached)
  // results instead of doing a real RF sweep. This forces the interface into
  // a clean, ready state -- the same fix the ESP32 core's own WiFiScan
  // example sketch uses.
  WiFi.disconnect();
  WiFi.setAutoReconnect(true);
  if (ssid_[0] != '\0') beginJoin();
}

void WifiEsp32Driver::onParamChanged(uint8_t local, const core::Params& p) {
  switch (local) {
    case P_SSID:
      strncpy(ssid_, p.str(globalParam(P_SSID)), sizeof(ssid_) - 1);
      ssid_[sizeof(ssid_) - 1] = '\0';
      break;
    case P_PASSWORD:
      strncpy(password_, p.str(globalParam(P_PASSWORD)), sizeof(password_) - 1);
      password_[sizeof(password_) - 1] = '\0';
      break;
    default:
      return;
  }
  // Either field changing invalidates whatever join is in flight or already
  // holds -- re-arm from Off exactly like the STM32/AT driver does.
  if (ssid_[0] == '\0') {
    WiFi.disconnect();
    status_ = STATUS_OFF;
    rssi_ = 0;
    ip_ = 0;
  } else {
    beginJoin();
  }
}

void WifiEsp32Driver::tick(uint32_t nowMs) {
  if (scanning_) {
    // Real-hardware-verified (2026-08-02): the async path this driver used
    // to run here -- WiFi.scanNetworks(true) polled via WiFi.scanComplete()
    // -- is unreliable on this core/hardware combination. It very often
    // reports a clean completion with zero results despite a real, strong
    // (-58dBm) AP in range, and that happened on scans that ran their full
    // ~6s duration, not just ones that returned suspiciously fast -- so no
    // amount of retrying *that* path changed the outcome. A blocking
    // WiFi.scanNetworks(false) call in the exact same spot found real
    // networks on 3 out of 3 tries. startScan() only arms `scanning_` (see
    // core::WifiScanner's own contract: Dispatcher::handle() must never
    // block) so the blocking call has to happen here instead, on the first
    // tick() after the op is dispatched -- stalling this one tick for the
    // scan's duration (telemetry gaps, no other op serviced) rather than
    // stalling the dispatch response itself.
    WiFi.disconnect();
    int16_t n = WiFi.scanNetworks(false);
    scanCount_ = (n > 0) ? (uint8_t)(n > kMaxScanResults ? kMaxScanResults : n) : 0;
    for (uint8_t i = 0; i < scanCount_; ++i) {
      strncpy(scanResults_[i].ssid, WiFi.SSID(i).c_str(), sizeof(scanResults_[i].ssid) - 1);
      scanResults_[i].ssid[sizeof(scanResults_[i].ssid) - 1] = '\0';
      scanResults_[i].rssi = (int16_t)WiFi.RSSI(i);
    }
    WiFi.scanDelete();
    scanning_ = false;
    scanResultReady_ = true;
  }

  wl_status_t s = WiFi.status();
  switch (status_) {
    case STATUS_CONNECTING:
      if (s == WL_CONNECTED) {
        status_ = STATUS_CONNECTED;
      } else if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL) {
        status_ = STATUS_FAILED;
        failedAt_ = nowMs;
      } else if (elapsedMs(nowMs, joinStartedAt_) > kJoinTimeoutMs) {
        // Some other wl_status_t (e.g. WL_DISCONNECTED) can also mean a
        // failed/timed-out join -- a flat deadline catches those without
        // trying to enumerate every value the stack might settle on.
        status_ = STATUS_FAILED;
        failedAt_ = nowMs;
      }
      break;
    case STATUS_CONNECTED:
      if (s != WL_CONNECTED) {
        // WiFi.setAutoReconnect(true) is already retrying in the background --
        // give it a fresh, tick-consistent kJoinTimeoutMs window rather than
        // reusing joinStartedAt_ from the original join. Without this,
        // elapsedMs() below sees an already-stale start time and the
        // watchdog fires on the very next tick instead of after a genuine
        // 15s grace period, turning nearly every WiFi blip into a forced
        // STATUS_FAILED. Assigned from this tick's own nowMs, so no
        // underflow risk -- same pattern the scan retry branch above uses.
        status_ = STATUS_CONNECTING;
        joinStartedAt_ = nowMs;
        rssi_ = 0;
        ip_ = 0;
      } else {
        rssi_ = (int16_t)WiFi.RSSI();
        IPAddress ip = WiFi.localIP();
        ip_ = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
              ((uint32_t)ip[2] << 8)  |  (uint32_t)ip[3];
      }
      break;
    case STATUS_FAILED:
      if (s == WL_CONNECTED) {
        // Self-healed during the backoff window (WiFi.setAutoReconnect(true)
        // got there first) -- go straight back to Connected and let this
        // case's own branch above populate rssi_/ip_ next tick, rather than
        // blindly waiting out kFailBackoffMs and forcing a needless
        // WiFi.begin() that tears down a link that's already fine.
        status_ = STATUS_CONNECTED;
      } else if (elapsedMs(nowMs, failedAt_) > kFailBackoffMs && ssid_[0] != '\0') {
        beginJoin();
      }
      break;
    default:
      break;
  }
}

void WifiEsp32Driver::readTelemetry(core::TlmValue* out) {
  out[T_STATUS].u = (uint32_t)status_;
  out[T_RSSI].i   = rssi_;
  out[T_IP].u     = ip_;
}

bool WifiEsp32Driver::startScan() {
  if (scanning_) return false;
  scanning_ = true;
  scanResultReady_ = false;
  // The blocking scan itself runs on the next tick() -- see that function's
  // own comment for why.
  return true;
}

size_t WifiEsp32Driver::pollPush(char* out, size_t cap) {
  if (!scanResultReady_) return 0;
  scanResultReady_ = false;

  JsonDocument doc;
  JsonArray arr = doc["scan"].to<JsonArray>();
  for (uint8_t i = 0; i < scanCount_; ++i) {
    JsonObject e = arr.add<JsonObject>();
    e["ssid"] = scanResults_[i].ssid;
    e["rssi"] = scanResults_[i].rssi;
  }
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

}  // namespace wifi

#endif  // FEATURE_WIFI && FW_MCU_ESP32
