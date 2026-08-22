# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.
It documents development procedures and practices only — build/test commands, layout, and rules
that must not be undone. It is not a memory file: decision history, rationale, and narrative belong
in `dev-docs/architecture.md`, `CHANGELOG.md`, or commit messages, not here. Keep this file minimal.

## What this is

A Betaflight-Configurator-style tool for an STM32 Black Pill (STM32F411CE or STM32F401CE, one
build env each): firmware exposes device config/telemetry over USB serial as JSON lines, and a
static Bootstrap web app (`web-app/`) drives it straight from the browser over Web Serial, with
WebUSB for DFU flashing. No backend, no build step, no npm.

| Where | What |
|---|---|
| `_notes/todo.md` | the live document — what's next. Read it first in any new session |
| `CHANGELOG.md` | what has shipped, one summary per change |
| `dev-docs/architecture.md` | the reasoning behind every rule below — read the relevant section before changing that area |
| `dev-docs/protocol.md` | the device wire protocol and the `Api` seam's push frames |
| `_notes/_archive/` | history, not documentation (see below) |

**`_notes/_archive/` holds superseded specs, implementation plans and the retired `progress.md`.**
Read it for the *reasoning* behind a past decision — those specs are approved designs, so don't
relitigate them without a real reason — but **never update anything in it**, and don't read it as
a description of the code today. Where it and the code disagree, the code is right.

**Specs, plans and research are never committed.** `_notes/` is gitignored; write new specs/plans
under `_notes/docs/plans` and research under `_notes/docs/research`, not `docs/` — `docs/` is
user-facing project documentation only, and anything placed there gets committed.

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

Building and bundling release firmware images is covered by the `bundle-firmware` skill —
invoke it rather than reading this file for those steps.

**web-app** (from `web-app/`):
```
python3 -m http.server 9091   # serves the static site; open http://localhost:9091
node --test                   # unit tests for js/*.js, no board needed
```
Web Serial, WebUSB and service workers all require a secure context — `localhost` or HTTPS.
Serving this tree from a LAN IP over plain HTTP will not work, and it needs a Chromium-based
browser (Chrome, Edge, Brave, Opera); Firefox and Safari have no Web Serial API.

There is no build step, and no automated test suite for the *rendered* UI, by design — but
"therefore you cannot check the UI" does not follow, and believing it has cost real defects.
**A working headless browser is installed at `~/.pwvenv`** (Playwright + Chromium):

```
~/.pwvenv/bin/python3 script.py    # sync_playwright(), p.chromium.launch(headless=True)
```

Use it to read rendered text, click through a page and collect console/`pageerror` events before
claiming a UI change works. Point it at `http://localhost:9091`; for a state the hardware cannot
be made to produce, install a fake `navigator.serial` with `add_init_script()` before the page
loads and answer the wire protocol from it.

Do NOT try apt's `python3-playwright` (its client and the packaged Node driver speak incompatible
protocol versions) or `firefox --headless --screenshot` (hangs on framebuffer mapping here).

**Environment facts already resolved — don't redo this work:**
- udev/permissions are already correct system-wide (PlatformIO's own `0483` vendor rule covers
  VCP/ST-Link/DFU and tells ModemManager to ignore them). No `dialout` group, no custom udev rules.
- Git commits use `feat:`/`fix:`/`docs:`/`chore:` prefixes.

## Code style

- Comments stay strictly minimal: 2-3 lines describing what the code does and, optionally, its
  config options. No historic reasoning, no change explanations, no comments written to help a
  future reader when the code itself is readable — the code is the source of truth.
- Never reference a line number or a spec/design-doc file in a comment — both go stale the moment
  either file changes. If the reasoning matters, it belongs in the commit message or PR
  description, not inline.
- Comments and documentation (`dev-docs/architecture.md`, `dev-docs/protocol.md`, this file) describe the
  project as it currently is, never as a narrative of what changed — no "no longer exists", "used
  to be", "the old X page", "retired". `CHANGELOG.md` is the one place that history belongs; a
  living doc a reader hits later has no time context for a change narrative. This holds even more
  before a first real release, when there is no shipped history for a reader to already know.

## Layout

**Two tiers, deliberately isolated so most of the stack tests without hardware:**

