#pragma once
#include "core/types.h"

namespace core {

class Registry;

struct ParamDef {
  const char* key;
  ParamType   type;
  const char* label;
  const char* unit;            // nullptr when unitless
  int32_t     minVal;          // U8 only
  int32_t     maxVal;          // U8 only
  const char* const* options;  // Enum only
  uint8_t     optionCount;     // Enum only
  size_t      maxLen;          // Str only
  int32_t     defNum;          // default for U8 / Enum index
  const char* defStr;          // default for Str, nullptr otherwise
  // UI grouping. nullptr means "inherit the owning module's label", which is
  // the right answer almost always -- set it only when a parameter belongs
  // under a heading other than its module's (e.g. tlm.rate lives in the
  // device module but reads better under "Telemetry").
  const char* group;
  // Conditional visibility. A DISPLAY HINT ONLY, exactly like TlmDef's
  // div/dec: showIfKey names another parameter and showIfVal the enum option
  // name it must hold for this parameter to be drawn. The firmware and the
  // backend still validate and accept a hidden parameter -- Terminal `set`
  // and INI restore must keep working regardless of what the UI is drawing,
  // so visibility must never become an access rule.
  //
  // The option NAME rather than its index, deliberately: the serializer then
  // never has to resolve another parameter's option table, and app.js
  // compares the string straight against its current form value.
  const char* showIfKey;   // nullptr -> always visible
  const char* showIfVal;
};

// Parameter values. Which parameters exist, and in what order, comes entirely
// from the Registry this is constructed with -- Params itself knows nothing
// about any particular feature.
class Params {
 public:
  explicit Params(const Registry& reg) : reg_(reg) { loadDefaults(); }

  void loadDefaults();

  SetResult setNum(ParamId id, int32_t v);
  SetResult setStr(ParamId id, const char* s);

  int32_t     num(ParamId id) const { return v_[id].num; }
  const char* str(ParamId id) const;

  // Raw access for persistence -- copies all value slots.
  const Value* raw() const { return v_; }
  Value*       rawMutable() { return v_; }

 private:
  const Registry& reg_;
  Value           v_[FW_MAX_PARAMS];
};

}  // namespace core
