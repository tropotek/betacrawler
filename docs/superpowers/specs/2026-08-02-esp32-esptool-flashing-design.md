# ESP32 esptool flashing — design

## Purpose

The app's Firmware page can already flash STM32 boards end-to-end (bundled image or a local
file, via `dfu-util`). The `esp32_wroom32` target — added 2026-08-01 — was deliberately kept out
of that mechanism: its design spec (`docs/superpowers/specs/2026-08-01-esp32-wroom-target-design.md`)
states flashing it is "not added to `app/firmware/`'s release catalog" and stays a `pio run
-t upload` developer action.

This revisits that decision. Motivation: parity for end users — someone using the app, not a
developer with PlatformIO installed, should be able to flash a pre-built ESP32 image from the
Firmware page the same way they flash a Black Pill today.

## Why this isn't "swap dfu-util for esptool"

STM32 DFU and ESP32/esptool are mechanically different in ways that shape the whole design:

- **Identity.** A Black Pill in DFU mode disappears from its serial port and reappears as a
  distinct USB device (`0483:df11`) that `dfu-util` finds by scanning USB directly — the app
  never needs to know which port it was on. An ESP32 never changes USB identity: `esptool` resets
  it into its ROM bootloader via DTR/RTS toggling on the *same* CP2102/CH340 serial port it talks
  JSON over. There is nothing to poll for the way `dfu-util -l` polls for `0483:df11`.
- **Image shape.** A raw STM32 image is one file written at one fixed flash address
  (`0x08000000`). A stock Arduino-framework ESP32 build produces four files at four offsets
  (`bootloader.bin` at `0x1000`, `partitions.bin` at `0x8000`, `boot_app0.bin` at `0xe000`,
  `firmware.bin` at `0x10000`) — the same four files `pio run -t upload` already writes.

Both differences are handled once, at the boundary, so everything downstream (manifest shape,
progress events, UI) stays uniform with the STM32 path.

## Manifest: the `method` field goes live

`_notes/todo.md` already flagged this as prepared-but-unused: every manifest entry has a
`method` field, currently always `"dfu"`. This design is what makes it real. STM32 entries keep
`"method": "dfu"` (now explicit rather than the only option); the ESP32 entry gets
`"method": "esptool"`. Both the bundler and the backend flashing code dispatch on this field
instead of guessing from `board`.

## Bundling (`app/tools/bundle_firmware.py`)

For an `esptool`-method env, after `pio run -e <env>`, merge the build's four output pieces into
one flashable image with `esptool.py merge_bin`:

| File | Offset | Source |
|---|---|---|
| `bootloader.bin` | `0x1000` | `.pio/build/<env>/bootloader.bin` |
| `partitions.bin` | `0x8000` | `.pio/build/<env>/partitions.bin` |
| `boot_app0.bin` | `0xe000` | the Arduino framework package (`framework-arduinoespressif32/tools/partitions/boot_app0.bin`) |
| `firmware.bin` | `0x10000` | `.pio/build/<env>/firmware.bin` |

These are the `esp32dev` board's standard 4MB/DIO/40MHz defaults, confirmed (not assumed) against
this project's own `~/.platformio/platforms/espressif32*/boards/esp32dev.json`
(`flash_size: "4MB"`, `flash_mode: "dio"`, `f_flash: "40000000L"`, upload speed `460800`) and by
actually running `esptool merge-bin` against this repo's real `esp32_wroom32` build output —
`boot_app0.bin` lives at the exact path above with no version suffix on this machine. They are
not re-derived per build; if a future `platformio.ini` change alters flash size or partition
table, the merge step is expected to start producing a boot-broken image, loudly, rather than
silently guessing — this is called out as a follow-up risk, not solved here (see Non-goals).

The resulting merged file is a **sparse** image, not four files back to back: bytes `0x0`-`0xFFF`
are `0xFF` padding (flash's erased-state value) because the first real content, `bootloader.bin`,
starts at offset `0x1000`. This matters for `check_esp32_image` below — the ESP image magic byte
is at offset `0x1000` in the merged file, not offset `0`.

Validation splits by method:
- `dfu`-method envs keep today's `check_vector_table` (MSP in SRAM, reset vector in flash).
- `esptool`-method envs get a new `check_esp32_image`. Verified against a real `merge-bin` run
  against this project's own build output: bytes `0x0`-`0xFFF` of the merged image are `0xFF`
  padding (the bootloader offset is `0x1000`, not `0x0`), so the ESP image magic byte (`0xE9`)
  has to be checked at offset `0x1000`, not offset `0` — a bare `blob[0] == 0xE9` would reject
  every real merged image. Size is also checked against sane bounds.
  This is the same check `firmware.py` applies to a bundled image, mirrored for the merge step
  the way `check_vector_table` already mirrors `firmware.py`'s upload-time check.

Manifest entries are otherwise unchanged in shape — one file, one sha256, one `size` — so
`Catalog` in the backend needs no changes at all.

## Bundler: an `--all` flag to build every board target

Separate from ESP32 support itself, but landing in the same pass because it touches the same
`plan_entry()` code path: `method` needs to come from *somewhere* other than a hardcoded string,
and once it does, a "build every board" flag follows almost for free.

**Method is derived, not hardcoded.** `plan_entry()` currently writes `"method": "dfu"`
unconditionally, for every env. It changes to read the env's `platformio.ini` block for
`-D FW_MCU_ESP32=1` (the same macro the 2026-08-01 spec introduced to guard ESP32-only driver
bodies) and set `"method": "esptool"` when present, `"dfu"` otherwise — one new `method_for(env)`
helper, parsed the same way `board_header_path()` already parses that block.

**`DEFAULT_ENV` stays `blackpill_f411ce`.** Bare `python3 app/tools/bundle_firmware.py` keeps
today's behavior unchanged — one board, the primary target, no surprise multi-board build (and no
surprise new `esptool`/pip dependency pulled in) for anyone still running the old command out of
habit.

