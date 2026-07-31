#pragma once
#include "core/module.h"
#include "core/inputs.h"

namespace core {

// Flattens the enabled modules into the global views the protocol layer needs
// (one parameter table, one telemetry field list) while routing callbacks back
// to the owning module with a module-local index.
//
// Everything is fixed-size and populated once at boot from src/modules.cpp;
// there is no removal and no allocation.
class Registry {
 public:
  // `driver` may be null: the native test build compiles every module's
  // descriptor but none of its Arduino driver code, so the real parameter and
  // telemetry tables are still assembled with nothing to drive them.
  // Returns false if a capacity limit in config.h would be exceeded.
  bool add(const ModuleDesc& desc, Module* driver = nullptr);

  // --- modules --------------------------------------------------------------
  uint8_t     moduleCount() const { return modCount_; }
  const char* moduleId(uint8_t i) const { return mods_[i].desc->id; }

  // --- parameters -----------------------------------------------------------
  uint8_t         paramCount() const { return paramCount_; }
  const ParamDef& paramDef(ParamId global) const;
  const char*     paramGroup(ParamId global) const;
  bool            findParam(const char* key, ParamId* out) const;

  // --- telemetry ------------------------------------------------------------
  uint8_t       tlmCount() const { return tlmCount_; }
  const TlmDef& tlmDef(uint8_t global) const;
  const char*   tlmGroup(uint8_t global) const;
  // Symmetric with findParam(): lets an observing module locate a telemetry
  // field by key instead of hardcoding an index that shifts whenever the
  // enabled module set changes.
  bool          findTlm(const char* key, uint8_t* out) const;

  // --- inputs -----------------------------------------------------------
  // The shared control-signal bus. modules.cpp's registerModules() sets
  // this once, pointing at the single core::Inputs instance it also wires
  // into rx's driver as a mutable reference -- Registry only ever hands out
  // the const side here, exactly like paramDef()/tlmDef() hand out
  // read-only views of state something else owns. A Registry nobody called
  // setInputs() on (every test in this file except the two above) falls
  // back to a static empty bus rather than a null dereference.
  void          setInputs(const Inputs& in) { inputs_ = &in; }
  const Inputs& inputs() const;

  // --- lifecycle ------------------------------------------------------------
  // Attaches every driver, then begins every driver -- two passes, so a
  // begin() can rely on all modules having been attached. Params is passed
  // through to attach() for modules that observe other modules' values.
  void begin(const Params& p);
  void tick(uint32_t nowMs);
  void notify(ParamId global, const Params& p);   // -> owner->onParamChanged(local, p)
  void collectTelemetry(TlmValue* out) const;     // fills tlmCount() entries

  // Polls every registered module's pollPush() in registration order and
  // returns the first non-zero result -- at most one module uses this
  // today, but the loop is generic so a second composes with no change
  // here. A module with no driver (the native test build's shape for any
  // module registered with driver=nullptr) is skipped, same as tick().
  size_t pollPush(char* out, size_t cap) const;

  // Identifies the parameter *layout* -- every key, type and bound, in order.
  // storage.cpp writes it into the flash header so that changing the enabled
  // module set (or editing a parameter's bounds) invalidates saved settings
  // instead of reinterpreting them against the wrong table. Deliberately does
  // not cover telemetry, which is never persisted.
  uint32_t fingerprint() const;

 private:
  struct Entry {
    const ModuleDesc* desc;
    Module*           driver;
    uint8_t           paramBase;   // global index of this module's first param
    uint8_t           tlmBase;
  };

  const Entry* ownerOfParam(ParamId global, uint8_t* local) const;

  Entry   mods_[FW_MAX_MODULES];
  uint8_t modCount_   = 0;
  uint8_t paramCount_ = 0;
  uint8_t tlmCount_   = 0;
  const Inputs* inputs_ = nullptr;
};

// Defined in src/modules.cpp -- the single wiring point where board #defines
// decide which modules exist. Declared here rather than in that file so both
// main.cpp and the native tests can call it.
void registerModules(Registry& reg);

}  // namespace core
