# Spec — Configurator Core (Project 1)

**Date:** 2026-07-25
**Status:** Approved, not yet implemented
**Progress tracking:** see `_notes/progress.md`

---

## Purpose

Build a Betaflight-Configurator-style control panel for an STM32 Black Pill: firmware exposing a
structured config/telemetry protocol over USB serial, and a PC app that reads live state and
changes settings.

The sibling project `test1` already proves the hardware path (USB CDC serial, ST7789 dashboard,
MCU telemetry readers). What's missing is any way to *talk to* the board from a PC. This project
builds that.

## Scope

This spec covers **Project 1: configurator core** only.

| In scope | Out of scope (deferred) |
|---|---|
| Firmware JSON-lines protocol | **Project 2: in-app DFU flashing** |
| Parameter registry + validation | Remote firmware release manifest |
| Flash persistence (save / defaults) | Porting the `test1` ST7789 dashboard |
| Telemetry streaming | Electron port |
| Python backend (serial link + REST/WS API) | Windows / macOS packaging |
| Schema-driven Bootstrap web UI | |

Development flashing stays on **ST-Link over SWD**, unchanged from the current workflow.

## Decisions

| Decision | Choice | Rationale |
|---|---|---|
| App stack | Python + FastAPI, Bootstrap UI | Fastest to working comms; best-supported serial/USB libs |
| Wire protocol | JSON lines | Self-describing, readable in a serial monitor, trivial app-side |
| Protocol logic | Backend owns device model | Testable with pytest, debuggable with curl |
| Firmware base | Fresh minimal | Display ported later |
| Testing | Host-side unit tests | Pure-C++ core compiles and tests natively; no board in the loop |
| Decomposition | Two sub-projects | DFU risk isolated from core value |
| Persistence | In scope | A configurator that forgets on reboot is half a product |
| Codegen for shared schema | **Rejected** | Runtime `schema` op already solves drift, and does it better |

## Portability constraint

Python was chosen on the condition that moving to Electron later is not a rewrite. That holds
only if:

> **`app/web/` is static HTML/CSS/JS containing nothing Python-specific.** No Jinja, no
> server-side rendering. It reaches the backend exclusively through the documented HTTP + WS API,
> and all such access goes through a single `Api` module in `app.js`.

That module is the entire porting surface. An Electron port reimplements the device model in Node
and moves `app/web/` across untouched.

## Verified environment

Measured on this machine 2026-07-25 — **not assumptions**:

- **udev/permissions already sorted; add no setup steps for this.** PlatformIO's
  `/etc/udev/rules.d/99-platformio-udev.rules` has a vendor-wide catch-all —
  `ATTRS{idVendor}=="0483", MODE="0666", ENV{ID_MM_DEVICE_IGNORE}="1"` — covering the VCP
  (`0483:5740`), ST-Link (`0483:3748`) and DFU (`0483:df11`). `/dev/ttyACM0` is already
  `crw-rw-rw-`; the user is **not** in `dialout` and does not need to be; ModemManager is active
  but already told to ignore these devices.
- **Python 3.14.4** — new enough that compiled wheels (pydantic/uvicorn) may lag. Resolve before
  writing code against it; fall back to 3.12 if needed.
- PlatformIO CLI at `~/.platformio/penv/bin/pio` — **not on `PATH`**.
- `dfu-util` present at `/usr/bin/dfu-util` (needed for Project 2, not this one).
- Board connected as `0483:5740` on `/dev/ttyACM0`. **Two** ST-Link/V2 units attached — uploads
  may need an explicit `upload_port`.

## Hardware

Black Pill STM32F411CE, 512KB flash, 128KB RAM.

- **LED**: `PC13`, **active-low** (LOW = on).
  **`PC13` has no timer channel on the F411 — there is no hardware PWM on this pin**, and it sits
  in the backup domain with limited drive current. Hence a blink-rate parameter rather than
  brightness.
- **Button**: `PA0`, `INPUT_PULLUP`.
- **Telemetry readers to port from `test1/src/main.cpp`**: `readVddaMv()` (:226), `readTempC()`
  (:231), `freeRamBytes()` (:57), plus `SystemCoreClock` and `millis()`. The factory-calibration
  constants at :31-36 (`VREFINT_CAL`, `TS_CAL1`, `TS_CAL2`) come with them.

