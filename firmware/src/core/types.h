#pragma once
#include <stdint.h>
#include <stddef.h>

namespace core {

constexpr size_t   kMaxStrLen   = 31;    // device.name max chars
constexpr size_t   kMaxLineIn   = 256;   // inbound line budget
constexpr size_t   kMaxLineOut  = 1024;  // outbound: schema is ~450 bytes
constexpr uint16_t kProtoVersion = 1;

enum ParamId : uint8_t {
  PARAM_LED_MODE = 0,
  PARAM_LED_BLINK_HZ,
  PARAM_DEVICE_NAME,
  PARAM_TLM_RATE,
  PARAM_COUNT
};

enum class ParamType { U8, Str, Enum };

enum class SetResult { Ok, NoKey, Range, BadEnum, TooLong, WrongType };

// One value slot. 36 bytes x 4 params is irrelevant on a 128KB part, and a
// flat struct is far simpler than a variant.
struct Value {
  int32_t num;
  char    str[kMaxStrLen + 1];
};

}  // namespace core
