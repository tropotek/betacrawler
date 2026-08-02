# Wiring the Display

**The display module ships off by default on the reference board.** Set `FEATURE_ST7789_240X240 1`
in the board header and reflash once it's wired, or the panel will sit connected but dark.

Only the panel needs wiring on the reference board — the LED (PC13) and button (PA0) are already
on the Black Pill. ST7789 240x240 on hardware SPI1:

| Panel | Black Pill |
|---|---|
| `SCL` / `SCK` | PA5 |
| `SDA` / `MOSI` | PA7 |
| `DC` | PB1 |
| `RES` | PB0 |
| `BLK` | 3V3 |
| `VCC` / `GND` | 3V3 / GND |

There is no CS and no backlight control. Most GMT130-style panels bring no CS pin out at all, and
tying `BLK` straight to 3V3 means the backlight is always on.

## No presence detection

**Nothing detects whether the panel is plugged in.** There is no MISO line to read a controller
ID back over, so the driver is write-only and the board runs identically with the panel absent.
Don't wait for a "display not found" warning — the firmware cannot honestly produce one.

## SPI speed

The driver defaults `DISPLAY_SPI_HZ` to 24MHz on the reference board (the STM32F411's SPI1
prescaler quantises this to a clean 96/4 division). If a long or noisy ribbon cable shows
artifacts, lower it in the board header. Going too low has a real cost, not just a theoretical
one: at 8MHz (which quantises down to 6MHz) a cycle-mode page flip stalled telemetry for 353ms —
past the frontend's 300ms staleness threshold at `tlm.rate 10`, producing a false "stale" badge
every 5 seconds.

## Not using a panel?

Set `FEATURE_ST7789_240X240 0` in the board header and reflash: the code, its parameters and its
UI controls all disappear together. See the [Display module page](../modules/display.md) for
what the module itself does.
