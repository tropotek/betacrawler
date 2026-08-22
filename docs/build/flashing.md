# Flashing the firmware

You do not need a programmer. Every STM32 carries a USB bootloader in ROM, and the configurator
writes to it directly from the browser — including the very first time, on a board whose flash is
still empty.

## Put the board in bootloader mode

Hold **BOOT0**, tap **NRST**, release **BOOT0**.

Both buttons are on the board. It then disappears as a serial port and appears as a DFU device
instead. Nothing about this depends on what is in flash, which is why the same three keystrokes
are also how you rescue a board whose firmware is broken.

## Flash it

Open **[the configurator](https://tropotek.github.io/betacrawler/app/)** and go to the
**Firmware** page. It works with nothing connected, on purpose.

![The configurator's Firmware page, offering an image for each supported board](../assets/screenshots/firmware.png)

1. Click **Select DFU device…** and pick the STM32 bootloader in the browser's chooser. It is
   remembered after the first time.
2. Choose the image for your chip — `blackpill_f411ce` or `blackpill_f401ce`. **Pick this
   yourself.** A board in DFU mode cannot say what it is: every STM32F4 bootloader reports the
   same USB identity, so the app has nothing to recommend from until it has spoken to the board
   over serial. See [Which Black Pill](what-you-need.md#which-black-pill) if you are not sure
   which one you have.
3. Click **Flash selected firmware**. The board resets into the new firmware on its own.

Only have the one board you are flashing plugged in — for the same reason the app cannot pick the
image for you, it cannot tell two bootloaders apart either.

On Windows, a board in bootloader mode has to be bound to the WinUSB driver before a browser can
see it. Use [Zadig](https://zadig.akeo.ie/) once, on the `STM32 BOOTLOADER` device. Linux and
macOS need nothing.

## Checking it worked

The onboard LED should settle into an even one-beat-per-second blink — that is the firmware
reporting itself healthy. Anything faster means a fault; see
[Status LED](../reference/status-led.md).

Then connect the configurator and open the **Help** page: it names the board the running firmware
was built for, which is the confirmation that you flashed the right image.

## Updating later

Once the board runs Betacrawler you do not need BOOT0 and NRST either. With the board connected,
the **Firmware** page offers the image matching it, reboots it into the bootloader itself, writes
the image and lets it restart — one click, no buttons.

The manual route above stays the fallback, and it is the one that works when the firmware on the
board is too broken to reboot itself.

## Building the firmware yourself

Only needed if you are changing the firmware. There is one build environment per Black Pill
variant, and they are not interchangeable:

| Your chip | Environment |
|---|---|
| STM32F411CE | `blackpill_f411ce` |
| STM32F401CE | `blackpill_f401ce` |

PlatformIO does the build. It is not on your `PATH` by default, so call it by its full path:

```bash
cd firmware
~/.platformio/penv/bin/pio run -e blackpill_f411ce
```

That writes `.pio/build/blackpill_f411ce/firmware.bin`. Flash it the same way as a shipped image:
put the board in DFU, then use **Advanced: flash a local file** on the Firmware page. Pick the
`.bin` — an `.elf` or `.hex` from the same folder is rejected, since flashing one would leave the
board unbootable.

An ST-Link on SWD (PA13/PA14) is worth having if you want to step through firmware in a debugger.
It is not needed to flash.

Next: [Connect to the board](../drive/install-and-connect.md).
