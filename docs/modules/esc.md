# ESC

**Type:** hardware module (`firmware/src/hardware/esc/`)

Drives a brushless motor ESC. On the reference board this is `TIM3_CH1` on `PA6` — deliberately a
**different timer** from the Servo module's `TIM4`.

## Board header

```c
#define ESC_TIMER  TIM3
#define ESC_PIN    PA6      // TIM3_CH1
```

## Why a separate timer from Servo

Two independently-constructed `HardwareTimer` objects sharing one physical peripheral would each
fight over its shared overflow/period register. If your board enables both Servo and ESC, give
them different timers, the way the reference board does.

`ESC_FRAME_US` (20000, 50Hz), `ESC_ARM_HOLD_MS` (2000), `ESC_INPUT_STALE_MS` (500) and
`ESC_ARM_LOW_MARGIN_US` (50) are all optional, defaulted in `esc_driver.cpp`.

## Power it separately

**Power the motor/ESC from its own supply, never the board's 5V/VBUS pin.** An ESC under load
draws far more than even the Servo module's VBUS warning already covers.

## Turning it off

Set `FEATURE_ESC 0` in the board header and reflash.
