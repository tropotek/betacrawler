#pragma once
#include "core/module.h"

namespace device {

// The always-present module: identity and link settings that exist no matter
// what hardware a board has. It lives in core/ rather than features/ or
// hardware/ precisely because it is not optional and drives no hardware --
// there is no device_driver.cpp.
extern const core::ModuleDesc kDesc;

}  // namespace device
