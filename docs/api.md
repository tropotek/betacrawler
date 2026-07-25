# HTTP / WebSocket API

The contract between `app/web/` and the backend. **An Electron port reimplements
exactly this surface in Node; `app/web/` moves across untouched.**

## REST

| Method | Path | Body | Returns |
|---|---|---|---|
| GET | `/api/ports` | — | `[{port, desc, vid, pid, match}]` |
| POST | `/api/connect` | `{"port": "/dev/ttyACM0"}` | status object |
| POST | `/api/disconnect` | — | status object |
| GET | `/api/status` | — | `{state, fw, proto, board}` |
| GET | `/api/schema` | — | array of param descriptors |
| GET | `/api/params` | — | `{key: value}` |
| PUT | `/api/params/{key}` | `{"val": V}` | `{ok, key, val}` |
| POST | `/api/params/save` | — | `{ok}` |
| POST | `/api/params/defaults` | — | `{ok, vals}` |
| POST | `/api/terminal` | `{"command": "get led.blink_hz"}` | `{ok, friendly, raw_sent, raw_recv}` |

`/api/terminal` powers the debug Terminal page's shell-like command line
(`get <key>`, `set <key> <value>`, `save`, `defaults`, `help`). It is a
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
| 400 | Save failed (flash write/read-back mismatch) | `{"err": "flash", "detail": "..."}` |
| 409 | Not connected | `{"err": "disconnected", ...}` |
| 502 | Connect failed / protocol mismatch | `{"detail": "..."}` |
| 504 | Device did not answer in time | `{"err": "timeout", ...}` |

## WebSocket `/ws`

Server pushes only; clients send nothing. Every frame is
`{"type": "tlm"|"state"|"log"|"raw", "data": ...}`.

- `tlm` — `{up, clk, temp, vdd, ram, btn}`
- `state` — status object, or the string `"disconnected"`
- `log` — device log string

A `save` stalls the board ~1s and telemetry will gap. **That is not a
disconnect** — do not treat it as one.
