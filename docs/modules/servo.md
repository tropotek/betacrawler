# Servo

**Type:** hardware module (`firmware/src/hardware/servo/`)

Drives a hobby servo. On the reference board (`blackpill_f411ce.h`) this is `TIM4_CH1` on `PB6`.

**Ships off by default on the reference board — and on that board, cannot simply be turned on.**
The tank build enables `esc1` (see [ESC](esc.md)) on that same `TIM4_CH1`/`PB6`, so the header
carries a compile-time `#error` guard that fires if `FEATURE_SERVO` and `FEATURE_ESC1` are both set
to `1` there. Setting `FEATURE_SERVO 1` on `blackpill_f411ce.h` requires first moving one of the
two to a different timer/pin — see the guard and its comment in the board header for the exact
condition.

## Board header

```c
#define SERVO_TIMER     TIM4
#define SERVO_PIN       PB6      // TIM4_CH1
```

The timer instance is named explicitly rather than derived from the pin, so which timer a board
claims for servo output is greppable across the codebase. The channel *is* derived from the pin —
the two must agree, and nothing checks that for you at compile time.

`SERVO_FRAME_US` is optional (defaults to 20000, i.e. 50Hz, in the driver); raise it only for a
digital servo that documents a faster frame rate.

## Power it separately

**Power the servo from 5V (USB VBUS), never 3V3**, with a 470–1000µF bulk capacitor at the
connector. A moving servo's current draw can droop VBUS far enough to reset the MCU and drop the
USB CDC link — which shows up as a mysterious configurator disconnect, not as anything obviously
electrical.

## Turning it off

Set `FEATURE_SERVO 0` in the board header and reflash.
