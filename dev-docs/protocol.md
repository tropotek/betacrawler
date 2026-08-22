# Device wire protocol

The contract between a host and the firmware. `web-app/js/` is one implementation of the host
half: `protocol.js` codes the lines, `webserial-link.js` owns the port and correlates ids,
`device-model.js` caches the schema and validates against it, and `api.js` is the only object the
UI is allowed to reach any of it through.

## The wire

One JSON object per line, `\n`-terminated, over USB CDC serial at 115200 (`FW_SERIAL_BAUD`). A
request carries an `id` and an `op`; the response echoes that `id`. A message with **no** `id` is
unsolicited — telemetry and log lines — which is what lets push traffic interleave safely with
request/response on one connection.

The firmware validates every input against its own registered descriptors and never trusts the
host. A host-side check is a convenience, never the guarantee.

## Operations

| `op` | Request fields | Response |
|---|---|---|
| `hello` | — | `{fw, name, ver, built, proto, board, mods, caps}` |
| `schema` | — | `{params: [...], tlm: [...]}` |
| `get` | `key` | `{key, val}` |
| `getall` | — | `{vals: {key: val}}` |
| `set` | `key`, `val` | `{ok}` |
| `save` | — | `{ok}` |
| `defaults` | — | `{ok}` |
| `revert` | — | `{ok, src}` |
| `tlm` | `on` | `{ok}` |
| `dfu` | — | `{ok}` |
| `wifiscan` | — | `{ok}` |

Every response carries `ok`. A failure adds `err`:

| `err` | Meaning |
|---|---|
| `nokey` | No such parameter |
| `range` / `enum` / `toolong` / `badtype` | The value was rejected by the parameter's own descriptor |
| `flash` | `save` wrote but the read-back did not match |
| `nodfu` | This build has no bootloader seam (`FEATURE_DFU` off) |
| `nowifi` / `busy` | No WiFi seam / a scan is already running |
| `badop` | Unknown `op` |

`fw` is a display string (`"betacrawler 4.0.0"`); `name`/`ver`/`built` are its structured form.
`built` is the firmware's own `__DATE__ " " __TIME__`, and a bundled image's manifest records the
identical string — which makes "is the board running *this* image?" an exact comparison rather
than a version-number guess, two builds of one version number included.

`mods` lists the modules this build registered; `caps` lists what the device can *do* rather than
what it has — `"dfu"` and `"wifiscan"`, each present only when the corresponding feature compiled
in. Both are additive: a firmware predating either omits the key, and a host reads a missing one
as empty.

## Schema

One response, two descriptors. The firmware's registered modules are the sole source of truth for
both, so a board that enables another module gains form controls and telemetry cards with no
change to any host.

