# Adding Your Own Module

A worked walkthrough of adding a new module, using a minimal example: a module called `beeper`
that drives a piezo buzzer with one on/off parameter. Substitute your own hardware and
parameters — the *shape* of the seven steps below is what matters, not this specific example.

## 1. Choose the folder

- **Behavior with no hardware of its own** (reacts to other modules' state, computes something) →
  `firmware/src/features/<name>/`
- **A peripheral driver** (this is `beeper`'s case — it owns a GPIO pin) →
  `firmware/src/hardware/<name>/`

```bash
mkdir -p firmware/src/hardware/beeper
```

## 2. Write `<name>_params.h` and `<name>_params.cpp` first

This declares the module's `ModuleDesc` — its parameters and telemetry fields — and has **zero
Arduino includes**. Write and native-test this before touching any hardware code. Follow
`firmware/src/features/led/led_params.{h,cpp}` as the reference pattern:

```cpp
// firmware/src/hardware/beeper/beeper_params.h
#pragma once
#include "core/module.h"

namespace beeper {

extern const core::ModuleDesc kDesc;

// Parameter indices *within this module* -- what onParamChanged() receives.
enum : uint8_t { P_ENABLED = 0 };

}  // namespace beeper
```

```cpp
// firmware/src/hardware/beeper/beeper_params.cpp
#include "hardware/beeper/beeper_params.h"

namespace beeper {

using core::ParamDef;
using core::ParamType;

// ParamType has three variants: U8 (a bounded integer -- 0/1 doubles as a
// bool), Str, and Enum. There is no dedicated Bool type.
static const ParamDef kParams[] = {
  // key              type            label      unit min max opts    n  maxlen def defStr group
  {"beeper.enabled",  ParamType::U8,  "Enabled",  nullptr, 0, 1, nullptr, 0, 0, 0, nullptr, nullptr},
};

const core::ModuleDesc kDesc = {
  "beeper", "Beeper",
  kParams, (uint8_t)(sizeof(kParams) / sizeof(kParams[0])),
  nullptr, 0,     // no telemetry -- its state is a parameter, same as led.mode
};

}  // namespace beeper
```

## 3. Write `<name>_driver.{h,cpp}`

This is the `core::Module` subclass that touches hardware — board builds only. Follow
`firmware/src/features/led/led_driver.{h,cpp}` as the reference pattern:

```cpp
// firmware/src/hardware/beeper/beeper_driver.h
#pragma once
#include "hardware/beeper/beeper_params.h"

namespace beeper {

// Requires BEEPER_PIN from the board header.
class BeeperDriver : public core::Module {
 public:
  void begin() override;
  void onParamChanged(uint8_t local, const core::Params& p) override;

 private:
  void apply(bool on);
};

}  // namespace beeper
```

```cpp
// firmware/src/hardware/beeper/beeper_driver.cpp
#include <Arduino.h>
#include "hardware/beeper/beeper_driver.h"

#ifndef BEEPER_PIN
#error "FEATURE_BEEPER is on but the board header defines no BEEPER_PIN"
#endif

namespace beeper {

void BeeperDriver::begin() {
  pinMode(BEEPER_PIN, OUTPUT);
}

void BeeperDriver::apply(bool on) {
  digitalWrite(BEEPER_PIN, on ? HIGH : LOW);
}

void BeeperDriver::onParamChanged(uint8_t local, const core::Params& p) {
  (void)local;
  apply(p.num(globalParam(P_ENABLED)) != 0);
}

}  // namespace beeper
```

The parameter is read through `globalParam(P_ENABLED)` — the module-local index `P_ENABLED`
translated to wherever this module's slice landed in the global table — never a hardcoded global
index. See [Modules — Overview](overview.md#module-local-indices) for why that indirection
exists.

## 4. Wire it into `modules.cpp`

`firmware/src/modules.cpp` is the one file with an `#if` block per module, using a `_DRV` macro
so the driver only exists in the Arduino build (the native test build registers the same
descriptor with a null driver):

```cpp
#if FEATURE_BEEPER
#  include "hardware/beeper/beeper_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/beeper/beeper_driver.h"
     static beeper::BeeperDriver g_beeper;
#    define BEEPER_DRV (&g_beeper)
#  else
#    define BEEPER_DRV nullptr
#  endif
#endif
```

Then, in the function further down that file where every other module calls `reg.add(...)`, add:

```cpp
#if FEATURE_BEEPER
  reg.add(beeper::kDesc, BEEPER_DRV);
#endif
```

## 5. Add the feature flag and pin to your board header

```c
#define FEATURE_BEEPER  1
#define BEEPER_PIN      PA8   // pick a pin genuinely free on your board
```

Cross-check against every other `#define …_PIN` in your board header before picking one — nothing
catches a pin collision for you at compile time.

## 6. Run the native tests

```bash
cd firmware
~/.platformio/penv/bin/pio test -e native
```

This compiles your `_params.cpp` against the real board header (no Arduino, no hardware needed)
and diffs the assembled schema against `firmware/test/golden/schema.json`. It will fail the first
time, telling you the golden file needs updating — that's expected for a genuinely new parameter,
not a bug to work around.

## 7. Build for the board and verify in the app

```bash
~/.platformio/penv/bin/pio run -e blackpill_f411ce -t upload
```

Connect with the backend running — `beeper.enabled` should now appear on the Configuration page
with **no `app.js` changes**. If you find yourself needing to touch `app.js` to make a new
parameter show up correctly, something about the module's declaration has drifted from the
schema-driven design — see
[Wire protocol and the schema-driven UI](../architecture.md#wire-protocol-and-the-schema-driven-ui)
in the architecture reference before working around it.
