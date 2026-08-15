# Status LED

The onboard LED reports firmware health. It is **not** a module: it has no parameters and no
telemetry, so it is wired directly in `main.cpp` alongside `FlashStore` and `DfuTrigger` rather
than through the registry.

| State | Pattern |
|---|---|
| Healthy | On 950ms, off 50ms — reads as solid on, with a brief wink each second |
| Fault | An even ~5Hz pulse |
| Hard fault | A much faster blink, driven by the exception handler itself |
| Frozen | Latched fully on or fully off — never winking |

## Why the healthy signal winks

A stopped board holds its pins in whatever state they were last written. So an LED left solid on
looks identical whether the firmware is running perfectly or died four hundred milliseconds ago —
the healthy signal and the dead signal are the same photons.

An LED that is simply off is no better: off is also unpowered, unflashed, a dead regulator, or a
pin never configured. It is the state the board is in *before* the firmware runs.

The healthy signal therefore has to be one a stopped loop cannot counterfeit. The wink costs
nothing to read — at a glance the LED looks solid — but it only happens if `loop()` is still
calling `tick()`.

## Board header

```c
#define FEATURE_STATUS_LED  1
#define LED_PIN         LED_BUILTIN
#define LED_ACTIVE_LOW  1
```

`LED_PIN` is deferred to the Arduino variant's own name (`LED_BUILTIN`) rather than a hardcoded pin
number, so it stays correct if the variant is ever revised. A board with no Arduino variant would
put a literal pin here instead. `LED_ACTIVE_LOW` exists because the reference board **sinks** its
LED on `PC13` — driving the pin LOW turns it on — and the driver should not have to guess a
board's wiring polarity.

With `FEATURE_STATUS_LED 0` the whole thing compiles to nothing.

## Fault codes

`core::Fault` is the one health verdict, recorded by `core::health()`:

| Code | Name | Raised by |
|---|---|---|
| 0 | none | — |
| 1 | registry | A module did not fit `FW_MAX_MODULES` / `FW_MAX_PARAMS` / `FW_MAX_TLM` and was dropped |
| 2 | panic | The hard fault handler |

**First fault wins.** When one fault cascades into another, the root cause is the actionable one.

The code reaches the app two ways: as a `boot: fault=<name>` line in the boot record, replayed on
every `hello`, and as the `fault` field in the telemetry frame, which the Configuration page
renders by name.

## The panic handler

`src/status_led.cpp` overrides the Arduino core's weak `HardFault_Handler`, which otherwise falls
through to an infinite loop that leaves the board frozen and silent.

It cannot use `delay()` or `millis()`. HardFault runs at priority −1, which masks every interrupt
that advances the tick, so `delay()` would never return. The blink is a bare counting loop instead.

One limit: a crash during static initialisation, before `begin()` has run `pinMode()`, still shows
nothing. That window is a few hundred instructions.

## Adding a pattern

Patterns are a list of alternating on/off durations in milliseconds, starting on, played by
`core::patternState()`. A new one per fault code is a table entry, not new logic — for example
`{150, 150, 150, 600}` blinks twice then pauses.