```json
{
  "params": [
    {"key": "rx.protocol", "type": "enum", "options": ["crossfire","elrs"],
     "def": "elrs", "label": "Protocol", "group": "RX"},
    {"key": "rx.deadband_us", "type": "u8", "min": 0, "max": 200, "def": 0,
     "label": "Deadband", "unit": "µs", "group": "RX"},
    {"key": "device.name", "type": "str", "maxlen": 31, "def": "betacrawler",
     "label": "Device Name", "group": "Device"}
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

`group` is a UI heading. On a telemetry field, `div`, `dec`, `fmt`, `lo` and `hi` are **display
hints only** — the wire always carries the device's native units (`vdd` is integer millivolts,
`up` is milliseconds) and only the browser divides, rounds and formats.

`lo`/`hi` are an optional declared range, emitted only when the field has one. A UI that draws a
bar uses them as its ends; the wire value itself is never clamped to them.

On a **parameter**, `showIf` (`{"key": "rx.protocol", "val": "crossfire"}`) is conditional
visibility, emitted only when declared. `key` names another parameter and `val` the enum **option
name** (not its index) it must currently hold for this one to be drawn. It is a display hint like
the rest: the device still validates and accepts a `set` or an INI restore against a parameter
that is currently hidden, so a client must never read `showIf` as an access rule.

`secret` (`true`, omitted otherwise) marks a Str parameter whose value is a credential rather than
a label — `wifi.password` is the first. Also display-only: the wire and flash carry it as plain
text like any other setting, and an INI dump therefore contains it in the clear. Masking is a
rendering convenience, not a storage guarantee.

`fmt` names a renderer, which may be textual (`hms`) or visual (`bar`). It is a **name, not a
format string**: every renderer has to implement it, and one that does not recognise the name
falls back to the plain number. `div`/`dec` do not apply to a field carrying a text `fmt`. `bar`
does not replace the number, so a field may legitimately carry both a divisor and a bar.

| `fmt` | Wire value | Rendered |
|-------|---------------------|------------|
| `hms` | milliseconds (u32) | `01:01:01` — hours are not clamped to two digits |
| `bar` | `1500` (with `lo`/`hi`) | `1500` plus a proportional bar |

`firmware/test/golden/schema.json` is a checked-in fixture of this response, regenerated and
diffed by a native test, so a schema change that isn't reflected there fails a test rather than
drifting silently.

## Discarding unsaved changes

`revert` reloads the settings last written to flash — the "last stored point". It is not
`defaults`, which goes to the firmware's factory values.

`src` says which source the firmware actually used. It is `"defaults"` when the board had nothing
valid stored (a fresh board, or settings discarded because the enabled module set changed the
fingerprint); the op falls back rather than failing, and reports it rather than landing the caller
somewhere silently. Only `src == "flash"` leaves the device with nothing unsaved — after a
`"defaults"` fallback there is something worth saving.

## Settings backup and restore

`dump` (a Terminal command, not a wire op) renders the device's settings as INI text: section =
the part of a key before the first dot, option = the rest, so `rx.deadband_us` becomes
`[rx]`/`deadband_us`. A key with no dot goes under `[general]`. The header lines are `;` comments,
so a dump parses straight back in.

Restore is the other half, and is deliberately **partial-tolerant**: a key this firmware doesn't
have (a dump taken from a board with more modules enabled) or a value it rejects is reported as
skipped, and every other key still applies. It writes to RAM only — it never sends a `save`, so
persisting stays an explicit user action, exactly like editing a field in the config form. Text
that isn't valid INI is rejected whole, before anything reaches the device.

## The push channel

`Api.subscribe(handler) -> unsubscribe` delivers `{type, data}` frames, whatever transport
produced them:

- `tlm` — one key per entry in the schema's `tlm` array. The field set is whatever the firmware's
  modules publish; read it from the descriptor rather than hardcoding it.
- `state` — status object, or the string `"disconnected"`.
- `log` — a device log line. The firmware holds its boot record (identity, whether saved settings
  survived, module/param counts, free RAM, plus any per-module init line) and **replays it after
  every `hello`**, so a host that connects long after boot still receives it. The Terminal shows
  these prefixed `[device]`. The buffer is 8 lines; if boot produced more, a final `boot: log
  full, lines dropped` says so rather than hiding it.
- `raw` — any other unsolicited object, passed through unread.
- `flash` — firmware-flashing progress: `{phase, pct, op, line}`, where `phase` runs `waiting` →
  `flashing` → `done`|`error`, plus `needsdevice` when a parked flash is waiting for a WebUSB
  grant. `op` names which pass a percentage belongs to (`erase`, then `download`, each running
  0–100%); a single bar keyed on a bare percentage would reach 100%, snap back and climb again.
  Deliberately **not** sent as `log`: log frames are the *device* talking, and misfiling host-side
  flashing output under them would misattribute it.
- `dfu` — `{present, busy}`, the bootloader's visibility to this origin and whether a flash is
  running.

A `save` stalls the board ~1s and telemetry will gap. **That is not a disconnect** — do not treat
it as one.
