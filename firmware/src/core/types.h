#pragma once
#include <stdint.h>
#include <stddef.h>
#include "config.h"

namespace core {

constexpr size_t   kMaxStrLen   = 31;    // longest Str param value, in chars
constexpr size_t   kMaxLineIn   = 256;   // inbound line budget
// Outbound line budget. The schema response is the largest thing sent: it
// now carries the parameter table AND the telemetry descriptor, ~800 bytes
// for this board. Headroom left for a board that enables several more
// modules; test_schema_lists_all_params_and_fits_buffer fails on truncation.
constexpr size_t   kMaxLineOut  = 2048;
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
