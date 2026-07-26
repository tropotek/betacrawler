# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Betaflight-Configurator-style tool for an STM32 Black Pill (STM32F411CE): firmware exposes
device config/telemetry over USB serial as JSON lines, a Python/FastAPI backend bridges that to
HTTP+WebSocket, and a static Bootstrap web UI drives it. This is **Project 1 ("configurator
core")** of a larger effort — DFU flashing (Project 2) has not been started.

Read `_notes/progress.md` first in any new session — it's the living status doc and says exactly
what's done and what's next. `_notes/spec-firmware-modules.md` is the current firmware design
(module registry, board configs, versioning); `_notes/_archive/spec-configurator-core.md` is the
original approved design for the configurator itself. Both are approved designs (don't
relitigate it without a real reason). `docs/api.md` is the HTTP/WS contract.

## Commands

`pio` is **not on PATH** — always invoke it as `~/.platformio/penv/bin/pio`.

**Firmware** (from `firmware/`):
```
~/.platformio/penv/bin/pio test -e native              # 51 tests, no board needed
~/.platformio/penv/bin/pio test -e native -f test_dispatch   # one suite only
~/.platformio/penv/bin/pio run -e blackpill_f411ce      # compile for the real board
~/.platformio/penv/bin/pio run -e blackpill_f411ce -t upload  # flash it (ST-Link/SWD)
~/.platformio/penv/bin/pio device monitor -b 115200     # raw serial console
```
Two ST-Link/V2 units may be attached at once — if upload grabs the wrong one, add
`upload_port = <device>` to `[env:blackpill_f411ce]`.

**Backend** (from `app/`, venv already at `app/.venv/`):
```
.venv/bin/pytest -v                                     # 89 tests, no board needed
.venv/bin/pytest tests/test_link.py -v                  # one file only
.venv/bin/uvicorn backend.main:app --port 8080           # serves API + app/web/ together
```

**Web UI**: no build step. `app/web/{index.html,app.js}` are static files served directly by the
FastAPI app above (`main.py` mounts `app/web/` at `/`). Manual verification only — no automated
UI tests exist by design, and no browser/headless-render tooling is available in this environment
(Firefox `--headless --screenshot` hangs on framebuffer mapping here).

**Environment facts already resolved — don't redo this work:**
- udev/permissions are already correct system-wide (PlatformIO's own `0483` vendor rule covers
  VCP/ST-Link/DFU and tells ModemManager to ignore them). No `dialout` group, no custom udev rules.
- System Python is 3.14.4 and all backend deps install and import cleanly on it.
- Git commits use `feat:`/`fix:`/`docs:`/`chore:` prefixes.

## Architecture

**Three tiers, deliberately isolated so most of the stack tests without hardware:**

```
firmware/include/      config.h (project name/version/baud/capacity limits) and
                        boards/<board>.h (FEATURE_* flags + pin map). Selected per-env by
                        -D BOARD_HEADER; _template.h documents adding a board.
firmware/src/core/     pure C++, zero Arduino — protocol, params, registry, dispatch,
                        led_curve, version, device_params. Native-tested (Unity).
firmware/src/features/ behaviours, one folder per module (led/)
firmware/src/hardware/ device drivers, one folder per module (system/, button/; OLED/WiFi go here)
firmware/src/modules.cpp  THE wiring file — one #if block per module. Compiled by BOTH envs.
firmware/src/          Arduino glue: main.cpp, storage.cpp (flash)

app/backend/           protocol.py (codec) -> link.py (threaded serial, id correlation) ->
                        device.py (schema cache + validation) -> main.py (FastAPI routes/WS).
                        terminal.py parses the Terminal page's commands; settings_ini.py is a
                        pure INI codec for settings backup/restore (no device, no coercion).
                        pytest-tested against a fake serial port (app/tests/fake_serial.py),
                        no board needed

app/web/                static HTML/JS only, talks to the backend exclusively through the
                        `Api` object in app.js — that object is the ENTIRE porting surface
                        for a hypothetical future Electron rewrite. Never call fetch/WebSocket
                        directly outside it.
```

**Modules are the core idea.** A board header's `FEATURE_*` flags decide what compiles in;
`src/modules.cpp` registers those modules into a `core::Registry` at boot, which flattens their
parameters into one table and their telemetry fields into one frame. `core/` never names a
feature. Each module is split in two:

- `<name>_params.cpp` — its `ModuleDesc` (id, label, `ParamDef[]`, `TlmDef[]`). **Zero Arduino
  includes**, because the native build compiles it.
- `<name>_driver.cpp` — the `core::Module` subclass that touches hardware. Board builds only.

That split is load-bearing, not stylistic: it is why `pio test -e native` can assemble the *real*
device's schema and keep `test/golden/schema.json` honest with no board attached. Don't put a
`ParamDef` in a driver file. Full recipes for adding a module or a board:
`_notes/spec-firmware-modules.md`.

**The hardware/persistence seam**: `core/` never touches a GPIO pin or a flash write directly.
It talks through `core::Module` (one per module — `onParamChanged`/`tick`/`readTelemetry`, in
`core/module.h`) and `Persistence::save()/load()` (`core/dispatch.h`), which `main.cpp` wires to
real Arduino/EEPROM code. Native tests inject fake modules, so "setting a parameter produced
exactly one hardware call, on the owning module, with that module's own local index" is provable
with no board attached. This is the one invariant most worth preserving if you touch
`dispatch.cpp` or `registry.cpp`. Modules always receive a **module-local** index, never a global
one — `Module::globalParam(local)` maps back when a driver needs to read a value.

**Build gotcha, already fixed — don't undo it:** `config.h` reaches the board header via
`#include BOARD_HEADER`, a macro-expanded include SCons cannot resolve, so board-header edits did
not trigger rebuilds (verified: toggling `FEATURE_LED` produced a byte-identical binary and
reported success). `firmware/scripts/config_hash.py` folds a hash of `include/**/*.h` into a
`-D FW_CONFIG_HASH` so any config edit forces a rebuild. Both envs reference it via
`extra_scripts`; removing that line silently reintroduces stale-binary builds.

**Wire protocol**: one JSON object per line, `\n`-terminated, over USB CDC serial (115200).
Requests carry an `id`; responses echo it. Messages with no `id` are unsolicited (telemetry, log)
— that's what lets push telemetry interleave safely with request/response on one connection.
Firmware validates every input independently of the backend (never trusts the host). Full spec in
`_notes/_archive/spec-configurator-core.md`; live contract in `docs/api.md`.

**Schema-driven UI**: the registered modules' descriptors are the single source of truth — for the
config form *and* the telemetry page. The `schema` wire op serializes both (`{params, tlm}`);
`DeviceModel` (backend) caches that response and validates `set()` calls against it before ever
touching the wire; `app.js` builds the config controls and the telemetry cards from it at runtime,
grouped by each item's `group` (defaulting to the owning module's label). Adding a firmware
parameter or telemetry field should need zero changes in `app.js` — if it doesn't, something has
drifted from that design. There is deliberately **no** field-label map or field-order table left
in `app.js`; display order is the firmware's module registration order.