```
firmware/include/      config.h (project name/version/baud/capacity limits) and
                        boards/<board>.h (FEATURE_* flags + pin map). Selected per-env by
                        -D BOARD_HEADER; _template.h documents adding a board.
firmware/src/core/     pure C++, zero Arduino — protocol, params, registry, dispatch,
                        triangle, led_pattern, health, tlm_format, boot_log, version,
                        device_params. Native-tested (Unity).
firmware/src/features/ behaviours, one folder per module (tank_drive/)
firmware/src/hardware/ device drivers, one folder per module (system/, button/, servo/, rx/;
                        WiFi and other peripherals go here)
firmware/src/modules.cpp  THE wiring file — one #if block per module. Compiled by BOTH envs.
firmware/src/          Arduino glue: main.cpp, storage.cpp (flash), dfu.cpp,
                        status_led.cpp (health LED + HardFault_Handler)
firmware/docs/          placeholder templates (BOM.md, ASSEMBLY.md) for a fork's own bill of
                        materials and assembly instructions

web-app/                the configurator: static HTML/JS, talking to the board directly over
                        the Web Serial API. `index.html` is a shell (header/sidebar/mount
                        point); each page's markup is its own file under `pages/`, fetched
                        and injected on navigation — adding a page means adding one
                        `pages/<name>.html` and a nav button, not editing every other page's
                        markup. `js/*.js` is the logic below the `Api` seam:
                        protocol/webserial-link/device-model/terminal/settings-ini/dfu,
                        vanilla ES modules with no build step, no npm dependencies and no
                        Python. Unit-tested with `node --test`, run from `web-app/` (it
                        discovers `tests/*.test.js` itself).
web-app/firmware/       the firmware images this site flashes, plus manifest.json. COMMITTED:
                        a static site has no server to build one on demand. Written at
                        release time by the `bundle-firmware` skill's script, which the
                        tests guard against drifting from the firmware sources.
```

**`hardware/`** sits outside both tiers — KiCad schematic/PCB source for the wiring
diagrams shown on the Wiring page (`web-app/pages/wiring.html`'s inline SVGs, format documented in
the `wiring-diagram-svg` skill). Reference material only: nothing in `firmware/`, `web-app/`, or the
build reads it, and it is opened directly in KiCad, not through either tested tier. Registered as its
own folder in `betacrawler.code-workspace`, same pattern as `firmware/`.

## Rules that must not be undone

Each of these has cost real defects or real rework. The reasoning is in `dev-docs/architecture.md`
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

**The `Api` seam** — no serial, WebUSB or network call outside `Api` (`web-app/js/api.js`) for
anything that talks to the device; no browser-only types (`File`, `Blob`, a stream) in or out; the
push channel stays `Api.subscribe(handler) -> unsubscribe`, delivering `{type, data}` frames
(`tlm`, `log`, `state`, `raw`, `flash`, `dfu`) regardless of what transport produced them, and owns
its own reconnection. Two documented exceptions: `showPage()`'s `fetch('pages/<name>.html')` loads
this app's own static page markup, not a device request; and `Api.connect()` takes a Web Serial
`SerialPort` object, because Web Serial's permission model has no other way to name a port.
Neither is part of the porting surface this seam exists to isolate for a hypothetical Electron
rewrite, so both are exempt. Any call that *does* talk to the device has no excuse to live outside
`Api`.

**Validation stays schema-driven, curation doesn't** — `div`/`dec`/`showIf` are **display hints
only**: the wire always carries native units, and the firmware still validates a hidden parameter
so Terminal `set` and INI restore keep working, regardless of whether any page shows that
parameter at all. Beyond that, `web-app/pages/{config,controller,modes}.html` hand-pick which keys
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

**The status LED outlives the registry** — `main.cpp` calls `g_statusLed.begin()` before
`registerModules()` and ticks it outside `g_reg`, on purpose: a health indicator must not depend on
the subsystem it reports on. The panic handler's busy-wait must never become `delay()` — HardFault
masks the interrupt that advances `millis()`, so it would hang forever.

**Both DFU paths must keep working** — the `dfu` wire op *and* BOOT0+NRST by hand. The Firmware
page stays out of `CONNECTION_REQUIRED_PAGES`; gating the recovery tool on a working device is
backwards. `enterDfu()` only arms the reboot (so the response can flush first) and `initVariant()`
clears the RTC magic before jumping (or a failed jump is an unrecoverable boot loop).

**`web-app/`'s DFU permission ladder has three rungs, not two** — poll what is granted, try the
chooser in case the flash's original click still counts, then park the flash behind a modal whose
own button opens it. Chrome gives a click 5 seconds of activation and a flash spends more than that
before it knows it needs permission, so rung two alone strands the user. `promptForDfuDevice()`
must keep reporting `SecurityError` distinctly, and `DFU_WAIT_MS` must stay short enough for rung
two to have a chance: `dev-docs/architecture.md`, "The permission ladder".

**CRSF receive stays off the ROM bootloader's UART pins** — `RX_RX_PIN` is PB7, not USART1's
usual PA10, and moving it back breaks both DFU paths. The bootloader picks its host interface by
watching for traffic and commits to the first one that shows any; a powered receiver on PA10 wins
that race every time, after which USB never enumerates. PB7 is the same USART1 on its alternate
pin, and is only an I2C1 candidate to the bootloader, which cannot commit without a master
clocking SCL. `RX_TX_PIN` stays PA9 deliberately: it is outbound only so it never triggers
detection, and leaving PB6 (I2C1_SCL) connected to nothing keeps that guarantee absolute — which
is also why esc1 keeps PB6. PA2/PA3 are no escape: USART2 is a bootloader interface too.

**`web-app/firmware/` stays committed** — unlike most build output: the site has no server, so
the images it flashes have to be in the deployed tree.
`web-app/tests/firmware-bundle.test.js` fails when those binaries no longer match the firmware
sources (the manifest's `fw_source_sha256`). Re-bundle and commit after any firmware source
change.

**Nothing can identify a board in DFU mode** — every STM32F4 bootloader reports `0483:df11`.

**Firmware and app share one version number** — `FW_VERSION` (`firmware/include/config.h`) and
`APP_VERSION` (`web-app/js/app.js`) are bumped together, not independently.

**Page convention** — every page fragment under `web-app/pages/` ends with a `<p>&nbsp;</p>`
spacer as its last child so content doesn't sit flush against the viewport bottom. Add one when you
add a page; don't strip them as "empty markup". All eight pages have one deliberately.
