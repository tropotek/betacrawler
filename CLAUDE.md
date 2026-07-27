# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Betaflight-Configurator-style tool for an STM32 Black Pill (STM32F411CE): firmware exposes
device config/telemetry over USB serial as JSON lines, a Python/FastAPI backend bridges that to
HTTP+WebSocket, and a static Bootstrap web UI drives it. **Project 1 ("configurator core")** and
**Project 2 (in-app DFU flashing)** are both done.

`_notes/todo.md` is the live document — what's next, and a Done list of what shipped. Read it
first in any new session. `docs/api.md` is the HTTP/WS contract, and this file describes the
architecture.

**`_notes/_archive/` is history, not documentation.** It holds superseded specs, implementation
plans and the retired `progress.md` status doc. Read it when you need the *reasoning* behind a
past decision — the specs there are approved designs, so don't relitigate them without a real
reason — but **never update anything in it**, and don't treat it as a description of the code as
it stands today. Where the archive and the code disagree, the code is right.

## Commands

`pio` is **not on PATH** — always invoke it as `~/.platformio/penv/bin/pio`.

**Firmware** (from `firmware/`):
```
~/.platformio/penv/bin/pio test -e native              # 93 tests, no board needed
~/.platformio/penv/bin/pio test -e native -f test_dispatch   # one suite only
~/.platformio/penv/bin/pio run -e blackpill_f411ce      # compile for the real board
~/.platformio/penv/bin/pio run -e blackpill_f411ce -t upload  # flash it (ST-Link/SWD)
~/.platformio/penv/bin/pio device monitor -b 115200     # raw serial console
```
Two ST-Link/V2 units may be attached at once — if upload grabs the wrong one, add
`upload_port = <device>` to `[env:blackpill_f411ce]`.

**Backend** (from `app/`, venv already at `app/.venv/`):
```
.venv/bin/pytest -v                                     # 170 tests, no board needed
.venv/bin/pytest tests/test_link.py -v                  # one file only
.venv/bin/uvicorn backend.main:app --port 8080           # serves API + app/web/ together
```

**Firmware bundle** (from the repo root), after any firmware change worth shipping. `app/firmware/`
is gitignored build output, so a fresh checkout has none until this is run:
```
python3 app/tools/bundle_firmware.py                    # builds, then updates app/firmware/
python3 app/tools/bundle_firmware.py board_a board_b    # the whole release set, in one go
python3 app/tools/bundle_firmware.py --add other_board  # merge, don't prune the rest
python3 app/tools/bundle_firmware.py --dry-run          # report only
```
Also available as the **Build release firmware** task in `silkscreen.code-workspace`.

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
                        led_curve, tlm_format, boot_log, version, device_params. Native-tested (Unity).
firmware/src/features/ behaviours, one folder per module (led/)
firmware/src/hardware/ device drivers, one folder per module (system/, button/, st7789_240x240/;
                        WiFi and other peripherals go here)
firmware/src/modules.cpp  THE wiring file — one #if block per module. Compiled by BOTH envs.
firmware/src/          Arduino glue: main.cpp, storage.cpp (flash)

app/backend/           protocol.py (codec) -> link.py (threaded serial, id correlation) ->
                        device.py (schema cache + validation) -> main.py (FastAPI routes/WS).
                        terminal.py parses the Terminal page's commands; settings_ini.py is a
                        pure INI codec for settings backup/restore (no device, no coercion);
                        firmware.py is the bundle catalog + dfu-util wrapper.
                        pytest-tested against a fake serial port (app/tests/fake_serial.py),
                        no board needed
app/firmware/          the firmware images this app version ships with, plus manifest.json.
                        GITIGNORED build output — produced by the script below, not committed.
app/tools/             bundle_firmware.py, run by hand at release time to produce the above

app/web/                static HTML/JS only, talks to the backend exclusively through the
                        `Api` object in app.js — that object is the ENTIRE porting surface
                        for a hypothetical future Electron rewrite. Never call fetch/WebSocket
                        directly outside it.