## Architecture

Three layers. Organising principle: **logic never touches I/O.**

```
firmware/
  src/core/              pure C++ — no Arduino, no I/O    <- native unit tests
    protocol.{h,cpp}       line -> Request; Response -> line
    params.{h,cpp}         registry, types, validation, defaults
  src/
    main.cpp               Arduino glue: USB CDC, timing, EEPROM
    hardware.{h,cpp}       LED control + telemetry readers (ported from test1)

app/backend/
  protocol.py            JSON-lines codec                  <- pytest
  link.py                SerialLink: thread, id correlation, timeouts, reconnect
  device.py              DeviceModel: schema cache, values, connection state
  main.py                FastAPI routes + WebSocket
app/web/                 static HTML/JS/Bootstrap — portable
  index.html
  app.js                 Api module + UI
  vendor/                Bootstrap 5, vendored (not CDN)

docs/
  api.md                 HTTP/WS contract — the Electron port contract
_notes/
  spec-configurator-core.md    this spec
  progress.md                  living status, updated every session
```

### The critical seam

`core/` must not call `digitalWrite` — that drags in Arduino and kills native testing. It depends
on an interface the glue implements:

```cpp
struct HardwareSink {
  virtual void onParamChanged(ParamId id, const Value& v) = 0;
};
```

Core validates and stores; the sink applies to hardware. Tests pass a mock sink and assert
*"setting `led.blink_hz` to 5 produced exactly one sink call with value 5"* — GPIO logic verified
with no board attached. **This interface is what makes the whole testing strategy work.**

ArduinoJson is header-only with no Arduino dependency and compiles natively, so `core/` uses the
real codec and the tests exercise real production code.

### Single source of truth

The parameter table in `core/params.cpp` is a static array. The `schema` op serialises it
directly. Adding a row makes firmware validation, persistence and the web UI's form control all
follow automatically, with no other edits anywhere.

## Wire protocol

One JSON object per line, `\n` terminated, both directions.

**Buffer sizing** — inbound and outbound are not symmetric, and getting this wrong truncates the
schema silently:

- **Inbound: 256 bytes max.** Longest realistic request is a `set` on `device.name` (31 chars),
  well inside it. Anything longer is discarded to the next `\n` with `overflow`.
- **Outbound: 1024 bytes.** The `schema` response for the four parameters runs ~450 bytes and is
  by far the largest message. Size the ArduinoJson document for the schema case, not the
  telemetry case.

Requests carry an `id` (1..65535, wrapping). Responses echo it. **Unsolicited messages carry no
`id`** — that is precisely what lets push telemetry interleave with request/response safely.

### Requests (PC → MCU)

```jsonc
{"id":N,"op":"hello"}
{"id":N,"op":"schema"}
{"id":N,"op":"get","key":"led.mode"}
{"id":N,"op":"getall"}                        // avoids N round-trips on connect
{"id":N,"op":"set","key":"led.blink_hz","val":5}
{"id":N,"op":"save"}                          // commit to flash
{"id":N,"op":"defaults"}                      // reset RAM values to defaults
{"id":N,"op":"tlm","on":true}                 // telemetry stream on/off ONLY
```

**The `tlm` op carries no rate.** Streaming rate comes from the `tlm.rate` parameter and nowhere
else — two ways to set one value is exactly the kind of drift the schema design exists to avoid.
The op toggles streaming; the parameter sets its speed.

### Responses (MCU → PC)

```jsonc
{"id":N,"ok":true,"fw":"app-demo 0.1.0","proto":1,"board":"blackpill_f411ce"}
{"id":N,"ok":true,"params":[ ... ]}           // schema
{"id":N,"ok":true,"key":"led.mode","val":"blink"}
{"id":N,"ok":true,"vals":{"led.mode":"blink","led.blink_hz":2, ...}}
{"id":N,"ok":true}
{"id":N,"ok":false,"err":"range"}
```

### Unsolicited (MCU → PC, no `id`)

```jsonc
{"tlm":{"up":1204,"clk":96,"temp":41.2,"vdd":3298,"ram":18432,"btn":0}}
{"log":"config saved"}
```

`up` = ms uptime, `clk` = MHz, `temp` = °C, `vdd` = mV, `ram` = free bytes, `btn` = 0|1.

