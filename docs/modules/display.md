# Display (ST7789 240x240)

**Type:** hardware module (`firmware/src/hardware/st7789_240x240/`)

Drives an on-device dashboard on an ST7789 240x240 SPI panel. Full wiring table and SPI-speed
notes are in [Wiring the Display](../guides/wiring-the-display.md) — this page covers the
module's design, not its pinout.

**Ships off by default on the reference board.** Set `FEATURE_ST7789_240X240 1` in the board
header and reflash to use it.

## One module per panel, named for the part

The module is named after its controller and resolution (`st7789_240x240`), not generically
`display`. A different screen gets its own module and its own `FEATURE_` flag, so two display
modules can never be enabled at once by accident — the naming convention is what prevents that
class of mistake.

## Write-only, on purpose

The module has no way to detect whether a panel is actually connected, because this wiring has no
MISO line to read a controller ID back over. The driver only ever writes to the panel, which
costs the same whether one is plugged in or not — see
[Wiring the Display](../guides/wiring-the-display.md#no-presence-detection) for the full
reasoning. Don't add a "panel not found" check; it can't be made honest on this hardware.

## Turning it off

Set `FEATURE_ST7789_240X240 0` in the board header and reflash — the code, its parameters and its
UI controls all disappear together.
