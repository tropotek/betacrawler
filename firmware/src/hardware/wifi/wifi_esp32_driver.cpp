#include "hardware/wifi/wifi_esp32_driver.h"

// Onboard-ESP32 counterpart to wifi_driver.cpp -- see that file's own header
// comment for why each is guarded to compile to nothing on the other
// architecture rather than being excluded by a build_src_filter.
#if FEATURE_WIFI && FW_MCU_ESP32

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <string.h>

// Backoff before retrying a failed join, matching WIFI_FAIL_BACKOFF_MS's
// default on the STM32/AT driver. Not board-header-overridable here: unlike
// that driver, nothing about this timing depends on board wiring.
static const uint32_t kFailBackoffMs = 5000;

namespace wifi {

void WifiEsp32Driver::attach(const core::Registry& reg, const core::Params& p) {
  (void)reg;
  strncpy(ssid_,     p.str(globalParam(P_SSID)),     sizeof(ssid_) - 1);
  strncpy(password_, p.str(globalParam(P_PASSWORD)), sizeof(password_) - 1);
}

void WifiEsp32Driver::beginJoin() {
  WiFi.begin(ssid_, password_);
  status_ = STATUS_CONNECTING;
}

void WifiEsp32Driver::begin() {
  WiFi.mode(WIFI_STA);
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
    // WiFi.scanComplete()'s contract: -1 (WIFI_SCAN_RUNNING) while still
    // scanning, -2 (WIFI_SCAN_FAILED) if the scan could not start or
    // errored, otherwise the network count.
    int16_t n = WiFi.scanComplete();
    if (n >= 0) {
      scanCount_ = (uint8_t)(n > kMaxScanResults ? kMaxScanResults : n);
      for (uint8_t i = 0; i < scanCount_; ++i) {
        strncpy(scanResults_[i].ssid, WiFi.SSID(i).c_str(), sizeof(scanResults_[i].ssid) - 1);
        scanResults_[i].ssid[sizeof(scanResults_[i].ssid) - 1] = '\0';
        scanResults_[i].rssi = (int16_t)WiFi.RSSI(i);
      }
      WiFi.scanDelete();
      scanning_ = false;
      scanResultReady_ = true;
    } else if (n == -2) {
      WiFi.scanDelete();
      scanCount_ = 0;
      scanning_ = false;
      scanResultReady_ = true;   // report an empty result rather than wedge
    }
  }

  wl_status_t s = WiFi.status();
  switch (status_) {
    case STATUS_CONNECTING:
      if (s == WL_CONNECTED) {
        status_ = STATUS_CONNECTED;
      } else if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL) {
        status_ = STATUS_FAILED;
        failedAt_ = nowMs;
      }
      break;
    case STATUS_CONNECTED:
      if (s != WL_CONNECTED) {
        status_ = STATUS_CONNECTING;   // WiFi.setAutoReconnect(true) is already retrying
      } else {
        rssi_ = (int16_t)WiFi.RSSI();
        IPAddress ip = WiFi.localIP();
        ip_ = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
              ((uint32_t)ip[2] << 8)  |  (uint32_t)ip[3];
      }
      break;
    case STATUS_FAILED:
      if (nowMs - failedAt_ > kFailBackoffMs && ssid_[0] != '\0') beginJoin();
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
  WiFi.scanNetworks(true);   // async
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
