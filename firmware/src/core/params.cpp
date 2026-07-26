#include "core/registry.h"
#include <string.h>

namespace core {

void Params::loadDefaults() {
  for (uint8_t i = 0; i < reg_.paramCount(); ++i) {
    const ParamDef& d = reg_.paramDef(i);
    v_[i].num = d.defNum;
    v_[i].str[0] = '\0';
    if (d.type == ParamType::Str && d.defStr) {
      strncpy(v_[i].str, d.defStr, kMaxStrLen);
      v_[i].str[kMaxStrLen] = '\0';
    }
  }
}

SetResult Params::setNum(ParamId id, int32_t val) {
  const ParamDef& d = reg_.paramDef(id);
  if (d.type != ParamType::U8) return SetResult::WrongType;
  if (val < d.minVal || val > d.maxVal) return SetResult::Range;
  v_[id].num = val;
  return SetResult::Ok;
}

SetResult Params::setStr(ParamId id, const char* s) {
  const ParamDef& d = reg_.paramDef(id);
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
  const ParamDef& d = reg_.paramDef(id);
  if (d.type == ParamType::Enum) {
    int32_t i = v_[id].num;
    if (i < 0 || i >= d.optionCount) return "";
    return d.options[i];
  }
  return v_[id].str;
}

}  // namespace core
