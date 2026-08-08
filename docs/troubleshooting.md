# Troubleshooting

## The port doesn't appear

Charge-only USB cable, or missing udev rules (see [Getting Started](getting-started.md)). The
board's own USB is the USB-C connector — the ST-Link is a separate path and does not carry the
serial port.

## Upload grabs the wrong ST-Link

With two ST-Link/V2 units attached at once, pin the right one by adding
`upload_port = <device>` to `[env:blackpill_f411ce]` in `firmware/platformio.ini`.

## Editing a board header seemed to change nothing

It rebuilds correctly — `firmware/scripts/config_hash.py` exists precisely because a
macro-expanded include is invisible to the build system's dependency scanner. Don't remove the
`extra_scripts` line that runs it (see [Adding a Board](guides/adding-a-board.md)), or silent
stale binaries come back.

## Saved settings vanished after reflashing

Working as intended. A fingerprint over every parameter's key, type and bounds is stored with the
settings, so changing the enabled module set or a parameter's range discards the saved block
rather than reinterpreting old bytes against a new table.

## A brief freeze when saving

The F411 has no real EEPROM; flash emulation stalls the MCU for about a second. That's why values
apply to RAM instantly and flash is written only on an explicit Save, and why the app's staleness
watchdog allows three telemetry intervals before it complains.

## The board is bricked

It isn't — BOOT0 + NRST reaches the ROM bootloader regardless of what is in flash.

### First flash without an ST-Link

A board with no betacrawler firmware on it can't be asked to reboot into DFU over USB, so do it by
hand — the same procedure rescues a board whose firmware is broken:

1. Make sure there is an image to flash: `python3 app/tools/bundle_firmware.py blackpill_f411ce`
   (see [Getting Started](getting-started.md)). From a fresh clone the Firmware page is empty
   until you do this.
2. Hold **BOOT0**, tap **NRST**, release BOOT0. The board goes quiet rather than showing any sign
   of life — that is what DFU mode looks like.
3. On the app's Firmware page, press Flash. (`dfu-util -l` should list the board if you want to
   check first.)

> **Nothing can identify a board in DFU mode.** Every STM32F4 ROM bootloader reports `0483:df11`
> and nothing else — no board name, no version. The app carries the board string forward from the
> last connection and says plainly when it has none. Check you picked the right image; the
> bootloader cannot check for you.

## ESP32: flashing fails or hangs

ESP32 boards flash over their own USB-UART bridge with `esptool`, not DFU — a different path from
the STM32 boards above. Make sure nothing else has the serial port open (a monitor session, a
second app instance), and that the board isn't held in a bootloader-entry strap state it needs
manual button presses to exit.
