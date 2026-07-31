#pragma once
#include <stdint.h>
#include <stddef.h>

namespace wifi {

// One parsed row from AT+CWLAP's reply, e.g.
// +CWLAP:(3,"MyHomeNetwork",-52,"aa:bb:cc:dd:ee:ff",6)
// ecn and mac/channel are not needed by this module and are not kept.
struct ScanResult {
  char    ssid[33];   // 32 bytes is the WiFi spec's own SSID ceiling
  int16_t rssi;
};

// What a completed line from the ESP-01 means to the driver's state
// machine. Other covers both plain echo/noise and any AT reply this driver
// does not need (e.g. AT+CWMODE's own OK-preceding blank line).
enum class LineKind : uint8_t {
  Other, Ok, Error, WifiConnected, WifiGotIp, WifiDisconnect,
  CwjapReply,   // answers AT+CWJAP?
  CwlapRow,     // one row of an AT+CWLAP reply -- there are 0..N of these
  Cifsr,        // answers AT+CIFSR
};

LineKind classifyLine(const char* line);

// Below: each takes a line already known (via classifyLine) to be the kind
// named. All return false on a malformed line rather than partially filling
// their output -- defensive only, since classifyLine already checked the
// prefix that gets a line routed here at all.

bool parseCwjapReply(const char* line, char* ssidOut, size_t ssidCap, int16_t* rssiOut);
bool parseCwlapRow(const char* line, ScanResult* out);
bool parseCifsr(const char* line, uint32_t* ipOut);

}  // namespace wifi
