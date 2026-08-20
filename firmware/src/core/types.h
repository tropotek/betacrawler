#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config.h"

namespace core {

constexpr size_t   kMaxStrLen   = 31;    // longest Str param value, in chars
constexpr size_t   kMaxLineIn   = 256;   // inbound line budget
// Outbound line budget -- the schema response (parameter table + telemetry
// descriptor) is the largest thing sent, currently 7281 bytes on this
// board's own module set. dispatch.cpp refuses to emit a truncated schema
// rather than corrupt it, so exceeding this ceiling drops the response
// instead -- and drops it whole, which reads as a dead device rather than a
// size problem. test_schema_lists_all_params_and_fits_buffer guards this
// board's actual size, but a fork with a larger module set must recheck its
// own. Costs 8KB of static RAM in one buffer, g_out in main.cpp, cheap on a
// 128KB part.
constexpr size_t   kMaxLineOut  = 8192;
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
