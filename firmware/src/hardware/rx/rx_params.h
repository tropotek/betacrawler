#pragma once
#include "core/module.h"
#include "hardware/rx/proto_crsf.h"

namespace rx {

// --- module descriptor ------------------------------------------------------

extern const core::ModuleDesc kDesc;

enum : uint8_t { P_SOURCE = 0, P_TIMEOUT_MS = 1 };

// Order must match kSources in rx_params.cpp -- the wire carries the name,
// the driver receives the index.
enum : int32_t { SRC_UART = 0, SRC_SIM = 1 };

// Twelve channels then five link fields. T_CH1 + n indexes channel n+1, which
// is what lets the driver fill the slice with one loop.
enum : uint8_t {
  T_CH1 = 0,
  T_LINK = T_CH1 + kUsedChannels,
  T_LQ,
  T_RSSI,
  T_RATE,
  T_ERR,
  T_COUNT,
};

}  // namespace rx
