#include "hardware/button/button_params.h"

namespace button {

using core::TlmDef;
using core::TlmType;

// Split out from the system module because a user button is the one genuinely
// optional part of that original six-field telemetry frame -- plenty of boards
// have no button, and FEATURE_BUTTON 0 should remove the field from the wire
// and the UI, not report a stuck 0.
static const TlmDef kTlm[T_COUNT] = {
  // key    label     unit     type          div  dec  group
  {"btn",  "Button",  nullptr, TlmType::U32,  0,   0,  nullptr},
};

const core::ModuleDesc kDesc = {
  "button", "Button",
  nullptr, 0,
  kTlm, T_COUNT,
};

}  // namespace button
