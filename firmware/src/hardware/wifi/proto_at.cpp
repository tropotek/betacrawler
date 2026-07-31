#include "hardware/wifi/proto_at.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

namespace wifi {

LineKind classifyLine(const char* line) {
  if (strcmp(line, "OK") == 0)    return LineKind::Ok;
  if (strcmp(line, "ERROR") == 0 || strcmp(line, "FAIL") == 0) return LineKind::Error;
  if (strcmp(line, "WIFI CONNECTED") == 0)  return LineKind::WifiConnected;
  if (strcmp(line, "WIFI GOT IP") == 0)     return LineKind::WifiGotIp;
  if (strcmp(line, "WIFI DISCONNECT") == 0) return LineKind::WifiDisconnect;
  if (strncmp(line, "+CWJAP:", 7) == 0)          return LineKind::CwjapReply;
  if (strncmp(line, "+CWLAP:", 7) == 0)          return LineKind::CwlapRow;
  if (strncmp(line, "+CIFSR:STAIP,", 13) == 0)   return LineKind::Cifsr;
  return LineKind::Other;
}

// Shared by parseCwjapReply/parseCwlapRow: both carry a quoted SSID as
// their first quoted field. Walking to the matching close-quote (rather
// than splitting on ',') is what keeps an SSID containing a comma from
// truncating the field and desyncing everything after it. `from` is
// positioned anywhere before the opening quote (a ':' or a '(' in
// practice); returns a pointer just past the closing quote, or nullptr on
// a malformed line.
static const char* readQuotedField(const char* from, char* out, size_t cap) {
  const char* q = strchr(from, '"');
  if (!q) return nullptr;
  ++q;
  size_t n = 0;
  while (*q && *q != '"') {
    if (out && n + 1 < cap) out[n++] = *q;
    ++q;
  }
  if (*q != '"') return nullptr;
  if (out && cap > 0) out[n < cap ? n : cap - 1] = '\0';
  return q + 1;
}

bool parseCwjapReply(const char* line, char* ssidOut, size_t ssidCap, int16_t* rssiOut) {
  // +CWJAP:"ssid","bssid",channel,rssi
  const char* afterSsid = readQuotedField(line, ssidOut, ssidCap);
  if (!afterSsid) return false;
  const char* afterBssid = readQuotedField(afterSsid, nullptr, 0);   // skip bssid
  if (!afterBssid) return false;
  const char* comma1 = strchr(afterBssid, ',');    // end of the bssid field
  if (!comma1) return false;
  const char* comma2 = strchr(comma1 + 1, ',');    // end of the channel field
  if (!comma2) return false;
  *rssiOut = (int16_t)atoi(comma2 + 1);
  return true;
}

bool parseCwlapRow(const char* line, ScanResult* out) {
  // +CWLAP:(ecn,"ssid",rssi,"mac",channel)
  const char* afterSsid = readQuotedField(line, out->ssid, sizeof(out->ssid));
  if (!afterSsid) return false;
  if (*afterSsid != ',') return false;
  out->rssi = (int16_t)atoi(afterSsid + 1);
  return true;
}

bool parseCifsr(const char* line, uint32_t* ipOut) {
  const char* q = strchr(line, '"');
  if (!q) return false;
  unsigned a, b, c, d;
  char extra;
  // %c after the fourth octet catches trailing junk ("1.2.3.4x") that
  // sscanf's %u would otherwise silently accept up to.
  int matched = sscanf(q + 1, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra);
  if (matched != 5 || extra != '"') return false;
  if (a > 255 || b > 255 || c > 255 || d > 255) return false;
  *ipOut = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
  return true;
}

}  // namespace wifi
