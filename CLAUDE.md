# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Betaflight-Configurator-style tool for an STM32 Black Pill (STM32F411CE): firmware exposes
device config/telemetry over USB serial as JSON lines, a Python/FastAPI backend bridges that to
HTTP+WebSocket, and a static Bootstrap web UI drives it. This is **Project 1 ("configurator
core")** of a larger effort — DFU flashing (Project 2) has not been started.

Read `_notes/progress.md` first in any new session — it's the living status doc and says exactly
what's done and what's next. `_notes/spec-configurator-core.md` is the approved design (don't
relitigate it without a real reason). `docs/api.md` is the HTTP/WS contract.

## Commands

`pio` is **not on PATH** — always invoke it as `~/.platformio/penv/bin/pio`.

**Firmware** (from `firmware/`):
```
~/.platformio/penv/bin/pio test -e native              # 31 tests, no board needed
~/.platformio/penv/bin/pio test -e native -f test_dispatch   # one suite only
~/.platformio/penv/bin/pio run -e blackpill_f411ce      # compile for the real board
~/.platformio/penv/bin/pio run -e blackpill_f411ce -t upload  # flash it (ST-Link/SWD)
~/.platformio/penv/bin/pio device monitor -b 115200     # raw serial console
```
Two ST-Link/V2 units may be attached at once — if upload grabs the wrong one, add
`upload_port = <device>` to `[env:blackpill_f411ce]`.

**Backend** (from `app/`, venv already at `app/.venv/`):
```
.venv/bin/pytest -v                                     # 34 tests, no board needed
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
firmware/src/core/     pure C++, zero Arduino — protocol.cpp, params.cpp, dispatch.cpp
                        native-tested (Unity), no board needed
firmware/src/          Arduino glue: main.cpp, hardware.cpp (LED/telemetry), storage.cpp (flash)
                        excluded from native tests by platformio.ini's
                        build_src_filter = +<core/*>  (an include-list, not exclude-list —
                        keeps every future Arduino-only file out automatically)

app/backend/           protocol.py (codec) -> link.py (threaded serial, id correlation) ->
                        device.py (schema cache + validation) -> main.py (FastAPI routes/WS)
                        pytest-tested against a fake serial port (app/tests/fake_serial.py),
                        no board needed

app/web/                static HTML/JS only, talks to the backend exclusively through the
                        `Api` object in app.js — that object is the ENTIRE porting surface
                        for a hypothetical future Electron rewrite. Never call fetch/WebSocket
                        directly outside it.
```

**The hardware/persistence seam** (`firmware/src/core/dispatch.h`): `core/` never touches a GPIO
pin or a flash write directly. It talks through two interfaces —
`HardwareSink::onParamChanged()` and `Persistence::save()/load()` — that `main.cpp` wires to real
Arduino/EEPROM code. Native tests inject mocks for both, so "setting `led.blink_hz` produced
exactly one hardware call" is provable with no board attached. This is the one invariant most
worth preserving if you touch `dispatch.cpp`.

**Wire protocol**: one JSON object per line, `\n`-terminated, over USB CDC serial (115200).
Requests carry an `id`; responses echo it. Messages with no `id` are unsolicited (telemetry, log)
— that's what lets push telemetry interleave safely with request/response on one connection.
Firmware validates every input independently of the backend (never trusts the host). Full spec in
`_notes/spec-configurator-core.md`; live contract in `docs/api.md`.

**Schema-driven UI**: the firmware's parameter table (`core/params.cpp`) is the single source of
truth. The `schema` wire op serializes it; `DeviceModel` (backend) caches that response and
validates `set()` calls against it before ever touching the wire; `app.js`'s `buildForm()`
generates the config form's controls from that same schema at runtime. Adding a firmware parameter
should need zero changes in `app.js` — if it doesn't, something has drifted from that design.
`firmware/test/golden/schema.json` is a checked-in fixture, regenerated and diffed by a native
test (`test_schema_golden_fixture_matches_firmware`) and loaded directly by the Python tests, so a
firmware schema change that isn't reflected there fails a test instead of drifting silently.

**Persistence discipline**: the F411 has no real EEPROM (flash-emulated), and an erase stalls the
MCU ~1s. Values apply to RAM/hardware instantly on `set`; flash is written **only** on explicit
`save`, guarded by a magic/version/CRC header that falls back to defaults on any mismatch.
`storage.cpp`'s `save()` does a read-back verification after the flush so a real flash failure has
an actual way to report `{"err":"flash"}` instead of that path being dead code.

**Disconnect detection has two independent layers**: the backend's `SerialLink` detects OS-level
port loss (unplug) immediately. The frontend watchdog additionally declares a distinct "stale"
badge state if telemetry hasn't arrived in 3x the configured interval while the port is still
OS-connected — catches a wedged-but-still-enumerated board. The 3x threshold is deliberate slack:
a `save`'s ~1s flash stall must never look like a disconnect.
