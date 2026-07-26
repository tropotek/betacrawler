# HTTP / WebSocket API

The contract between `app/web/` and the backend. **An Electron port reimplements
exactly this surface in Node; `app/web/` moves across untouched.**

## REST

| Method | Path | Body | Returns |
|---|---|---|---|
| GET | `/api/ports` | — | `[{port, desc, vid, pid, match}]` |
| POST | `/api/connect` | `{"port": "/dev/ttyACM0"}` | status object |
| POST | `/api/disconnect` | — | status object |
| GET | `/api/status` | — | `{state, fw, proto, board, name, ver, built, mods}` |
| GET | `/api/schema` | — | `{params: [...], tlm: [...]}` |
| GET | `/api/params` | — | `{key: value}` |
| PUT | `/api/params/{key}` | `{"val": V}` | `{ok, key, val}` |
| POST | `/api/params/save` | — | `{ok}` |
| POST | `/api/params/defaults` | — | `{ok, vals}` |
| POST | `/api/params/restore` | `{"ini": "[led]\nmode = on\n"}` | `{ok, applied, skipped, vals}` |
| POST | `/api/terminal` | `{"command": "get led.blink_hz"}` | `{ok, friendly, raw_sent, raw_recv}` |

### Status fields

`fw` is a display string (`"app-demo 1.0.0"`) and is the stable field to show
in a UI. `name`/`ver`/`built` are its structured form, and `mods` lists the
modules the connected firmware was built with (`["device","system","button",
"led"]`). Firmware predating the module refactor omits all four; the backend
reports them as `null` / `[]` rather than failing the handshake.

### Schema

One response, two descriptors — the firmware's registered modules are the sole
source of truth for both, so a board that enables another module gains form
controls and telemetry cards with **no change to the backend or the UI**.

```json
{
  "params": [
    {"key": "led.mode", "type": "enum", "options": ["off","on","blink","fade"],
     "def": "blink", "label": "LED Mode", "group": "LED"},
    {"key": "led.blink_hz", "type": "u8", "min": 1, "max": 20, "def": 2,
     "label": "Rate", "unit": "Hz", "group": "LED"},
    {"key": "disp.page", "type": "enum", "options": ["info","stats","cycle"],
     "def": "info", "label": "Page", "group": "Display"}
  ],
  "tlm": [
    {"key": "temp", "label": "Temp", "unit": "°C", "dec": 1, "group": "System"},
    {"key": "vdd", "label": "VDD", "unit": "V", "div": 1000, "dec": 2, "group": "System"}
  ]
}
```

`group` is a UI heading; both pages render one section per group, in the order
the groups first appear. On a telemetry field, `div` and `dec` are **display
hints only** — the wire always carries the device's native units (`vdd` is
integer millivolts) and only the browser divides and rounds.

### Settings backup and restore

`dump` (a Terminal command) renders the device's settings as INI text; section
= the part of a key before the first dot, option = the rest, so `led.blink_hz`
becomes `[led]`/`blink_hz`. A key with no dot goes under `[general]`. The header
lines are `;` comments, so a dump parses straight back in.

`POST /api/params/restore` is the other half: it applies such a file to the
device. It is deliberately **partial-tolerant** — a key this firmware doesn't
have (a dump taken from a board with more modules enabled) or a value it
rejects goes into `skipped`, and every other key still applies:

```json
{"ok": false,
 "applied": ["led.mode"],
 "skipped": [{"key": "wifi.ssid", "reason": "unknown parameter 'wifi.ssid'"}],
 "vals": {"led.mode": "on", "...": "..."}}
```

`ok` is true only when at least one key applied and nothing was skipped.
Restore writes to RAM only — it never sends a `save`, so persisting stays an
explicit user action, exactly like editing a field in the config form. Text
that isn't valid INI is rejected whole, before anything reaches the device
(400 `{"err": "badini"}`); a disconnected device is 409, as everywhere else.

`/api/terminal` powers the debug Terminal page's shell-like command line
(`get <key>`, `set <key> <value>`, `save`, `defaults`, `list`, `dump`,
`help`). It is a
deliberate exception to the error-status table below: it **always returns
200**. Command-level failures (unknown command/key, bad value, disconnected,
...) are carried in `ok:false` + `friendly`, console-style, not as an HTTP
error status. `raw_sent`/`raw_recv` are the literal wire JSON lines exchanged
for that command — empty for `help`, or for any error caught before a line is
ever written to the port (e.g. an unknown key or a malformed argument count).

### Error responses

| Status | Meaning | Body |
|---|---|---|
| 400 | Value rejected | `{"err": "range"\|"enum"\|"toolong"\|"nokey"\|"badtype", "detail": "..."}` |
| 400 | Restore body is not valid INI | `{"err": "badini", "detail": "..."}` |
| 400 | Save failed (flash write/read-back mismatch) | `{"err": "flash", "detail": "..."}` |
| 409 | Not connected | `{"err": "disconnected", ...}` |
| 502 | Connect failed / protocol mismatch | `{"detail": "..."}` |
| 504 | Device did not answer in time | `{"err": "timeout", ...}` |

## WebSocket `/ws`

Server pushes only; clients send nothing. Every frame is
`{"type": "tlm"|"state"|"log"|"raw", "data": ...}`.

- `tlm` — one key per entry in the schema's `tlm` array. For the stock
  blackpill build that is `{up, clk, ram, temp, vdd, btn}`, but the field set
  is whatever the firmware's modules publish — read it from the descriptor
  rather than hardcoding it.
- `state` — status object, or the string `"disconnected"`
- `log` — device log string. Unsolicited `{"log": "..."}` lines from the
  firmware. The device holds its boot record (identity, whether saved settings
  survived, module/param counts, free RAM, plus any per-module line such as the
  display's init timing) and **replays it after every `hello`**, so a client
  that connects long after boot still receives it. The UI shows these in the
  Terminal prefixed `[device]`. Buffer is 8 lines; if boot produced more, a
  final `boot: log full, lines dropped` says so rather than hiding it.

A `save` stalls the board ~1s and telemetry will gap. **That is not a
disconnect** — do not treat it as one.