```

**The `Api` seam has rules, and they are cheap to keep and expensive to retrofit.** Beyond "no
`fetch`/`WebSocket` outside it": no `Api` method may take or return a browser-only type (a `File`,
a `Blob`, the `WebSocket` itself) — bytes, strings, plain objects and callbacks only; every path
goes through `Api.base` so nothing assumes an origin; and the push channel stays
`Api.subscribe(handler) -> unsubscribe`, owning its own reconnection, rather than handing a socket
back to a caller that would touch `.onclose`. All three were audited and fixed on 2026-07-27 —
`flashUpload` really did take a `File`, because `fetch` accepts one, which is exactly how this
kind of coupling gets in. Full contract, a grep check for the mechanical part, and the port
assessment: `_notes/review-electron-port-readiness.md`.

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
`_notes/_archive/spec-firmware-modules.md`.

**The hardware/persistence seam**: `core/` never touches a GPIO pin or a flash write directly.
It talks through `core::Module` (one per module — `onParamChanged`/`tick`/`readTelemetry`, in
`core/module.h`) and `Persistence::save()/load()` (`core/dispatch.h`), which `main.cpp` wires to
real Arduino/EEPROM code. Native tests inject fake modules, so "setting a parameter produced
exactly one hardware call, on the owning module, with that module's own local index" is provable
with no board attached. This is the one invariant most worth preserving if you touch
`dispatch.cpp` or `registry.cpp`. Modules always receive a **module-local** index, never a global
one — `Module::globalParam(local)` maps back when a driver needs to read a value.

**Boot health**: `core/boot_log.h` holds a fixed buffer of lines recorded during `setup()` —
firmware identity, whether saved settings survived the fingerprint check, module/param/telemetry
counts, free RAM, plus whatever modules add (the display contributes its init timing). `main.cpp`
emits it at the end of boot *and again after every `hello`*. The replay is the point: USB CDC
enumerates well after `setup()` runs and the app connects later still, so a line merely printed at
boot reaches nobody. `hello`'s response shape is deliberately unchanged — the record follows it as
separate unsolicited `{"log":...}` lines, which `app.js` renders in the Terminal as `[device] …`.

**Observing modules** use `Module::attach(const Registry&, const Params&)`, called on every module
before any module's `begin()`. It exists for modules that must *read* the rest of the device — the
display is the only one so far. Access is const on purpose: an observer may look at the device, but
must never reconfigure it behind `dispatch`'s back, which would skip validation and the change
notification everything else depends on. Resolve keys there once (`findParam`/`findTlm`), never per
tick — the registry is fixed after boot. Most modules override nothing and are unaffected.

**The display is deliberately the exception to schema-driven rendering.** `app.js` builds itself
entirely from the descriptor; the on-device dashboard (`hardware/st7789_240x240/`) uses a *curated* layout
instead, because it must fit 240x240 exactly and a board may want to show things the registry knows
nothing about. The coupling that buys is contained — every key is resolved once in `attach()` and a
key the board doesn't publish drops its row — but it does mean `st7789_240x240_driver.cpp` is the one file
outside a module's own folder that names other modules' keys. Values still render through
`core::formatTlm()` from each field's own `TlmDef`, so the panel and the browser cannot disagree
about what a telemetry frame says. **Nothing detects whether a panel is physically connected** —
there is no MISO to read a controller ID back over, and `Arduino_HWSPI::begin()` returns true
unconditionally, so never treat `gfx->begin()`'s bool as a presence check. The driver is write-only
and therefore cannot block the loop when the panel is absent; it emits a truthful startup `log`
line instead of a fabricated warning. Full detail: `_notes/_archive/spec-display.md`.

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
mismatch.

**Three states, three buttons.** A device's parameters can be at factory defaults, at what is
stored in flash, or at whatever RAM currently holds — and each Configuration button reaches
exactly one:

| Button | Direction | Dirty after |
|---|---|---|
| Save to flash | RAM → flash | no |
| Discard changes | flash → RAM (`revert` op) | no — RAM now equals flash |
| Load defaults | factory → RAM | yes, deliberately |

`revert` is the only op that reads the `Persistence::load()` seam back; before it, that seam was
called solely by `main.cpp` at boot, so flash was write-only from the host's point of view. It
falls back to defaults when nothing valid is stored and reports `src` so the host can say so —
that field is also what decides the dirty flag, since the fallback case *does* leave something
worth writing. Full detail: `_notes/spec-config-revert.md`.

The fingerprint (`Registry::fingerprint()`) hashes every parameter's key, type and
bounds, so changing the enabled module set — or a parameter's range — discards saved settings
rather than reinterpreting stored bytes against a different table.
`storage.cpp`'s `save()` does a read-back verification after the flush so a real flash failure has
an actual way to report `{"err":"flash"}` instead of that path being dead code.

**In-app firmware updates (Project 2).** The app ships the firmware that matches it: built
images live in `app/firmware/` with a `manifest.json`, produced by
`app/tools/bundle_firmware.py` at release time (it builds first, then derives every manifest
field from the sources and the binary — nothing is typed in). A file picker survives only as a
collapsed *Advanced* path, where a vector-table check is all that stands between picking
`firmware.elf` out of `.pio/build` and a board that no longer enumerates.

**`app/firmware/` is gitignored build output, not source — that is the current, deliberate
decision, reversed on 2026-07-27 from the opposite one.** The binaries used to be committed so
that app/firmware pairing was a checked-in fact; they now aren't, because the folder is what a
release *produces* on the way to a packaged executable, and committing a binary per firmware
change per board does not scale past one board. The pairing guarantee did not go away, it moved:
the script is the only thing that writes there, and it derives every field from the tree it just
built. The manifest is ignored alongside the binaries on purpose — a committed manifest whose
`sha256` fields describe absent files is precisely the drift the script exists to prevent.
**So: don't re-commit them, and don't "fix" the `.gitignore` entry.** A source checkout having
no firmware until someone runs the script is the expected state, and only a developer ever sees
it; a packaged app is built after the script has run.

A multi-board run is **all-or-nothing**: `plan_entry()` builds and validates every env before
`release()` writes anything, so a second board failing to compile cannot leave a manifest that
looks like a complete release and isn't. By default the manifest describes exactly the envs named
in that one command and images from a previous run are pruned; `--add` merges instead. Pruning
only ever deletes files a *previous manifest listed* — never a wildcard sweep of the directory,
which would take a binary someone had put there by hand. `app/tests/test_bundle_firmware.py`
covers those invariants against a fixture tree with the `pio run` call injected out.

Getting into DFU has two paths, and **both must keep working**: the `dfu` wire op (one click) and
BOOT0+NRST by hand (for a board whose firmware is broken). That is also why the Firmware page,
like the Terminal, is *not* in `CONNECTION_REQUIRED_PAGES` — gating the recovery tool on a working
device is exactly backwards. Two orderings in the firmware are load-bearing and easy to
"simplify" into bugs: `Bootloader::enterDfu()` only **arms** the reboot so `main.cpp` can flush
the response first (otherwise the host cannot tell a reboot from a dead board), and `initVariant()`
**clears the RTC magic before jumping** (otherwise a failed jump is an unrecoverable boot loop).
`src/dfu.cpp` is Arduino glue beside `storage.cpp`, not a module — it has no params and no
telemetry, so the registry would buy it nothing. Full detail: `_notes/spec-dfu-upload.md`.

**Nothing can identify a board in DFU mode** — every STM32F4 bootloader reports `0483:df11` and
nothing else. The app carries the `board` string forward from the last `hello` and says so plainly
when it has none, rather than guessing. Any future "auto-detect the right firmware" idea runs into
this wall first.

**Disconnect detection has two independent layers**: the backend's `SerialLink` detects OS-level
port loss (unplug) immediately. The frontend watchdog additionally declares a distinct "stale"
badge state if telemetry hasn't arrived in 3x the configured interval while the port is still
OS-connected — catches a wedged-but-still-enumerated board. The 3x threshold is deliberate slack:
a `save`'s ~1s flash stall must never look like a disconnect.
