#include <Arduino.h>
#include "hardware/wifi/wifi_driver.h"
#include "hardware/wifi/wifi_params.h"
#include "config.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

#ifndef WIFI_RX_PIN
#error "FEATURE_WIFI is on but the board header defines no WIFI_RX_PIN"
#endif
#ifndef WIFI_TX_PIN
#error "FEATURE_WIFI is on but the board header defines no WIFI_TX_PIN"
#endif
#ifndef WIFI_BAUD
#error "FEATURE_WIFI is on but the board header defines no WIFI_BAUD"
#endif

// Board-header overridable, defaulted here -- same pattern esc_driver.cpp
// and rx_driver.cpp already use for their own timing constants.
#ifndef WIFI_CMD_TIMEOUT_MS
#define WIFI_CMD_TIMEOUT_MS 5000
#endif
#ifndef WIFI_SCAN_TIMEOUT_MS
#define WIFI_SCAN_TIMEOUT_MS 8000
#endif
#ifndef WIFI_STATUS_POLL_MS
#define WIFI_STATUS_POLL_MS 3000
#endif
#ifndef WIFI_FAIL_BACKOFF_MS
#define WIFI_FAIL_BACKOFF_MS 5000
#endif

namespace wifi {

// Constructed from pins rather than a global Serial2: the STM32 core only
// defines Serial2 when the variant declares PIN_SERIAL2_RX/TX, exactly the
// same reasoning rx_driver.cpp already documents for its own USART1 instance.
static HardwareSerial g_uart(WIFI_RX_PIN, WIFI_TX_PIN);

void WifiDriver::attach(const core::Registry& reg, const core::Params& p) {
  (void)reg;
  strncpy(ssid_,     p.str(globalParam(P_SSID)),     sizeof(ssid_) - 1);
  strncpy(password_, p.str(globalParam(P_PASSWORD)), sizeof(password_) - 1);
}

void WifiDriver::begin() {
  uart_ = &g_uart;
  uart_->begin(WIFI_BAUD);
  state_ = State::Init;
  nextInitStep_ = 0;
}

void WifiDriver::sendLine(const char* cmd) {
  uart_->print(cmd);
  uart_->print("\r\n");
  cmdSentAt_ = millis();
}

void WifiDriver::beginJoin() {
  char cmd[96];
  snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid_, password_);
  sendLine(cmd);
  state_ = State::Joining;
  status_ = STATUS_CONNECTING;
}

void WifiDriver::onParamChanged(uint8_t local, const core::Params& p) {
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
  // holds -- re-arm from Idle exactly like esc/rx re-reset on a mode change.
  if (ssid_[0] == '\0') {
    state_ = State::Idle;
    status_ = STATUS_OFF;
    // Local state alone would leave the ESP-01 itself still associated --
    // tell it to actually disassociate. Fire-and-forget, same as every
    // other command this driver sends; its OK/ERROR reply is routed
    // through the normal Ok/Error handling and ignored (state_ is Idle by
    // then, so neither case does anything with it).
    sendLine("AT+CWQAP");
  } else {
    beginJoin();
  }
}

void WifiDriver::handleLine(const char* line) {
  LineKind kind = classifyLine(line);

  if (scanning_) {
    if (kind == LineKind::CwlapRow) {
      if (scanCount_ < kMaxScanResults) {
        if (parseCwlapRow(line, &scanResults_[scanCount_])) ++scanCount_;
      }
      return;
    }
    if (kind == LineKind::Ok || kind == LineKind::Error) {
      finishScan();
      return;
    }
    // Fall through for anything else (e.g. a stray URC mid-scan) -- handled
    // by the normal state machine below.
  }

  switch (kind) {
    case LineKind::WifiConnected:
      break;   // informational only; GOT IP is what actually gates "connected"
    case LineKind::WifiGotIp:
      state_ = State::Connected;
      status_ = STATUS_CONNECTED;
      cmdSentAt_ = 0;
      break;
    case LineKind::WifiDisconnect:
      if (state_ == State::Connected) {
        state_ = State::Idle;
        status_ = STATUS_OFF;
        rssi_ = 0;
        ip_ = 0;
        if (ssid_[0] != '\0') {
          // A scan's AT+CWLAP reply may still be outstanding -- sending
          // AT+CWJAP now would put a second command on the wire with no
          // per-command correlation to tell the replies apart (scan
          // completion is "whichever Ok/Error arrives next while
          // scanning_"). Defer the rejoin; finishScan() sends it once the
          // scan actually ends.
          if (scanning_) rejoinPending_ = true;
          else beginJoin();
        }
      }
      break;
    case LineKind::CwjapReply: {
      char joinedSsid[33];
      int16_t rssi = 0;
      if (parseCwjapReply(line, joinedSsid, sizeof(joinedSsid), &rssi)) rssi_ = rssi;
      break;
    }
    case LineKind::Cifsr: {
      uint32_t ip = 0;
      if (parseCifsr(line, &ip)) ip_ = ip;
      break;
    }
    case LineKind::Error:
      if (state_ == State::Joining) {
        state_ = State::Failed;
        status_ = STATUS_FAILED;
        failedAt_ = millis();
      }
      cmdSentAt_ = 0;
      break;
    case LineKind::Ok:
      if (state_ == State::Init) {
        ++nextInitStep_;
        cmdSentAt_ = 0;
      }
      break;
    default:
      break;
  }
}

