#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config.h"

namespace core {

constexpr size_t   kMaxStrLen   = 31;    // longest Str param value, in chars
constexpr size_t   kMaxLineIn   = 256;   // inbound line budget
// Outbound line budget. The schema response is the largest thing sent: it
// carries the parameter table AND the telemetry descriptor. Measured 4251
// bytes with the final 30 telemetry fields, 23 of them RX (16 RC channels,
// five link-health, rfrate/pwr), plus the protocol params with showIf. This
// is the final measurement -- the RX module's field set is done growing, so
// there is nothing further projected to arrive. So 4096 no longer fits.
// Headroom at 6144 is ~1900 bytes.
// test_schema_lists_all_params_and_fits_buffer guards THIS BOARD ONLY -- it
// runs against realReg, the actual module set registerModules() builds here,
// so it cannot fail for a fork with a different (larger) module set. A fork
// that raises FW_MAX_MODULES/FW_MAX_PARAMS/FW_MAX_TLM enough to grow the
// schema past this ceiling must re-check the schema size itself; nothing
// here does it for them. Dispatcher::handle() refuses to emit a truncated
// schema (see core/dispatch.cpp), so exceeding this ceiling drops the
// response rather than corrupting it -- but the fork still loses schema
// data it needs. Costs 2KB of RAM in one buffer, g_out in main.cpp, on a
// 128KB part.
constexpr size_t   kMaxLineOut  = 6144;
constexpr uint16_t kProtoVersion = 1;

// An index into the registry's flattened parameter list. This used to be a
// compile-time enum (PARAM_LED_MODE, ...), which forced core/ to know every
// feature that could ever exist. Modules now contribute their own parameters
// at boot, so the index is only knowable at runtime and callers resolve one
// by key via Registry::findParam().
using ParamId = uint8_t;
constexpr ParamId kNoParam = 0xFF;

enum class ParamType { U8, Str, Enum };

enum class SetResult { Ok, NoKey, Range, BadEnum, TooLong, WrongType };

// One value slot. 36 bytes x FW_MAX_PARAMS is irrelevant on a 128KB part, and
// a flat struct is far simpler than a variant.
struct Value {
  int32_t num;
  char    str[kMaxStrLen + 1];
};

}  // namespace core
