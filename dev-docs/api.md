# HTTP / WebSocket API

The contract between `app/web/` and the backend. **An Electron port reimplements
exactly this surface in Node; `app/web/` moves across untouched.**

## REST

| Method | Path | Body | Returns |
|---|---|---|---|
| GET | `/api/ports` | — | `[{port, desc, vid, pid, match, board, sim}]` |
| POST | `/api/connect` | `{"port": "/dev/ttyACM0"}` | status object |
| POST | `/api/disconnect` | — | status object |
| GET | `/api/status` | — | `{state, fw, proto, board, name, ver, built, mods}` |
| GET | `/api/schema` | — | `{params: [...], tlm: [...]}` |
| GET | `/api/params` | — | `{key: value}` |
| PUT | `/api/params/{key}` | `{"val": V}` | `{ok, key, val}` |
| POST | `/api/params/save` | — | `{ok}` |
| POST | `/api/params/defaults` | — | `{ok, vals}` |
| POST | `/api/params/revert` | — | `{ok, src, vals}` |
| POST | `/api/params/restore` | `{"ini": "[led]\nmode = on\n"}` | `{ok, applied, skipped, vals}` |
| POST | `/api/terminal` | `{"command": "get rx.deadband_us"}` | `{ok, friendly, raw_sent, raw_recv}` |
| GET | `/api/firmware/catalog` | — | `{app_version, images, board, recommended}` |
| GET | `/api/firmware/dfu-status` | — | `{present, devices, busy}` |
| POST | `/api/firmware/enter-dfu` | — | `{ok, ...status}` |
| POST | `/api/firmware/flash` | `{"id": "<catalog id>", "port": "<optional, required for esptool-method images>"}` | `{ok, id}` |
| POST | `/api/firmware/flash-upload?method=dfu\|esptool&port=<required for esptool>` | raw `.bin` bytes | `{ok, filename, size}` |
| POST | `/api/wifi/scan` | — | `{ok}` |

### Status fields

`fw` is a display string (`"betacrawler 1.0.0"`) and is the stable field to show
in a UI. `name`/`ver`/`built` are its structured form, and `mods` lists the
modules the connected firmware was built with (`["device","system","button",
"led"]`). Firmware predating the module refactor omits all four; the backend
reports them as `null` / `[]` rather than failing the handshake.

`caps` lists device capabilities that are **not** modules — things the device
can *do* rather than things it *has*. Currently `"dfu"` and `"wifiscan"`, each
present when the corresponding feature (`FEATURE_DFU`, `FEATURE_WIFI`) was
compiled in — see `POST /api/wifi/scan` below for the latter. Same additive
contract as `mods`: firmware that predates a given cap omits the key and the
backend reports `[]`.

### The simulated port

`/api/ports` always lists `sim://board` first, with `sim: true` and
`match: false`. Connecting to it runs an in-process simulated device that
speaks the same wire protocol as real firmware, so every route on this page
behaves identically against it. It reports `board: "simulator"` and no
`caps`, so `/api/firmware/catalog` recommends nothing and `enter-dfu`
answers `nodfu`. Its telemetry is fabricated and reacts to the parameters
you set; nothing it stores survives a disconnect.

`built` is the firmware's own `__DATE__ " " __TIME__`, and the firmware bundle's
manifest records the identical string. That makes "is the device running *this*
image?" an exact comparison rather than a version-number guess — two builds of
the same version number are still told apart.

### Schema

One response, two descriptors — the firmware's registered modules are the sole
source of truth for both, so a board that enables another module gains form
controls and telemetry cards with **no change to the backend or the UI**.

```json
{
  "params": [
    {"key": "rx.protocol", "type": "enum", "options": ["crossfire","elrs"],
     "def": "elrs", "label": "Protocol", "group": "RX"},
    {"key": "rx.deadband_us", "type": "u8", "min": 0, "max": 200, "def": 0,
     "label": "Deadband", "unit": "µs", "group": "RX"},
    {"key": "disp.page", "type": "enum", "options": ["info","stats","cycle"],
     "def": "info", "label": "Page", "group": "Display"}
  ],
  "tlm": [
    {"key": "temp", "label": "Temp", "unit": "°C", "dec": 1, "group": "System"},
    {"key": "vdd", "label": "VDD", "unit": "V", "div": 1000, "dec": 2, "group": "System"},
    {"key": "up", "label": "Uptime", "fmt": "hms", "group": "System"},
    {"key": "fault", "label": "Fault", "group": "System"},
    {"key": "ch1", "label": "CH1", "unit": "µs", "fmt": "bar", "lo": 988, "hi": 2012, "group": "RC Channels"}
  ]
}
```

`group` is a UI heading; both pages render one section per group, in the order
the groups first appear. On a telemetry field, `div`, `dec`, `fmt`, `lo` and
`hi` are **display hints only** — the wire always carries the device's native
units (`vdd` is integer millivolts, `up` is milliseconds) and only the browser
divides, rounds and formats.

`lo`/`hi` are an optional declared range, emitted only when the field has one
(i.e. only when they differ). A UI that draws a bar for the reading uses them
as the bar's ends; the wire value itself is never clamped to them.

