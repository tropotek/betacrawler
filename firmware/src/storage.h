#pragma once
#include "core/dispatch.h"

class FlashStore : public core::Persistence {
 public:
  bool save(const core::Params& p) override;
  bool load(core::Params* p) override;
};
