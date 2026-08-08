# Modules — Overview

Every piece of behavior or hardware in betacrawler is a **module**: LED, Button, Servo, ESC, RX,
Display, WiFi. Modules are the unit a board turns on or off with one `FEATURE_*` flag, and the
unit whose parameters and telemetry flow automatically into the app with zero UI code.

## Two files per module, split on purpose

- **`<name>_params.cpp`** — the module's `ModuleDesc`: its parameters and telemetry fields. **No
  Arduino includes.** This is what makes the file compile in the native test environment with no
  board attached, which is how `pio test -e native` can assemble the *real* device's parameter
  and telemetry tables and diff them against `firmware/test/golden/schema.json` — a firmware
  schema change that isn't reflected there fails a test instead of drifting quietly.
- **`<name>_driver.cpp`** — the `core::Module` subclass that actually touches hardware. Board
  builds only; this is where pins, timers, and peripheral calls live.

A `ParamDef` never belongs in a driver file, and a driver never hardcodes a parameter's shape —
the params file is the single source of truth for what a module exposes.

## Module-local indices

A module always receives a **module-local** parameter index, never a global one.
`Module::globalParam(local)` maps back to the global index when one is genuinely needed. Inside
`dispatch.cpp` and `registry.cpp`, the invariant worth protecting is: exactly one hardware call,
on the owning module, with that module's own local index.

## Observers are const

`Module::attach()` hands out **const** access to other modules on purpose. A module must never
reconfigure another behind `dispatch`'s back. If a module needs to read another's state, it
resolves that dependency once in `attach()` — never per tick.

## Where the exact parameters live

This site deliberately does **not** list each module's exact parameter names, ranges, or
defaults — that would be a second list to keep in sync with the firmware schema by hand, and it
would drift the first time a default changes. The connected app's **Configuration** and
**Terminal** pages read the schema straight from the device, so they're always accurate. Use
those to see exactly what a module exposes today; use this site to understand *why* it's shaped
the way it is.

## The modules

- [LED](led.md) — status indication
- [Button](button.md) — user input
- [Servo](servo.md) — hobby servo output
- [ESC](esc.md) — brushless motor ESC output (two independent instances, `esc0`/`esc1`, sharing
  one math library)
- [RX](rx.md) — RC receiver input (CRSF / ExpressLRS)
- [Display](display.md) — ST7789 240x240 on-device dashboard
- [WiFi](wifi.md) — network connectivity (ESP-01 AT companion chip on STM32 boards, native WiFi
  on ESP32 boards)

Ready to write one yourself? See [Adding Your Own Module](adding-your-own-module.md).
