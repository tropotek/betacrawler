# Flashing the firmware

The first flash needs an ST-Link. After that, the app updates the board over USB and you will not
need the programmer again.

## Why the first one is different

The app's one-click firmware update works by asking the running firmware to reboot into the
STM32's built-in USB bootloader. A blank board has no firmware to ask, so the first image has to
go in over SWD with a programmer.

## Wiring the ST-Link

Four wires from the ST-Link/V2 to the header on the short edge of the board:

| ST-Link | Board |
|---|---|
| SWDIO | PA13 |
| SWCLK | PA14 |
| GND | GND |
| 3.3V | 3V3 |

## Build and upload

PlatformIO does both. It is not on your `PATH` by default, so call it by its full path:

```bash
cd firmware
~/.platformio/penv/bin/pio run -e blackpill_f411ce
~/.platformio/penv/bin/pio run -e blackpill_f411ce -t upload
```

The first command compiles and the second flashes. If you have two ST-Link units plugged in and
it grabs the wrong one, add `upload_port = <device>` under `[env:blackpill_f411ce]` in
`firmware/platformio.ini`.

## Checking it worked

Unplug the ST-Link and power the board over USB. The onboard LED should settle into an even
one-beat-per-second blink — that is the firmware reporting itself healthy. Anything faster means
a fault; see [Status LED](../reference/status-led.md).

## Updating later

Once the board runs Betacrawler, updates go through the app's **Firmware** page over USB. No
programmer, no wires to move.

That page stays available even when no device is connected, on purpose: it is the recovery tool,
so gating it on a working device would be backwards.

If a board ever becomes unresponsive and will not appear for an update, you can reach the same
built-in bootloader by hand: hold **BOOT0**, tap **NRST**, release **BOOT0**.

Next: [Install and connect](../drive/install-and-connect.md).
