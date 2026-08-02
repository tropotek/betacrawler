# Button

**Type:** hardware module (`firmware/src/hardware/button/`)

A user input button. On the reference board this is the Black Pill's `KEY` button on `PA0`,
pulled up.

## Board header

```c
#define BUTTON_PIN      USER_BTN
```

## Polarity is sampled, not assumed

The driver samples the pin's **idle level at boot** rather than hardcoding whether the button is
active-high or active-low. That matters if you wire an external button with different pull
direction than the onboard one — the driver adapts rather than requiring a header flag to match
your wiring.

## What it's for

The simplest possible input source for triggering an action from firmware — useful during
bring-up, or as a physical override that doesn't depend on the app being connected. Its exact
behavior (what a press does) is configured from the app; see
[Modules — Overview](overview.md#where-the-exact-parameters-live).

## Turning it off

Set `FEATURE_BUTTON 0` in the board header and reflash.
