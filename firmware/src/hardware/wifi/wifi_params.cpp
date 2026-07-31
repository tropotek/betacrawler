#include "hardware/wifi/wifi_params.h"

namespace wifi {

using core::ParamDef;
using core::ParamType;
using core::TlmDef;
using core::TlmType;

static const ParamDef kParams[] = {
  // key             type            label       unit     min max opts     n  maxlen             def defStr group    showIfKey showIfVal secret
  {"wifi.ssid",     ParamType::Str, "SSID",     nullptr, 0, 0, nullptr, 0, core::kMaxStrLen, 0, "", nullptr, nullptr, nullptr, false},
  {"wifi.password", ParamType::Str, "Password", nullptr, 0, 0, nullptr, 0, core::kMaxStrLen, 0, "", nullptr, nullptr, nullptr, true},
};

static const TlmDef kTlm[T_COUNT] = {
  // key           label     unit     type          div dec fmt      group
  {"wifi.status", "Status", nullptr,  TlmType::U32, 0,  0,  nullptr, nullptr},
  {"wifi.rssi",   "RSSI",   "dBm",    TlmType::I32, 0,  0,  nullptr, nullptr},
  {"wifi.ip",     "IP",     nullptr,  TlmType::U32, 0,  0,  "ip",    nullptr},
};

const core::ModuleDesc kDesc = {
  "wifi", "WiFi",
  kParams, sizeof(kParams) / sizeof(kParams[0]),
  kTlm, T_COUNT,
};

}  // namespace wifi
