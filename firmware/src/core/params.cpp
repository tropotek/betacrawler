#include "core/params.h"
#include <string.h>

namespace core {

static const char* const kLedModes[] = {"off", "on", "blink"};

static const ParamDef kDefs[PARAM_COUNT] = {
  {"led.mode",     ParamType::Enum, "LED Mode",       nullptr,
   0, 0, kLedModes, 3, 0, 2, nullptr},
  {"led.blink_hz", ParamType::U8,   "Blink Rate",     "Hz",
   1, 20, nullptr, 0, 0, 2, nullptr},
  {"device.name",  ParamType::Str,  "Device Name",    nullptr,
   0, 0, nullptr, 0, kMaxStrLen, 0, "app-demo"},
  {"tlm.rate",     ParamType::U8,   "Telemetry Rate", "Hz",
   1, 50, nullptr, 0, 0, 10, nullptr},
};

const ParamDef* defs() { return kDefs; }

bool findParam(const char* key, ParamId* out) {
  for (uint8_t i = 0; i < PARAM_COUNT; ++i) {
    if (strcmp(kDefs[i].key, key) == 0) {
      *out = static_cast<ParamId>(i);
      return true;
    }
  }
  return false;
}

void Params::loadDefaults() {
  for (uint8_t i = 0; i < PARAM_COUNT; ++i) {
    v_[i].num = kDefs[i].defNum;
    v_[i].str[0] = '\0';
    if (kDefs[i].type == ParamType::Str && kDefs[i].defStr) {
      strncpy(v_[i].str, kDefs[i].defStr, kMaxStrLen);
      v_[i].str[kMaxStrLen] = '\0';
    }
  }
}

SetResult Params::setNum(ParamId id, int32_t val) {
  const ParamDef& d = kDefs[id];
  if (d.type != ParamType::U8) return SetResult::WrongType;
  if (val < d.minVal || val > d.maxVal) return SetResult::Range;
  v_[id].num = val;
  return SetResult::Ok;
}

SetResult Params::setStr(ParamId id, const char* s) {
  const ParamDef& d = kDefs[id];
  if (d.type == ParamType::Enum) {
    for (uint8_t i = 0; i < d.optionCount; ++i) {
      if (strcmp(d.options[i], s) == 0) {
        v_[id].num = i;
        return SetResult::Ok;
      }
    }
    return SetResult::BadEnum;
  }
  if (d.type != ParamType::Str) return SetResult::WrongType;
  if (strlen(s) > d.maxLen) return SetResult::TooLong;  // reject, never truncate
  strncpy(v_[id].str, s, kMaxStrLen);
  v_[id].str[kMaxStrLen] = '\0';
  return SetResult::Ok;
}

const char* Params::str(ParamId id) const {
  const ParamDef& d = kDefs[id];
  if (d.type == ParamType::Enum) {
    int32_t i = v_[id].num;
    if (i < 0 || i >= d.optionCount) return "";
    return d.options[i];
  }
  return v_[id].str;
}

}  // namespace core
