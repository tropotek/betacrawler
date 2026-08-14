# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.
It documents development procedures and practices only — build/test commands, layout, and rules
that must not be undone. It is not a memory file: decision history, rationale, and narrative belong
in `docs/architecture.md`, `CHANGELOG.md`, or commit messages, not here. Keep this file minimal.

## What this is

A Betaflight-Configurator-style tool for an STM32 Black Pill (STM32F411CE): firmware exposes
device config/telemetry over USB serial as JSON lines, a Python/FastAPI backend bridges that to
HTTP+WebSocket, and a static Bootstrap web UI drives it.

| Where | What |
|---|---|
| `_notes/todo.md` | the live document — what's next. Read it first in any new session |
| `CHANGELOG.md` | what has shipped, one summary per change |
| `docs/architecture.md` | the reasoning behind every rule below — read the relevant section before changing that area |
| `docs/api.md` | the HTTP/WS contract |
| `_notes/_archive/` | history, not documentation (see below) |

**`_notes/_archive/` holds superseded specs, implementation plans and the retired `progress.md`.**
Read it for the *reasoning* behind a past decision — those specs are approved designs, so don't
relitigate them without a real reason — but **never update anything in it**, and don't read it as
a description of the code today. Where it and the code disagree, the code is right.

**Specs and plans (brainstorming/writing-plans skill output) are never committed.** `_notes/` is
gitignored; write new specs/plans under it (e.g. `_notes/_archive/superpowers/`), not `docs/` —
`docs/` is user-facing project documentation only, and anything placed there gets committed.

## Commands

`pio` is **not on PATH** — always invoke it as `~/.platformio/penv/bin/pio`.

**Firmware** (from `firmware/`):
```
~/.platformio/penv/bin/pio test -e native                     # no board needed
~/.platformio/penv/bin/pio test -e native -f test_dispatch    # one suite only
~/.platformio/penv/bin/pio run -e blackpill_f411ce            # compile for the real board
~/.platformio/penv/bin/pio run -e blackpill_f411ce -t upload  # flash it (ST-Link/SWD)
~/.platformio/penv/bin/pio device monitor -b 115200           # raw serial console
```
Two ST-Link/V2 units may be attached at once — if upload grabs the wrong one, add
`upload_port = <device>` to `[env:blackpill_f411ce]`.

ESP32 (`esp32_wroom32`) is not a supported build target at this time — its `[env:]` block was
removed from `platformio.ini`. `boards/esp32_wroom32.h` and `hardware/wifi/wifi_esp32_driver.cpp`
are left in place as unused dead code, so support is cheap to resurrect later; see
`firmware/platformio.ini`'s comment where the env used to be.

**Backend** (from `app/`, venv already at `app/.venv/`):
```
.venv/bin/pytest -v                                      # no board needed
.venv/bin/pytest tests/test_link.py -v                   # one file only
.venv/bin/uvicorn backend.main:app --port 8080           # serves API + app/web/ together
```

Building and bundling release firmware images (`app/firmware/`) is covered by the
`bundle-firmware` skill — invoke it rather than reading this file for those steps.

**Web UI**: no build step. `app/web/{index.html,app.js}` are static files served directly by the
FastAPI app above (`main.py` mounts `app/web/` at `/`). No automated UI test suite exists, by
design — but "therefore you cannot check the UI" does not follow, and believing it has cost real
defects. **A working headless browser is installed at `~/.pwvenv`** (Playwright + Chromium):

```
~/.pwvenv/bin/python3 script.py    # sync_playwright(), p.chromium.launch(headless=True)
```

Use it to read rendered text, click through a page and collect console/`pageerror` events before
claiming a UI change works. Two techniques, both already used here:

- Point it at the real server against the **real board** for the normal path.
- For a state the hardware cannot be made to produce, build the app around a fake device —
  `create_app(DeviceModel(SerialLink(open_port=lambda p: FakeSerial(responder=...))))` from
  `app/tests/`, served with uvicorn on its own port. That is how the `revert` fallback branch was
  verified: no real board reports it without a corrupt flash record.

