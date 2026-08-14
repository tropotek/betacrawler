#include "core/registry.h"
#include <string.h>

namespace core {

bool Registry::add(const ModuleDesc& desc, Module* driver) {
  if (modCount_ >= FW_MAX_MODULES) return false;
  if (paramCount_ + desc.paramCount > FW_MAX_PARAMS) return false;
  if (tlmCount_   + desc.tlmCount   > FW_MAX_TLM)    return false;

  if (driver) driver->bind(paramCount_);
  mods_[modCount_] = Entry{&desc, driver, paramCount_, tlmCount_};
  ++modCount_;
  paramCount_ += desc.paramCount;
  tlmCount_   += desc.tlmCount;
  return true;
}

const Registry::Entry* Registry::ownerOfParam(ParamId global, uint8_t* local) const {
  for (uint8_t i = 0; i < modCount_; ++i) {
    const Entry& e = mods_[i];
    if (global >= e.paramBase && global < e.paramBase + e.desc->paramCount) {
      *local = (uint8_t)(global - e.paramBase);
      return &e;
    }
  }
  return nullptr;
}

const ParamDef& Registry::paramDef(ParamId global) const {
  uint8_t local = 0;
  const Entry* e = ownerOfParam(global, &local);
  return e->desc->params[local];
}

const char* Registry::paramGroup(ParamId global) const {
  uint8_t local = 0;
  const Entry* e = ownerOfParam(global, &local);
  const char* g = e->desc->params[local].group;
  return g ? g : e->desc->label;
}

bool Registry::findParam(const char* key, ParamId* out) const {
  for (uint8_t i = 0; i < modCount_; ++i) {
    const Entry& e = mods_[i];
    for (uint8_t k = 0; k < e.desc->paramCount; ++k) {
      if (strcmp(e.desc->params[k].key, key) == 0) {
        *out = (ParamId)(e.paramBase + k);
        return true;
      }
    }
  }
  return false;
}

// Callers iterate 0..tlmCount(), so the fallback is unreachable -- but it
// returns a valid, inert descriptor rather than dereferencing a module that
// may well have no telemetry at all, so a future out-of-range caller gets an
// empty card instead of undefined behaviour.
static const TlmDef kNoTlm = {"", "", nullptr, TlmType::U32, 0, 0, nullptr, nullptr};

const TlmDef& Registry::tlmDef(uint8_t global) const {
  for (uint8_t i = 0; i < modCount_; ++i) {
    const Entry& e = mods_[i];
    if (global >= e.tlmBase && global < e.tlmBase + e.desc->tlmCount)
      return e.desc->tlm[global - e.tlmBase];
  }
  return kNoTlm;
}

const char* Registry::tlmGroup(uint8_t global) const {
  for (uint8_t i = 0; i < modCount_; ++i) {
    const Entry& e = mods_[i];
    if (global >= e.tlmBase && global < e.tlmBase + e.desc->tlmCount) {
      const char* g = e.desc->tlm[global - e.tlmBase].group;
      return g ? g : e.desc->label;
    }
  }
  return "";
}

bool Registry::findTlm(const char* key, uint8_t* out) const {
  for (uint8_t i = 0; i < modCount_; ++i) {
    const Entry& e = mods_[i];
    for (uint8_t k = 0; k < e.desc->tlmCount; ++k) {
      if (strcmp(e.desc->tlm[k].key, key) == 0) {
        *out = (uint8_t)(e.tlmBase + k);
        return true;
      }
    }
  }
  return false;
}

const Inputs& Registry::inputs() const {
  static const Inputs kEmptyInputs;
  return inputs_ ? *inputs_ : kEmptyInputs;
}

const Inputs& Registry::driveOutputs() const {
  static const Inputs kEmptyDriveOutputs;
  return driveOutputs_ ? *driveOutputs_ : kEmptyDriveOutputs;
}

void Registry::begin(const Params& p) {
  // Two passes on purpose: every module is attached before any module begins,
  // so a driver's begin() can act on state another module published.
  for (uint8_t i = 0; i < modCount_; ++i)
    if (mods_[i].driver) mods_[i].driver->attach(*this, p);
  for (uint8_t i = 0; i < modCount_; ++i)
    if (mods_[i].driver) mods_[i].driver->begin();
}

void Registry::tick(uint32_t nowMs) {
  for (uint8_t i = 0; i < modCount_; ++i)
    if (mods_[i].driver) mods_[i].driver->tick(nowMs);
}

size_t Registry::pollPush(char* out, size_t cap) const {
  for (uint8_t i = 0; i < modCount_; ++i) {
    if (!mods_[i].driver) continue;
    size_t n = mods_[i].driver->pollPush(out, cap);
    if (n > 0) return n;
  }
  return 0;
}

void Registry::notify(ParamId global, const Params& p) {
  uint8_t local = 0;
  const Entry* e = ownerOfParam(global, &local);
  if (e && e->driver) e->driver->onParamChanged(local, p);
}

void Registry::collectTelemetry(TlmValue* out) const {
  for (uint8_t i = 0; i < modCount_; ++i) {
    const Entry& e = mods_[i];
    if (e.desc->tlmCount == 0) continue;
    if (e.driver) {
      e.driver->readTelemetry(out + e.tlmBase);
    } else {
      // No driver (native build): publish a defined zero rather than whatever
      // was on the stack, so a descriptor-only build still serializes.
      for (uint8_t k = 0; k < e.desc->tlmCount; ++k) out[e.tlmBase + k].u = 0;
    }
  }
}

uint32_t Registry::fingerprint() const {
  // FNV-1a. Not a security hash -- it only has to change when the layout
  // changes, and it is compared against a value written by the same build.
  uint32_t h = 2166136261u;
  auto mix = [&h](const void* data, size_t n) {
    const uint8_t* b = (const uint8_t*)data;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
  };
  auto mixStr = [&mix](const char* s) { mix(s, strlen(s) + 1); };

  for (uint8_t i = 0; i < paramCount_; ++i) {
    const ParamDef& d = paramDef((ParamId)i);
    mixStr(d.key);
    uint8_t t = (uint8_t)d.type;
    mix(&t, sizeof(t));
    // Bounds are included because widening a range changes what a saved value
    // is allowed to mean, even though the byte layout is untouched.
    mix(&d.minVal, sizeof(d.minVal));
    mix(&d.maxVal, sizeof(d.maxVal));
    mix(&d.optionCount, sizeof(d.optionCount));
    mix(&d.maxLen, sizeof(d.maxLen));
  }
  return h;
}

}  // namespace core