Telemetry `div`/`dec` in the descriptor are display hints only: the wire always carries the
device's native units (`vdd` is integer millivolts) and only the browser divides and rounds.
`firmware/test/golden/schema.json` is a checked-in fixture, regenerated and diffed by a native
test (`test_schema_golden_fixture_matches_firmware`) and loaded directly by the Python tests, so a
firmware schema change that isn't reflected there fails a test instead of drifting silently.

**Versioning**: firmware and app are separate projects with independent version numbers that are
not meant to track each other. Firmware: `FW_VERSION` in `firmware/include/config.h`, reported
over the wire by `hello` (`name`/`ver`/`built`/`mods`, alongside the unchanged `fw` display
string). App (backend + web UI): `APP_VERSION` at the top of `app/web/app.js`. **Both stay 1.0.0
in this template** — bumps happen in real forked projects, not here.

**Persistence discipline**: the F411 has no real EEPROM (flash-emulated), and an erase stalls the
MCU ~1s. Values apply to RAM/hardware instantly on `set`; flash is written **only** on explicit
`save`, guarded by a magic/version/**fingerprint**/CRC header that falls back to defaults on any
mismatch. The fingerprint (`Registry::fingerprint()`) hashes every parameter's key, type and
bounds, so changing the enabled module set — or a parameter's range — discards saved settings
rather than reinterpreting stored bytes against a different table.
`storage.cpp`'s `save()` does a read-back verification after the flush so a real flash failure has
an actual way to report `{"err":"flash"}` instead of that path being dead code.

**Disconnect detection has two independent layers**: the backend's `SerialLink` detects OS-level
port loss (unplug) immediately. The frontend watchdog additionally declares a distinct "stale"
badge state if telemetry hasn't arrived in 3x the configured interval while the port is still
OS-connected — catches a wedged-but-still-enumerated board. The 3x threshold is deliberate slack:
a `save`'s ~1s flash stall must never look like a disconnect.
