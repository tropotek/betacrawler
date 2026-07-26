#pragma once
#include "core/module.h"

namespace st7789 {

extern const core::ModuleDesc kDesc;

// Parameter indices *within this module* -- what onParamChanged() receives.
enum : uint8_t { P_MODE = 0, P_PAGE = 1, P_RATE = 2 };

// Values of the disp.mode enum, in declaration order.
enum : int32_t { MODE_OFF = 0, MODE_ON = 1 };

// Values of the disp.page enum, in declaration order. CYCLE is not a page --
// it alternates INFO and STATS, so the driver keeps a separate "page currently
// shown" of its own rather than writing back into the parameter.
enum : int32_t { PAGE_INFO = 0, PAGE_STATS = 1, PAGE_CYCLE = 2 };

// How long CYCLE dwells on each page.
constexpr uint32_t kCycleMs = 5000;

}  // namespace st7789
