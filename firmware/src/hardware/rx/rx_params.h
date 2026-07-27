#pragma once
#include "core/module.h"
#include "hardware/rx/proto_crsf.h"

namespace rx {

// --- module descriptor ------------------------------------------------------

extern const core::ModuleDesc kDesc;

// --- protocols --------------------------------------------------------------

// A protocol is a table entry, not a subclass: what differs between the two
// entries is three constants and one lookup table, and a vtable would be
// ceremony around a struct.
//
// Being straight about what the selector does: Crossfire and ELRS share the
// ENTIRE CRSF parser -- same sync byte, same 0x16/0x14 frames, same baud,
// because an ELRS receiver speaks CRSF to the flight controller. What
// genuinely differs is the channel count, how the 0x14 RF-mode index is
// numbered, and how long to wait before calling the link lost. `baud` differs
// for neither today; it is here because it is where SBUS's 100000 8E2
// inverted goes without touching the driver.
struct Protocol {
  const char* name;                    // matches the rx.protocol enum option
  uint32_t    baud;
  uint8_t     channels;                // published; the wire frame always has 16
  uint8_t     timeoutParam;            // module-local, resolved via globalParam()
  uint16_t  (*rfRateHz)(uint8_t idx);  // 0 = index not in this protocol's table
};

extern const Protocol kProtocols[];
constexpr uint8_t kProtocolCount = 2;

// Order must match kProtocolNames and kProtocols in rx_params.cpp -- the wire
// carries the name, the driver receives the index.
enum : int32_t { PROTO_CROSSFIRE = 0, PROTO_ELRS = 1 };

// Order must match kSources in rx_params.cpp.
enum : int32_t { SRC_UART = 0, SRC_SIM = 1 };

enum : uint8_t {
  P_PROTOCOL          = 0,
  P_SOURCE            = 1,
  P_CROSSFIRE_TIMEOUT = 2,
  P_ELRS_TIMEOUT      = 3,
};

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
