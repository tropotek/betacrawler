# LED

**Type:** feature module (`firmware/src/features/led/`)

The onboard status LED. On the reference board this is the Black Pill's built-in LED on `PC13`,
which the board **sinks** — driving the pin LOW turns the LED on, so `LED_ACTIVE_LOW` exists in
the board header precisely so the driver doesn't have to guess a board's wiring polarity.

## Board header

```c
#define LED_PIN         LED_BUILTIN
#define LED_ACTIVE_LOW  1
```

`LED_PIN` is deferred to the Arduino variant's own name (`LED_BUILTIN`) rather than a hardcoded
pin number, so it stays correct if the variant is ever revised. A board with no Arduino variant
would put a literal pin here instead.

## What it's for

Visual status feedback that doesn't need a connected app to be useful — the kind of thing you
glance at during bring-up, or leave blinking as a "the firmware is alive" heartbeat. Its exact
modes and parameters (blink rate, pattern, etc.) are set from the app's Configuration page; see
[Modules — Overview](overview.md#where-the-exact-parameters-live) for why they aren't repeated
here.

## Turning it off

Set `FEATURE_LED 0` in the board header and reflash — the module, its parameters and its UI
control disappear together.
