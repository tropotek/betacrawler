#pragma once
#include "core/dispatch.h"

// Flash-emulated EEPROM. Holds the registry reference so it can stamp the
// parameter-layout fingerprint into the header -- see storage.cpp.
class FlashStore : public core::Persistence {
 public:
  explicit FlashStore(const core::Registry& reg) : reg_(reg) {}

  bool save(const core::Params& p) override;
  bool load(core::Params* p) override;

 private:
  const core::Registry& reg_;
};