Do NOT try apt's `python3-playwright` (its client and the packaged Node driver speak incompatible
protocol versions) or `firefox --headless --screenshot` (hangs on framebuffer mapping here).

**Environment facts already resolved — don't redo this work:**
- udev/permissions are already correct system-wide (PlatformIO's own `0483` vendor rule covers
  VCP/ST-Link/DFU and tells ModemManager to ignore them). No `dialout` group, no custom udev rules.
- System Python is 3.14.4 and all backend deps install and import cleanly on it.
- Git commits use `feat:`/`fix:`/`docs:`/`chore:` prefixes.

## Code style

- Comments stay strictly minimal: 2-3 lines describing what the code does and, optionally, its
  config options. No historic reasoning, no change explanations, no comments written to help a
  future reader when the code itself is readable — the code is the source of truth.
- Never reference a line number or a spec/design-doc file in a comment — both go stale the moment
  either file changes. If the reasoning matters, it belongs in the commit message or PR
  description, not inline.
- Comments and documentation (`docs/architecture.md`, `docs/api.md`, this file) describe the
  project as it currently is, never as a narrative of what changed — no "no longer exists", "used
  to be", "the old X page", "retired". `CHANGELOG.md` is the one place that history belongs; a
  living doc a reader hits later has no time context for a change narrative. This holds even more
  before a first real release, when there is no shipped history for a reader to already know.

## Layout

**Three tiers, deliberately isolated so most of the stack tests without hardware:**

```
firmware/include/      config.h (project name/version/baud/capacity limits) and
                        boards/<board>.h (FEATURE_* flags + pin map). Selected per-env by
                        -D BOARD_HEADER; _template.h documents adding a board.
firmware/src/core/     pure C++, zero Arduino — protocol, params, registry, dispatch,
                        led_curve, tlm_format, boot_log, version, device_params. Native-tested (Unity).
firmware/src/features/ behaviours, one folder per module (led/)
firmware/src/hardware/ device drivers, one folder per module (system/, button/, servo/, rx/,
                        st7789_240x240/; WiFi and other peripherals go here)
firmware/src/modules.cpp  THE wiring file — one #if block per module. Compiled by BOTH envs.
firmware/src/          Arduino glue: main.cpp, storage.cpp (flash), dfu.cpp
firmware/docs/          placeholder templates (BOM.md, ASSEMBLY.md) for a fork's own bill of
                        materials and assembly instructions

app/backend/           protocol.py (codec) -> link.py (threaded serial, id correlation) ->
                        device.py (schema cache + validation) -> main.py (FastAPI routes/WS).
                        terminal.py parses the Terminal page's commands; settings_ini.py is a
                        pure INI codec for settings backup/restore (no device, no coercion);
                        firmware.py is the bundle catalog + dfu-util wrapper.
                        pytest-tested against a fake serial port (app/tests/fake_serial.py),
                        no board needed
app/firmware/          the firmware images this app version ships with, plus manifest.json.
                        GITIGNORED build output — produced by the script above, not committed.
app/tools/             bundle_firmware.py, run by hand at release time to produce the above

app/web/                static HTML/JS only, talks to the backend exclusively through the
                        `Api` object in app.js — that object is the ENTIRE porting surface
                        for a hypothetical future Electron rewrite. `index.html` is a shell
                        (header/sidebar/mount point); each page's markup is its own file
                        under `pages/`, fetched and injected into the mount point on
                        navigation — adding a page means adding one `pages/<name>.html` and
                        a nav button, not editing every other page's markup.
app/docs/               placeholder template (USER_GUIDE.md) for a fork's own user guide
```

**`hardware/`** sits outside those three tiers — KiCad schematic/PCB source for the wiring
diagrams shown on the Examples page (`app/web/index.html`'s inline SVGs, format documented in the
`wiring-diagram-svg` skill). Reference material only: nothing in `firmware/`, `app/`, or the build
reads it, and it is opened directly in KiCad, not through either tested tier. Registered as its own
folder in `betacrawler.code-workspace`, same pattern as `firmware/`.

## Rules that must not be undone