**A new `--all` flag builds every board target.** `all_board_envs()` parses `platformio.ini` for
every `[env:*]` section that defines `-D BOARD_HEADER=...` (reusing the regex
`board_header_path()` already applies per-env), in file order. `native` has no `BOARD_HEADER` —
it's the host-side unit-test env, not a shippable board — so it's excluded without a name-based
blocklist that would itself go stale, which is exactly the kind of drift this script's docstring
already says it exists to prevent. `--all` is mutually exclusive with naming envs explicitly
(`bundle_firmware.py --all board_a` is an argument error, not a silent override).

**VS Code task.** `silkscreen.code-workspace`'s existing "Build release firmware" task (prompts
for space-separated env names, defaulting to `blackpill_f411ce`) is left as-is for selective
builds. A new "Build ALL release firmware" task runs `bundle_firmware.py --all` directly with no
prompt, so building the full release set is one Tasks: Run Task away.

**Docs.** The `bundle-firmware` skill's usage block and this script's own module docstring gain a
line for `--all`.

## Backend: `EsptoolFlasher`

New class in `firmware.py`, sibling to `DfuFlasher`, same injected-`runner` pattern (so it tests
with a fake subprocess and no real esptool):

```
class EsptoolFlasher:
    def __init__(self, runner=None, esptool: str = "esptool"):
        ...
    def flash(self, path: Path, port: str, on_progress=None) -> None:
        argv = [self._esptool, "--chip", "esp32", "--port", port,
                "--baud", "460800", "write-flash", "0x0", str(path)]
        # NO_COLOR=1: forces plain output even when the backend process
        # inherited a color-capable TERM, so the log never carries raw
        # ANSI escape bytes.
        ...
```

- No `devices()` / `wait_for_device()` — there is nothing to enumerate. The port is supplied by
  the caller (see UI below), and `esptool` performs the reset-into-bootloader handshake itself
  when it opens that port.