ArduinoJson does its own float serialisation, so the `-u _printf_float` linker trap does not
apply — natural types are safe.

### Error codes

`range` · `enum` · `toolong` · `nokey` · `badop` · `badjson` · `overflow`

## Parameters

| key | type | constraints | default | label | unit |
|---|---|---|---|---|---|
| `led.mode` | enum | `off` \| `on` \| `blink` | `blink` | LED Mode | |
| `led.blink_hz` | u8 | 1..20 | 2 | Blink Rate | Hz |
| `device.name` | str | maxlen 31 | `app-demo` | Device Name | |
| `tlm.rate` | u8 | 1..50 | 10 | Telemetry Rate | Hz |

**Out-of-bounds values are rejected, never coerced.** A `device.name` longer than 31 chars returns
`toolong`; it is not silently truncated. Same for numeric ranges and enums — silent coercion means
the UI shows a value the device does not hold.

Chosen to cover four distinct UI control types (select, slider+number, text, number), so the
schema-driven generator is genuinely proven rather than working for one case. `led.blink_hz`
gives visible confirmation a change landed; `tlm.rate` is self-demonstrating.

Schema entries as emitted:

```jsonc
{"key":"led.mode","type":"enum","options":["off","on","blink"],"def":"blink","label":"LED Mode"}
{"key":"led.blink_hz","type":"u8","min":1,"max":20,"def":2,"label":"Blink Rate","unit":"Hz"}
{"key":"device.name","type":"str","maxlen":31,"def":"app-demo","label":"Device Name"}
{"key":"tlm.rate","type":"u8","min":1,"max":50,"def":10,"label":"Telemetry Rate","unit":"Hz"}
```

## Persistence

Values live in RAM and apply instantly. Flash is written **only** on explicit `save`.

The F411 has no real EEPROM; the Arduino core emulates it in flash. A sector erase **stalls the
CPU for ~1s** and can disrupt USB, and flash is rated ~10k cycles. Never write per-change.

```
offset 0  magic    u32   0x4D444C31 ("MDL1")
offset 4  version  u16   1
offset 6  crc16    u16   over payload
offset 8  payload        packed param values
```

On boot: read, verify magic + version + CRC; on any mismatch, silently load defaults.

## Backend API

The Electron port contract. Documented in `docs/api.md` as it is built.

```
GET  /api/ports              -> [{"port":"/dev/ttyACM0","desc":"...","vid":"0483","pid":"5740"}]
POST /api/connect            {"port":"/dev/ttyACM0"}
POST /api/disconnect
GET  /api/status             -> {"state":"connected","fw":"...","proto":1,"board":"..."}
GET  /api/schema             -> cached schema
GET  /api/params             -> {"led.mode":"blink", ...}
PUT  /api/params/{key}       {"val":V}   -> 200 | 400 {"err":"range"} | 504
POST /api/params/save
POST /api/params/defaults
WS   /ws                     -> {"type":"tlm"|"state"|"log", ...}
```

**`link.py` owns the serial port in a dedicated thread.** pyserial's `read()` blocks; calling it
from the async event loop freezes the entire server and looks like a board crash. The reader
thread parses lines, resolves pending futures by `id`, and publishes id-less messages to
subscribers.

Board auto-detected by scanning `serial.tools.list_ports` for VID `0x0483` / PID `0x5740`.

## Data flow

*Setting a value:*

```
UI  --PUT /api/params/led.blink_hz {"val":5}-->  main.py
                                                 device.py  validate vs cached schema
                                                 link.py    assign id=7, send, await
    {"id":7,"op":"set","key":"led.blink_hz","val":5}  ---> firmware
                                                 core: validate -> store -> sink.onParamChanged()
                                                 glue: retimes the blink
    <--- {"id":7,"ok":true}
                                                 link.py resolves future id=7
UI  <--200 OK--                                  device.py updates cache
```

*Telemetry* runs independently: a firmware timer emits `{"tlm":{...}}` at `tlm.rate` Hz. The
reader thread sees no `id`, publishes to subscribers, and it lands on the WebSocket.

The backend validates against its cached schema before sending, so bad input fails fast with a
useful message. **The firmware validates again regardless — it must never trust its host.**

## Error handling