On a **parameter**, `showIf` (`{"key": "rx.protocol", "val": "crossfire"}`) is
conditional visibility, emitted only when the parameter declares one. `key`
names another parameter and `val` the enum **option name** (not its index)
that parameter must currently hold for this one to be drawn. Like `div`/
`dec`/`fmt`/`lo`/`hi` on a telemetry field, it is a **display hint only**: the
device still validates and accepts a `set`/restore against a parameter that
is currently hidden, so a client must never treat `showIf` as an access rule.

On a **parameter**, `secret` (`true`, omitted otherwise) marks a Str field whose value is a
credential rather than a label — `wifi.password` is the first example. Like every other schema
hint, this is display-only: the wire and flash storage carry it as plain text identical to any
other setting, and Terminal `set`/INI restore accept it unchanged. A client renders it as a masked
input rather than plain text. Note that an INI settings backup/dump therefore also contains the
password in clear text, same as every other setting — masking is a display convenience, not a
storage guarantee.

`fmt` names a renderer, which may be textual (`hms`) or visual (`bar`). It is a
**name, not a format string**: both renderers (the browser and the firmware's
own panel) have to implement it, and a client that does not recognise the name
falls back to the plain number. `div`/`dec` do not apply to a field that
carries a **text** `fmt`. `bar` is a visual renderer that does not replace the
number, so a field may legitimately carry both a divisor and a bar.

| `fmt` | Wire value          | Rendered   |
|-------|---------------------|------------|
| `hms` | milliseconds (u32)  | `01:01:01` — hours are not clamped to two digits |
| `bar` | `1500` (with `lo`/`hi`) | `1500` plus a proportional bar |

### Discarding unsaved changes

`POST /api/params/revert` reloads the settings last written to flash — the "last stored point".
It is not the same as `defaults`, which goes to the firmware's factory values.

`src` says which source the firmware actually used. It is `"defaults"` when the board had
nothing valid stored (a fresh board, or settings discarded because the enabled module set
changed the fingerprint); the op falls back rather than failing, and reports it rather than
landing the caller somewhere silently. Only `src == "flash"` leaves the device with nothing
unsaved — after a `"defaults"` fallback there is something worth saving.

### Settings backup and restore

`dump` (a Terminal command) renders the device's settings as INI text; section
= the part of a key before the first dot, option = the rest, so `rx.deadband_us`
becomes `[rx]`/`deadband_us`. A key with no dot goes under `[general]`. The header
lines are `;` comments, so a dump parses straight back in.

`POST /api/params/restore` is the other half: it applies such a file to the
device. It is deliberately **partial-tolerant** — a key this firmware doesn't
have (a dump taken from a board with more modules enabled) or a value it
rejects goes into `skipped`, and every other key still applies:

```json
{"ok": false,
 "applied": ["rx.protocol"],
 "skipped": [{"key": "wifi.ssid", "reason": "unknown parameter 'wifi.ssid'"}],
 "vals": {"rx.protocol": "elrs", "...": "..."}}
```

`ok` is true only when at least one key applied and nothing was skipped.
Restore writes to RAM only — it never sends a `save`, so persisting stays an
explicit user action, exactly like editing a field in the config form. Text
that isn't valid INI is rejected whole, before anything reaches the device
(400 `{"err": "badini"}`); a disconnected device is 409, as everywhere else.

`/api/terminal` powers the debug Terminal page's shell-like command line
(`get <key>`, `set <key> <value>`, `save`, `defaults`, `revert`, `list`, `dump`,
`help`). It is a
deliberate exception to the error-status table below: it **always returns
200**. Command-level failures (unknown command/key, bad value, disconnected,
...) are carried in `ok:false` + `friendly`, console-style, not as an HTTP
error status. `raw_sent`/`raw_recv` are the literal wire JSON lines exchanged
for that command — empty for `help`, or for any error caught before a line is
ever written to the port (e.g. an unknown key or a malformed argument count).

### Firmware bundle and DFU flashing

The app ships the firmware images that match it, under `app/firmware/`, with a
`manifest.json` describing each. `app/tools/bundle_firmware.py` produces both at
release time; the folder is build output and is not in the repo, so a source
checkout serves an empty catalog until that script has been run.
`/api/firmware/catalog` serves that manifest:

```json
{"app_version": "1.0.0",
 "board": "blackpill_f411ce",
 "recommended": "blackpill_f411ce-betacrawler-1.0.0",
 "images": [
   {"id": "blackpill_f411ce-betacrawler-1.0.0", "board": "blackpill_f411ce",
    "name": "betacrawler", "version": "1.0.0", "built": "Jul 26 2026 16:25:03",
    "proto": 1, "method": "dfu", "file": "blackpill_f411ce/betacrawler-1.0.0.bin",
    "size": 86652, "sha256": "...", "notes": "led, button, servo, dfu",
    "available": true}]}
```

`recommended` is the image whose `board` matches the last device that completed
a `hello`, and is `null` when no board has been connected in this session. **A
board in DFU mode reports `0483:df11` and nothing else** — it cannot say what it
is — so there is no honest recommendation to make without that prior handshake.

