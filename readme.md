# silkscreen

**This project is currently under construction. If you want to contribute hit up our disscussion forum and get involved.**

----

A Betaflight-Configurator-style tool for small microcontroller boards, and a template to fork
for your own.

The silkscreen on a PCB is the printed layer that tells you what every pad and pin actually is.
This does the same for firmware: the board declares its parameters and telemetry, and a browser
UI builds itself from that declaration. Adding a setting to the firmware makes a control appear
in the app. There is no second list to keep in sync.

Three tiers:

```
  STM32 board  ──USB serial──  Python backend  ──HTTP+WebSocket──  browser UI
  (firmware/)   JSON lines      (app/backend/)                      (app/web/)
```

The reference board is a WeAct **Black Pill (STM32F411CE)** with an LED, a button and an
optional ST7789 240x240 panel. Everything below uses it as the worked example.

## What you get

- **Live config** — every firmware parameter as a form control, validated on the device as well
  as in the app. Values apply instantly; flash is written only when you press Save.
- **Telemetry** — pushed from the board at a configurable rate, rendered as cards.
- **Terminal** — type `get led.mode`, `set led.blink_hz 5`, `save`; see the raw JSON both ways.
- **Firmware updates in-app** — the app carries an image matching its own version and flashes it
  over USB DFU, no ST-Link needed after the first time. (In a fresh clone you build that image
  once — see [step 3](#getting-started-in-vs-code); a packaged build already has it.)
- **Settings backup/restore** as INI files.
- **An on-device dashboard** on the optional SPI panel.

## Prerequisites

| | |
|---|---|
| **VS Code** | with the **PlatformIO IDE** extension (`platformio.platformio-ide`) |
| **Python 3** | 3.10+ should be fine; developed and tested here against 3.14 |
| **A board** | WeAct Black Pill STM32F411CE (or edit one header for a different one) |
| **A USB-C cable** | a *data* cable — charge-only cables are the classic first hour lost |
| **ST-Link/V2** | only for the very first flash, and only if you skip the DFU path below |
| **dfu-util** | optional, for the in-app Firmware page (`apt install dfu-util`) |

PlatformIO brings its own compiler toolchain, so there is nothing else to install for the
firmware side.

**Linux permissions:** install PlatformIO's udev rules once, or the board will enumerate but
refuse to open:

```bash
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/master/platformio/assets/system/99-platformio-udev.rules \
  | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

That one file covers the board's USB serial port, the ST-Link and DFU mode, and tells
ModemManager to keep its hands off. Replug the board afterwards.

## Getting started in VS Code

**1. Clone and open the workspace**

```bash
git clone <your-repo-url> silkscreen
cd silkscreen
code silkscreen.code-workspace
```

Open the **workspace file**, not the folder. It lists the repo root and `firmware/` as two
folders on purpose: PlatformIO only activates on a folder that has `platformio.ini` at its top
level, so `firmware/` has to be a workspace folder in its own right for the build buttons to
appear. VS Code will offer to install the recommended PlatformIO extension — accept, then
reload. The first activation downloads the STM32 toolchain and takes a few minutes.

**2. Build and flash the firmware**

With an ST-Link/V2 wired to the Black Pill's SWD pads (`3V3`, `SWDIO`/PA13, `SWCLK`/PA14,
`GND`), use the PlatformIO toolbar at the bottom of the window: **✓** builds, **→** uploads.
Or from a terminal:

```bash
cd firmware
~/.platformio/penv/bin/pio run -e blackpill_f411ce -t upload
```

`pio` is not on `PATH` after an IDE-only install — that full path is the reliable way to call it.

*No ST-Link?* You don't need one — do step 3 first, then flash over USB with dfu-util alone. See
[First flash without an ST-Link](#first-flash-without-an-st-link) below.

**3. Build the firmware the app ships**

`app/firmware/` holds the image the in-app **Firmware** page flashes. It is build output, not
source, so **a fresh clone does not have it** — run this once:

```bash
python3 app/tools/bundle_firmware.py blackpill_f411ce
```

Or **Terminal → Run Task → Build release firmware**. Re-run it after any firmware change worth
shipping; name every board you ship in the one command, since that command *is* the release.

Skip this and everything else still works — the app just shows an empty Firmware page telling you
to run it. (A packaged build of the app already contains the images, because this script is what
fills the folder before packaging.)

**4. Start the backend**

From the workspace, run the pre-configured VS Code task: **Terminal → Run Task → Run backend
server**. It restarts cleanly over any instance it already left running. Or by hand:

```bash
python3 -m venv app/.venv
app/.venv/bin/pip install -r app/requirements.txt
./run-server.sh                      # http://127.0.0.1:8080
```

`run-server.sh` takes `PORT=9000` and passes extra arguments straight to uvicorn (`--reload`).

**5. Connect**

Open <http://127.0.0.1:8080>, pick the port and press Connect. The board appears as
`/dev/ttyACM0` on Linux, `COMx` on Windows, `/dev/cu.usbmodem*` on macOS; the picker marks ports
with ST's USB vendor id `0483` as **(STM32)**, so the right one is obvious. The UI then reads
the device's schema and builds itself.

## The pages

| Page | What it does |
|---|---|
| **Home** | how to connect, and what the other pages are for |
| **Configuration** | every parameter, grouped by module. **Save to flash** persists; **Load defaults** resets |
| **Telemetry** | live values pushed from the board |
| **Terminal** | hand-typed commands, optionally showing raw JSON and background device traffic. Also where settings backup (`dump`) and **Restore from INI…** live |
| **Firmware** | flash the bundled image over USB DFU |
| **Help** | in-app troubleshooting — port not appearing, stale badge, settings not surviving a restart |

The connection badge and the firmware identity string sit in the top bar, visible from every
page.

Terminal and Firmware deliberately work while **disconnected** — gating the recovery tool on a
working device would be exactly backwards.

## Updating firmware from the app

The Firmware page flashes the images in `app/firmware/`. That folder is build output rather than
source — a packaged app is built after the release script has filled it, so a release always
carries firmware, while a source checkout has none until you run the script yourself. What keeps
"this app version pairs with this firmware" honest is that the script is the only thing that ever
writes there, and it derives every manifest field from the tree it just built.

One click does the whole thing: the app asks the board to reboot into its ROM bootloader, waits
for it to re-enumerate as `0483:df11`, runs dfu-util, and the board comes back on its own.

### First flash without an ST-Link

A board with no silkscreen firmware on it can't be asked to reboot into DFU, so do it by hand —
the same procedure rescues a board whose firmware is broken:

1. Make sure there is an image to flash: `python3 app/tools/bundle_firmware.py blackpill_f411ce`
   (step 3 above). From a fresh clone the Firmware page is empty until you do.
2. Hold **BOOT0**, tap **NRST**, release BOOT0. The board goes quiet rather than showing any
   sign of life — that is what DFU mode looks like.
3. On the Firmware page, press Flash. (`dfu-util -l` should list the board if you want to check
   first.)

Both routes into DFU are supported on purpose, and both are expected to keep working.

> **Nothing can identify a board in DFU mode.** Every STM32F4 ROM bootloader reports
> `0483:df11` and nothing else — no board name, no version. The app carries the board string
> forward from the last connection and says plainly when it has none. Check you picked the right
> image; the bootloader cannot check for you.

## Wiring the optional display

Only the panel needs wiring — the LED (PC13) and button (PA0) are already on the Black Pill.
ST7789 240x240 on hardware SPI1:

| Panel | Black Pill |
|---|---|
| `SCL` / `SCK` | PA5 |
| `SDA` / `MOSI` | PA7 |
| `DC` | PB1 |
| `RES` | PB0 |
| `BLK` | 3V3 |
| `VCC` / `GND` | 3V3 / GND |

There is no CS and no backlight control. **Nothing detects whether the panel is plugged in** —
there is no MISO line to read a controller ID back over — so the driver is write-only and the
board runs identically with the panel absent. Don't wait for a "display not found" warning; the
firmware cannot honestly produce one.

Not using a panel? Set `FEATURE_ST7789_240X240 0` in the board header and reflash: the code, its
parameters and its UI controls all disappear together.

## Running the tests

Neither suite needs a board attached.

```bash
cd firmware && ~/.platformio/penv/bin/pio test -e native      # 88 tests
cd app && .venv/bin/pytest -q                                 # 148 tests
```

The native suite compiles the *real* board header, so it assembles the actual device's parameter
and telemetry tables and diffs them against `firmware/test/golden/schema.json`. A firmware schema
change that isn't reflected there fails a test instead of drifting quietly. The Python suite runs
against a fake serial port. Only drivers, timing and boot ordering are outside both — that is the
part you verify by flashing the board.

## Making it yours

**Rename it.** `FW_PROJECT_NAME` in `firmware/include/config.h` is the one string to change;
the device name default, the identity reported over the wire and the firmware bundle's filename
all derive from it. The app's version lives separately at the top of `app/web/app.js` — firmware
and app are independent projects whose numbers are not meant to track each other.

**Add a board.** Copy `firmware/include/boards/_template.h`, fill in the `FEATURE_*` flags and
pin map, and add an `[env:]` block to `firmware/platformio.ini`. No source file changes.

**Add a module.** One folder under `firmware/src/features/<name>/` (behaviour) or
`firmware/src/hardware/<name>/` (a peripheral), plus one `#if` block in `firmware/src/modules.cpp`.
Each module is two files:

- `<name>_params.cpp` — its parameters and telemetry fields. **No Arduino includes**, because
  the native test build compiles it.
- `<name>_driver.cpp` — the code that touches hardware. Board builds only.

That split is load-bearing rather than stylistic: it's why the native suite can assemble the real
device's schema with no board attached. Name modules for the specific part (`st7789_240x240`, not
`display`) — every variant gets its own.

Nothing else needs editing. The parameters a module declares flow automatically from firmware
schema → backend → web form. **If adding a parameter requires touching `app.js`, something has
drifted from the design.**

**Ship a firmware build** after a change worth releasing. Name every board the release covers in
one command — that command *is* the release, and images from a previous run are pruned:

```bash
python3 app/tools/bundle_firmware.py board_a board_b
python3 app/tools/bundle_firmware.py --add board_c    # merge instead of replacing
python3 app/tools/bundle_firmware.py --dry-run        # report, write nothing
```

Or **Terminal → Run Task → Build release firmware** in the workspace.

Every manifest field is derived from the sources and the resulting binary — nothing is typed in.
Every board is built and validated before anything is written, so a board that fails to compile
leaves the previous release untouched rather than half-replaced.

## Layout

```
firmware/include/       config.h (identity, limits) and boards/<board>.h (FEATURE_* + pins)
firmware/src/core/      pure C++, zero Arduino: protocol, params, registry, dispatch. Unit-tested
firmware/src/features/  behaviours, one folder per module
firmware/src/hardware/  device drivers, one folder per module
firmware/src/modules.cpp  the wiring file — one #if block per module

app/backend/            protocol.py → link.py → device.py → main.py (FastAPI)
app/firmware/           the images this app ships with, plus manifest.json. Gitignored: it is
                        built by the script below, not committed
app/tools/              bundle_firmware.py, run by hand at release time
app/web/                static HTML/JS, no build step

docs/api.md             the HTTP/WebSocket contract
CLAUDE.md               architecture notes and the reasoning behind the design
```

The web UI talks to the backend exclusively through the `Api` object in `app.js`. That object is
the entire porting surface if you ever want an Electron build — everything else moves across
untouched.

## Wire protocol

One JSON object per line over USB CDC at 115200. Requests carry an `id` and responses echo it;
messages without an `id` are unsolicited, which is what lets pushed telemetry interleave safely
with request/response on a single connection. The firmware validates every input itself and never
trusts the host. Full contract in [`docs/api.md`](docs/api.md).

## Troubleshooting

**The port doesn't appear.** Charge-only USB cable, or missing udev rules (above). The board's
own USB is the USB-C connector — the ST-Link is a separate path and does not carry the serial
port.

**Upload grabs the wrong ST-Link.** With two attached, pin the right one by adding
`upload_port = <device>` to `[env:blackpill_f411ce]`.

**Editing a board header seemed to change nothing.** It rebuilds correctly —
`firmware/scripts/config_hash.py` exists precisely because a macro-expanded include is invisible
to the build system's dependency scanner. Don't remove the `extra_scripts` line that runs it, or
silent stale binaries come back.

**Saved settings vanished after reflashing.** Working as intended. A fingerprint over every
parameter's key, type and bounds is stored with the settings, so changing the enabled module set
or a parameter's range discards the saved block rather than reinterpreting old bytes against a
new table.

**A brief freeze when saving.** The F411 has no real EEPROM; flash emulation stalls the MCU for
about a second. That's why values apply to RAM instantly and flash is written only on an explicit
Save, and why the app's staleness watchdog allows three telemetry intervals before it complains.

**The board is bricked.** It isn't — BOOT0 + NRST reaches the ROM bootloader regardless of what
is in flash. See [First flash without an ST-Link](#first-flash-without-an-st-link).

## License

Copyright (C) 2026 mick-shed

This program is free software: you can redistribute it and/or modify it under the terms of the
**GNU General Public License, version 3** or (at your option) any later version, as published by
the Free Software Foundation. It is distributed in the hope that it will be useful, but **without
any warranty** — without even the implied warranty of merchantability or fitness for a particular
purpose. See [`LICENSE`](LICENSE) for the full text, or <https://www.gnu.org/licenses/>.

That covers the firmware, the backend and the web UI. If you fork this as the base for your own
board, the GPL comes with it: distributing a device running derived firmware means offering the
corresponding source to whoever you gave the device to.

Third-party components keep their own licenses and are not covered by the above:
Bootstrap and Alpine.js (both MIT) are vendored in `app/web/vendor/`, and ArduinoJson (MIT), the
GFX Library for Arduino and the STM32 Arduino core are pulled in at build time by PlatformIO.
All are GPL-compatible.
