#pragma once
#include "core/module.h"

namespace wifi {

extern const core::ModuleDesc kDesc;

// Parameter indices within this module -- what onParamChanged() receives.
enum : uint8_t { P_SSID = 0, P_PASSWORD = 1 };

// Telemetry indices within this module's slice of the frame.
enum : uint8_t { T_STATUS = 0, T_RSSI = 1, T_IP = 2, T_COUNT = 3 };

// Values of the wifi.status telemetry field. A plain number, following the
// precedent rx's `link` field and esc's `arm` field already set.
enum : int32_t {
  STATUS_OFF = 0, STATUS_CONNECTING = 1, STATUS_CONNECTED = 2, STATUS_FAILED = 3
};

}  // namespace wifi
