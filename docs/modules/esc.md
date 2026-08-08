# ESC

**Type:** two hardware modules, sharing one pure-math library
(`firmware/src/hardware/esc/esc_math.h`/`.cpp`, no Arduino includes, native-tested) plus one
`core::Module` implementation per instance (`firmware/src/hardware/esc0/`,
`firmware/src/hardware/esc1/`).

Drives a brushless motor ESC. Betacrawler ships two independent instances, `esc0` and `esc1`, each
with its own `FEATURE_ESC0`/`FEATURE_ESC1` flag, its own parameters (`esc0.*`/`esc1.*` on the
wire), its own telemetry fields, and its own arm state — enabling or disabling one has no effect
on the other. On the reference board (`blackpill_f411ce.h`) both ship **on**: `esc0` on `TIM3_CH1`
/ `PA6`, `esc1` on `TIM4_CH1` / `PB6`.

## Why two modules instead of one with a channel count

Each `EscDriver` instance owns one physical timer peripheral end to end — construction, arming,
telemetry. Splitting the math that doesn't touch hardware (pulse-width clamping, arm-state
transitions, input staleness) into shared `esc::` free functions in `esc_math.h`/`.cpp`, and
leaving `esc0_driver.cpp`/`esc1_driver.cpp` as near-duplicate thin wrappers around that math, means
neither driver has to reason about the other's state, and the pattern extends to a third instance
by copying a folder rather than by changing shared logic. `esc0` and `esc1`'s `_params.cpp` files
are likewise near-identical, differing only in their key prefix (`esc0.`/`esc1.`) and telemetry key
suffix (`esc0`/`arm0` vs `esc1`/`arm1`).

## Board header

Each instance needs its own timer and pin macros:

```c
#define ESC0_TIMER  TIM3
#define ESC0_PIN    PA6      // TIM3_CH1

#define ESC1_TIMER  TIM4
#define ESC1_PIN    PB6      // TIM4_CH1
```

A board with only one ESC defines just `ESC0_TIMER`/`ESC0_PIN` and leaves `FEATURE_ESC1` at its
default-off.

## Why a separate timer per instance (and from Servo)

Two independently-constructed `HardwareTimer` objects sharing one physical peripheral would each
fight over its shared overflow/period register — true whether the two instances are `esc0` and
`esc1`, or an ESC and the Servo module. Every enabled ESC instance and Servo (if also enabled) on
one board must each claim a **different** timer peripheral. The reference board's own header
carries a compile-time `#error` guard that catches enabling Servo and `esc1` together, since both
would otherwise claim `TIM4`/`PB6` — see `blackpill_f411ce.h` for the exact condition. A board
defining its own pin map should add the equivalent guard for whichever timers it reuses.

`ESC0_FRAME_US`/`ESC1_FRAME_US` (20000, 50Hz), `ESC0_ARM_HOLD_MS`/`ESC1_ARM_HOLD_MS` (2000),
`ESC0_INPUT_STALE_MS`/`ESC1_INPUT_STALE_MS` (500) and `ESC0_ARM_LOW_MARGIN_US`/
`ESC1_ARM_LOW_MARGIN_US` (50) are all optional per instance, defaulted in
`esc0_driver.cpp`/`esc1_driver.cpp` respectively.

## Power it separately

**Power the motor/ESC from its own supply, never the board's 5V/VBUS pin.** An ESC under load
draws far more than even the Servo module's VBUS warning already covers. This applies to each ESC
instance independently.

## Turning one off

Set `FEATURE_ESC0 0` (or `FEATURE_ESC1 0`) in the board header and reflash. The other instance is
unaffected.

## Porting a fork from the old single-instance module

Earlier versions of betacrawler shipped one `esc` module. `FEATURE_ESC`, `ESC_TIMER`, `ESC_PIN`,
`ESC_FRAME_US`, `ESC_ARM_HOLD_MS`, `ESC_INPUT_STALE_MS` and `ESC_ARM_LOW_MARGIN_US` no longer
exist — rename them to their `ESC0_*` equivalents in your board header (or `ESC1_*`, if adding a
second instance) and reflash. This is a breaking rename: a board header still using the old names
will fail to compile once the module it referred to is gone.