void WifiDriver::drainUart(uint32_t nowMs) {
  (void)nowMs;
  while (uart_->available() > 0) {
    char c = (char)uart_->read();
    if (c == '\r') continue;
    if (c == '\n') {
      lineBuf_[lineLen_] = '\0';
      if (lineLen_ > 0) handleLine(lineBuf_);
      lineLen_ = 0;
      continue;
    }
    if (lineLen_ + 1 < sizeof(lineBuf_)) lineBuf_[lineLen_++] = c;
    // else: silently drop the overlong byte, mirroring core::LineReader's
    // "keep consuming until newline" recovery -- a wedged line must not
    // starve the buffer forever.
  }
}

void WifiDriver::tick(uint32_t nowMs) {
  drainUart(nowMs);

  // A command in flight that never answered -- degrade rather than wedge.
  if (cmdSentAt_ != 0 && nowMs - cmdSentAt_ > WIFI_CMD_TIMEOUT_MS) {
    cmdSentAt_ = 0;
    if (state_ == State::Joining) { state_ = State::Failed; status_ = STATUS_FAILED; failedAt_ = nowMs; }
    else if (state_ == State::Init) { ++nextInitStep_; }   // skip a wedged init step rather than hang forever
  }
  if (scanning_ && nowMs - scanStartedAt_ > WIFI_SCAN_TIMEOUT_MS) {
    finishScan();   // report whatever rows arrived before the timeout
  }

  switch (state_) {
    case State::Init:
      if (cmdSentAt_ == 0) {
        static const char* const kInit[] = {"ATE0", "AT+CWMODE=1"};
        if (nextInitStep_ < 2) {
          sendLine(kInit[nextInitStep_]);
        } else {
          state_ = State::Idle;
          if (ssid_[0] != '\0') beginJoin();
        }
      }
      break;
    case State::Failed:
      if (nowMs - failedAt_ > WIFI_FAIL_BACKOFF_MS && ssid_[0] != '\0') beginJoin();
      break;
    case State::Connected:
      if (!scanning_ && cmdSentAt_ == 0 && nowMs - lastStatusPollMs_ > WIFI_STATUS_POLL_MS) {
        lastStatusPollMs_ = nowMs;
        sendLine("AT+CWJAP?");
        // AT+CIFSR is queued on the reply to AT+CWJAP? in a fuller
        // implementation; issuing both back-to-back here is simplest and
        // the ESP-01 queues AT commands, so this is left as the first one
        // -- a second poll cycle three seconds later picks up AT+CIFSR.
        // (Revisit if IP goes stale in bring-up testing -- Task 12.)
      }
      break;
    default:
      break;
  }
}

void WifiDriver::readTelemetry(core::TlmValue* out) {
  out[T_STATUS].u = (uint32_t)status_;
  out[T_RSSI].i   = rssi_;
  out[T_IP].u     = ip_;
}

// Called exactly once per scan, from whichever of the two places actually
// ends it: handleLine()'s Ok/Error branch (the normal case) or tick()'s
// WIFI_SCAN_TIMEOUT_MS guard (the scan wedged). Centralising this is what
// guarantees a WifiDisconnect's deferred rejoin (see rejoinPending_) fires
// exactly once, from whichever path actually closes the scan out.
void WifiDriver::finishScan() {
  scanning_ = false;
  scanResultReady_ = true;
  // The scan's own AT+CWLAP is no longer in flight either way -- via its
  // Ok/Error reply, or because it just timed out.
  cmdSentAt_ = 0;
  if (rejoinPending_) {
    rejoinPending_ = false;
    beginJoin();
  }
}

bool WifiDriver::startScan() {
  if (scanning_) return false;
  scanning_ = true;
  scanCount_ = 0;
  scanStartedAt_ = millis();
  sendLine("AT+CWLAP");
  return true;
}

size_t WifiDriver::pollPush(char* out, size_t cap) {
  if (!scanResultReady_) return 0;
  scanResultReady_ = false;

  JsonDocument doc;
  JsonArray arr = doc["scan"].to<JsonArray>();
  for (uint8_t i = 0; i < scanCount_; ++i) {
    JsonObject e = arr.add<JsonObject>();
    e["ssid"] = scanResults_[i].ssid;
    e["rssi"] = scanResults_[i].rssi;
  }
  if (measureJson(doc) + 1 > cap) return 0;   // refuse rather than truncate, same rule writeLog follows
  return serializeJson(doc, out, cap);
}

}  // namespace wifi