Each of these has cost real defects or real rework. The reasoning is in `docs/architecture.md`
under the named section — read it before changing that area, not after.

**Modules** — `core/` never names a feature. Each module is `<name>_params.cpp` (its `ModuleDesc`;
**zero Arduino includes**, the native build compiles it) plus `<name>_driver.cpp` (the
`core::Module` subclass that touches hardware). Never put a `ParamDef` in a driver file. Adding a
module or a board: `_notes/_archive/spec-firmware-modules.md`.

**Module-local indices** — modules always receive a module-local parameter index, never a global
one; `Module::globalParam(local)` maps back. "Exactly one hardware call, on the owning module,
with that module's own local index" is the invariant most worth preserving in `dispatch.cpp` and
`registry.cpp`.

**Observers are const** — `Module::attach()` hands out const access on purpose. A module must
never reconfigure another behind `dispatch`'s back. Resolve keys once in `attach()`, never per tick.

**The `Api` seam** — no `fetch`/`WebSocket` outside `Api` for anything that talks to the device or
backend; no browser-only types (`File`, `Blob`, the socket itself) in or out; every such path
through `Api.base`; the push channel stays `Api.subscribe(handler) -> unsubscribe` and owns its own
reconnection. One documented exception: `showPage()`'s `fetch('pages/<name>.html')` in `app.js`
loads this app's own static page markup, not a device/backend request — it isn't part of the
porting surface this seam exists to isolate for a hypothetical Electron rewrite, so it's exempt.
Any fetch that *does* talk to the device or backend has no excuse to live outside `Api`.

**Validation stays schema-driven, curation doesn't** — `div`/`dec`/`showIf` are **display hints
only**: the wire always carries native units, and firmware/backend still validate a hidden
parameter so Terminal `set` and INI restore keep working, regardless of whether any page shows that
parameter at all. Beyond that, `app/web/pages/{config,controller,modes}.html` hand-pick which keys
they show, their label, their order and their page placement via `Alpine.store('config').field(key)`
/`Alpine.store('telemetry').field(key)` (a per-key lookup, not an iteration). Adding a firmware
parameter needs a page decision and a written label before it appears anywhere in the UI. A curated
page must handle a key its own board doesn't publish (`field(key).def === null`) explicitly rather
than assume every named key exists.

**`config_hash.py` stays in `extra_scripts`** — board-header edits don't trigger rebuilds without
it (`#include BOARD_HEADER` is invisible to SCons), and removing it silently reintroduces
stale-binary builds. Its companion, `force_version_rebuild()` in `bundle_firmware.py`, is what
makes a shipped image's `built` stamp truthful.

**Flash is written only on explicit `save`** — an erase stalls the MCU ~1s. Set applies to
RAM/hardware instantly. The stored record is guarded by magic/version/fingerprint/CRC and falls
back to defaults on any mismatch.

**Both DFU paths must keep working** — the `dfu` wire op *and* BOOT0+NRST by hand. The Firmware
page stays out of `CONNECTION_REQUIRED_PAGES`; gating the recovery tool on a working device is
backwards. `enterDfu()` only arms the reboot (so the response can flush first) and `initVariant()`
clears the RTC magic before jumping (or a failed jump is an unrecoverable boot loop).

**`app/firmware/` stays gitignored** — don't re-commit the binaries and don't "fix" the
`.gitignore` entry. A checkout with no firmware until the script runs is the expected state.

**No panel presence detection exists** — `gfx->begin()`'s bool is not a presence check; there is no
MISO to read a controller ID back over. Likewise, nothing can identify a board in DFU mode: every
STM32F4 bootloader reports `0483:df11`.

**Both versions stay 1.0.0 in this template** — firmware (`FW_VERSION`) and app (`APP_VERSION`)
version independently, and bumps happen in forked projects, not here.

**Page convention** — every `<div id="page-*">` section ends with a `<p>&nbsp;</p>` spacer as its
last child so content doesn't sit flush against the viewport bottom. Add one when you add a page;
don't strip them as "empty markup". All six pages have one deliberately.
