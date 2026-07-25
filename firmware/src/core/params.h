#pragma once
#include "core/types.h"

namespace core {

struct ParamDef {
  const char* key;
  ParamType   type;
  const char* label;
  const char* unit;          // nullptr when unitless
  int32_t     minVal;        // U8 only
  int32_t     maxVal;        // U8 only
  const char* const* options;  // Enum only
  uint8_t     optionCount;    // Enum only
  size_t      maxLen;         // Str only
  int32_t     defNum;         // default for U8 / Enum index
  const char* defStr;         // default for Str, nullptr otherwise
};

const ParamDef* defs();
bool findParam(const char* key, ParamId* out);

class Params {
 public:
  Params() { loadDefaults(); }
  void loadDefaults();

  SetResult setNum(ParamId id, int32_t v);
  SetResult setStr(ParamId id, const char* s);

  int32_t     num(ParamId id) const { return v_[id].num; }
  const char* str(ParamId id) const;

  // Raw access for persistence — copies all value slots.
  const Value* raw() const { return v_; }
  Value*       rawMutable() { return v_; }

 private:
  Value v_[PARAM_COUNT];
};

}  // namespace core