Every firmware route works while disconnected, on purpose: a board that needs
re-flashing is frequently a board that cannot be talked to. The exception is
`enter-dfu`, which by definition needs a live device; it answers `409
disconnected` otherwise.

`enter-dfu` sends the firmware's `dfu` op. The firmware **replies before it
resets** — flushing the response, then rebooting — so `ok:true` means "request
accepted" and the port vanishing a moment later is the expected outcome, not a
fault. The backend closes the link immediately for the same reason.

`flash` re-verifies the bundled file's size and sha256 on disk before writing
anything; the manifest and the binary are two files that can drift apart.
`flash-upload` has no checksum to check against, so it validates the image's
vector table instead (initial SP in SRAM, Thumb reset vector in flash) and
rejects sizes outside 1KB–512KB — which is what catches the realistic mistake of
picking `firmware.elf` or `firmware.hex` out of `.pio/build` instead of
`firmware.bin`. It takes the image as the **raw request body**, not a multipart
form: one fewer backend dependency, and the caller only has to produce bytes.

An `esptool`-method image (currently `esp32_wroom32`) is flashed differently under the hood: an
ESP32 in its ROM bootloader has no distinct USB identity to detect the way a Black Pill in DFU
mode does (`0483:df11`), so both `flash` and `flash-upload` require an explicit `port` for such
an image, and skip the `waiting` phase entirely (there is nothing to poll for). If the app is
currently connected to the board on that same port, it is released first so `esptool` can open
it; a connection on a different port is left alone.

Only one flash runs at a time; a second request is `409 {"err": "busy"}`.

### WiFi network scan

`POST /api/wifi/scan` arms an SSID scan (`AT+CWLAP` on the ESP-01) and returns as soon as the
firmware has started it — scanning itself takes a few seconds, so results are **not** in this
response. They arrive later over `/ws` as a `scan` frame:

```json
{"type": "scan", "data": [{"ssid": "Home", "rssi": -52}, {"ssid": "Neighbour", "rssi": -81}]}
```

Present only when the connected firmware's `caps` includes `"wifiscan"` (`FEATURE_WIFI` compiled
in). `409 disconnected` applies as it does everywhere else; a device that has never joined a
network can still scan.

### Error responses

| Status | Meaning | Body |
|---|---|---|
| 400 | Value rejected | `{"err": "range"\|"enum"\|"toolong"\|"nokey"\|"badtype", "detail": "..."}` |
| 400 | Restore body is not valid INI | `{"err": "badini", "detail": "..."}` |
| 400 | Save failed (flash write/read-back mismatch) | `{"err": "flash", "detail": "..."}` |
| 400 | Bad image, bad catalog entry, failed flash | `{"err": "firmware", "detail": "..."}` |
| 400 | Firmware cannot reboot to DFU | `{"err": "nodfu", "detail": "..."}` |
| 400 | Firmware cannot scan for networks | `{"err": "nowifi", "detail": "..."}` |
| 409 | Not connected | `{"err": "disconnected", ...}` |
| 409 | A flash is already running | `{"err": "busy", "detail": "..."}` |
| 502 | Connect failed / protocol mismatch | `{"detail": "..."}` |
| 504 | Device did not answer in time | `{"err": "timeout", ...}` |

## WebSocket `/ws`

Server pushes only; clients send nothing. Every frame is
`{"type": "tlm"|"state"|"log"|"flash"|"scan"|"raw", "data": ...}`.

- `tlm` — one key per entry in the schema's `tlm` array. For the stock
  blackpill build that is `{up, clk, ram, temp, vdd, btn}`, but the field set
  is whatever the firmware's modules publish — read it from the descriptor
  rather than hardcoding it.
- `state` — status object, or the string `"disconnected"`
- `log` — device log string. Unsolicited `{"log": "..."}` lines from the
  firmware. The device holds its boot record (identity, whether saved settings
  survived, module/param counts, free RAM, plus any per-module init line) and
  **replays it after every `hello`**, so a client
  that connects long after boot still receives it. The UI shows these in the
  Terminal prefixed `[device]`. Buffer is 8 lines; if boot produced more, a
  final `boot: log full, lines dropped` says so rather than hiding it.

- `flash` — firmware-flashing progress:
  `{phase, pct, op, line}`. `phase` is `waiting` → `flashing` → `done`|`error`.
  `op` names which of dfu-util's two passes a percentage belongs to (`erase`
  then `download`, each running 0–100%); a single bar keyed on a bare
  percentage would reach 100%, snap back and climb again. `line` is dfu-util's
  own output, passed through unfiltered — including the "Invalid DFU suffix"
  warning every raw `.bin` produces.

  Deliberately **not** sent as `log`. Log frames are the *device* talking and
  the UI renders them as `[device] …`; flashing output comes from dfu-util
  running on the host, and filing it under `log` would misattribute it.

- `scan` — the result of `POST /api/wifi/scan`: `[{ssid, rssi}, ...]`. Arrives
  once, some seconds after the request that armed it; see "WiFi network scan"
  above.

A `save` stalls the board ~1s and telemetry will gap. **That is not a
disconnect** — do not treat it as one.