- Progress: verified against the real, installed `esptool` package (v5.3.1) rather than
  assumed — its actual `write-flash` output is `Writing at 0x00010000 [====>          ]  12.3%
  41000/334144 bytes...` (a bracketed bar, and a **float** percent with one decimal, not the
  `(NN %)` shape older esptool docs/memory suggest). A regex pulls the float and rounds it to an
  int; there is only ever one progress source (the write pass — the stub-based path esptool uses
  by default never prints a separate erase percentage), so `op` is `"writing"` whenever it
  matches, `None` otherwise. Also verified: with no controlling TTY (a subprocess pipe) but a
  color-capable `TERM` inherited from the parent process, esptool still emits ANSI escape codes
  and `\r`-based overwrites, which would otherwise land as literal escape bytes in the log
  textarea — `EsptoolFlasher` sets `NO_COLOR=1` in the subprocess environment to force plain
  output regardless of what the backend process's own terminal supports. The existing `_Process`
  wrapper (already splits on both `\r` and `\n` for `dfu-util`'s sake) is reused as-is.
  Output stays the same `{"op", "pct", "line"}` dict shape `on_progress` already produces for
  DFU, so `FlashSession`, the WS `flash` event, and app.js's `onFlashEvent` need no changes.
- `esptool` ships as a pip package with a console-script entry point (confirmed: `pip install
  esptool` provides both an `esptool` and an `esptool.py` command) — added to
  `app/requirements.txt`, not expected on `PATH` like `dfu-util` (which has no pip package).
  A missing install is handled the same way a missing `dfu-util` is: caught `FileNotFoundError`
  → a `FirmwareError` the Firmware page can show, not a crash.
- CLI syntax: esptool v5 deprecated the underscored subcommands/flags (`write_flash`,
  `merge_bin`, `--flash_mode`) in favor of hyphenated ones (`write-flash`, `merge-bin`,
  `--flash-mode`) — the old spellings still work today but print a deprecation warning line on
  every single flash, which would appear in the user-facing log. `EsptoolFlasher` and the
  bundler's merge step both use the hyphenated forms throughout.

**Releasing the port.** If the app is currently connected to the board on the same port the user
selected, `SerialLink` is holding it open and `esptool` cannot also open it. Before starting an
`esptool` flash, the route compares the requested port to the connected one and, on a match,
releases the link the same way `enter_dfu()` does today — except with no wire op sent first,
since ESP32 boards have `FEATURE_DFU` off and there is no such op to send.

This needs one small addition `DeviceModel` doesn't have today: `status()` currently reports
`fw`/`board`/`caps`/etc. from the `hello` response but never the port string itself (`connect()`
takes `port` as a local argument and never stores it). Fixed by having `connect()` stash
`self._port = port` alongside `self._link.connect(port)`, cleared on disconnect, and added to the
`status()` dict. Small, but real — the release check above doesn't work without it.

**Validation** splits the same way bundling does:
- `validate_dfu_image()` — today's `validate_image()`, renamed, unchanged behavior.
- `validate_esp32_image()` — new: magic byte `0xE9`, size bounds. No vector-table check, since
  a merged ESP32 image has no Cortex-M vector table at offset 0.

## Routes

- `POST /api/firmware/flash` — body gains an optional `port` field, required when the target
  image's `method` is `esptool`. `FlashSession.start()` picks `DfuFlasher.flash(path, ...)` or
  `EsptoolFlasher.flash(path, port, ...)` based on the image's `method`.
- `POST /api/firmware/flash-upload` — gains `method` (query param, default `dfu`) and `port`
  (query param, required when `method=esptool`), alongside the existing `filename`. Picks
  `validate_dfu_image` / `validate_esp32_image` and the matching flasher the same way.
- `GET /api/ports` — unchanged. Already returns `{"port", "desc", "vid", "pid", "match",
  "board"}` per port, and `_KNOWN_BOARDS` in `link.py` already recognizes the ESP32 devkit's
  CP2102 bridge (`0x10C4:0xEA60`) and labels it `"ESP32"`. Reused as-is for the new port picker.

## Frontend (`app.js` / `index.html`)

**Bundled-image flow.** Selecting a catalog card whose `method === 'esptool'` swaps the
DFU-specific controls (badge, "Reboot to DFU" button, DFU-mode polling) for a port `<select>`
populated from `Api.get('/api/ports')`. Options are labeled with the same format the Connect
dropdown already uses (`app.js`, port-list rendering: `p.board ? \`${p.port} (${p.board})\` :
p.desc || p.port`), so a connected ESP32 shows as `/dev/ttyUSB0 (ESP32)` here too, not a bare
device path. "Flash selected firmware" stays disabled until a port is chosen. The DFU badge and
`pollDfu()` stay conditional on a `dfu`-method image being selected/available — no behavior
change for the STM32 path.

**Advanced: flash a local file.** Gains a target selector (`STM32 (DFU)` / `ESP32 (esptool)`)
above the file picker. Choosing ESP32 reveals the same port `<select>` described above and sends
`method`/`port` alongside the upload. The existing "nothing verifies this file matches your
board" warning applies to both targets unchanged.

**Progress panel / log / done state**: unchanged — same event shape, same rendering.

**"Getting into DFU mode by hand" section** gains an ESP32-equivalent paragraph: hold BOOT/IO0
until `esptool` connects, matching the recovery procedure the 2026-08-01 spec already documents
for the manual `pio` flow.

## Testing

- `EsptoolFlasher`: unit tests with a fake runner, mirroring `DfuFlasher`'s coverage (happy path,
  missing-tool, non-zero exit, progress parsing).
- `flash-upload`'s `method=esptool` branch: fake runner + a synthetic `0xE9`-prefixed blob for
  `validate_esp32_image`.
- `test_bundle_firmware.py`'s existing two-board fixture tree grows a case exercising the
  `esptool` merge step and `check_esp32_image` — per `_notes/todo.md`, this half of the two-board
  work ("method != dfu has no dispatch behind it") was explicitly left untested until now.
- `method_for()` and `all_board_envs()` get direct unit tests against a fixture `platformio.ini`
  (mixed DFU/esptool envs, plus a `native`-like env with no `BOARD_HEADER` to confirm it's
  excluded), plus a `--all`-vs-explicit-envs mutual-exclusion test and a check that bare
  `release([])` still defaults to `[DEFAULT_ENV]` only, unchanged from today.
- Manual verification on real hardware: bundle → app UI → pick port → flash → board reboots →
  reconnect, end to end. Per this repo's standing rule, green tests alone don't close this out.

## Non-goals

- **Per-file partition flashing.** Considered and declined — a merged single binary keeps the
  manifest/Catalog/UI uniform with the STM32 shape; the cost is a slightly larger download
  (padding between partitions).
- **Auto-detecting the ESP32's port.** Declined — the port dialog is explicit for both the
  bundled and Advanced-upload flows, on both success and recovery paths.
- **Re-deriving merge offsets from a live PlatformIO build.** The hardcoded offsets match today's
  `esp32dev`/4MB/DIO/40MHz config exactly; a future flash-size or partition-table change in
  `platformio.ini` would need this table updated by hand. Flagged, not solved, here.
- **Other ESP32 variants** (S2/S3/C3/etc.) — out of scope, same as the 2026-08-01 spec.