| Failure | Handled by | Behaviour |
|---|---|---|
| Value out of range / bad enum | firmware core | `{"ok":false,"err":"range"}` -> HTTP 400 -> inline field error |
| No response in **1s** | `link.py` | future times out -> HTTP 504 -> toast, connection marked suspect |
| Board unplugged | reader thread | state -> `disconnected`, pushed over WS; UI disables form |
| Garbage / partial line | both codecs | log and discard; **never** let one bad line wedge the parser |
| Oversized line into firmware | firmware core | discard to next `\n`, reply `{"ok":false,"err":"overflow"}` |
| Unknown protocol version | `device.py` | refuse to connect on `proto` mismatch rather than misbehaving |
| **Flash erase during `save`** | UI | MCU stalls ~1s and telemetry gaps — tolerate it, do **not** declare disconnect |

**Disconnect threshold: 3 missed telemetry intervals**, deliberately slack enough to survive a
legitimate `save`.

This heuristic only works while telemetry is streaming. **When telemetry is off, disconnect
detection falls back to serial-port-level errors alone** — there is no heartbeat, and none is
being added. The UI therefore leaves streaming on by default, and `tlm.on=false` is understood to
degrade disconnect detection to "we notice when the OS tells us."

**Reconnection is modelled explicitly, not bolted on** — Project 2 (DFU) will make the port vanish
and reappear on every flash. `link.py` treats "port disappeared" as a normal state transition and
rescans on a timer.

## UI

Bootstrap 5, vendored locally (works offline, ports cleanly into Electron). Single page with a
persistent connection bar (port dropdown, connect/disconnect, firmware version badge) and two tabs:

- **Configuration** — form generated from the schema response; controls and bounds derived from
  the declared metadata; dirty-state indicator; Save / Load Defaults.
- **Telemetry** — live values with small sparklines, fed by the WebSocket.

A **Firmware** tab is added by Project 2. Leave room for it; build nothing.

## Testing

### Native C++ (`pio test -e native`, Unity)

| Under test | Cases |
|---|---|
| `protocol.cpp` | valid ops parse; malformed JSON rejected; oversized line discards to next `\n`; responses serialise |
| `params.cpp` | range rejection, enum rejection, over-long string **rejected** with `toolong`, defaults, schema matches table |
| `HardwareSink` | mock asserts `led.blink_hz=5` produces exactly one sink call with 5 |

### pytest (`app/tests/`)

The highest-value test double is a **scripted fake serial port**. Real hardware cannot reliably
reproduce the cases that actually break this code; a fake can, deterministically:

- response arrives *after* its timeout
- responses arrive **out of order** (id=8 before id=7)
- telemetry lines interleave mid-request
- garbage bytes mid-stream
- port vanishes mid-request

Those five drive `link.py`'s design. Then `device.py` (schema cache, state machine) and `main.py`
routes via FastAPI's `TestClient`.

### Manual checklist (not automated)

- LED blink rate visibly changes on set
- `save` -> power-cycle -> values persist
- `defaults` restores and the UI reflects it
- telemetry visibly speeds up when `tlm.rate` increases

### Known gap

**EEPROM persistence and USB CDC behaviour have no automated coverage** under this strategy; they
live in the manual checklist. If persistence bugs bite during implementation, that is the signal
to revisit and add on-target Unity tests, which were considered and set aside.

## Build order

Falls out of the dependency graph. TDD per unit: failing test -> minimum code -> refactor.

1. `core/protocol` — native tests
2. `core/params` — native tests
3. Firmware glue: `main.cpp`, `hardware.cpp` (LED, telemetry) — verify via serial monitor
4. Firmware EEPROM persistence
5. `backend/protocol.py` — pytest
6. `backend/link.py` — pytest against the fake serial
7. `backend/device.py` — pytest
8. `backend/main.py` routes — pytest via TestClient
9. `web/` UI — manual
10. Manual verification checklist pass

Steps 1-2 need no hardware. Step 3 is the first that needs the board.

## Risks

- **Python 3.14.4 is very new** — compiled wheels for pydantic/uvicorn may lag. Resolve at step 5
  before writing code against it; fall back to 3.12 if needed.
- **USB CDC TX can block** if the host is not reading. Rate-limit telemetry; never block the main
  loop on a full TX buffer.
- **Flash erase during `save`** stalls the MCU ~1s. Expect a telemetry gap; the UI must tolerate it.
