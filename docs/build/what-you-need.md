# What you need

These docs cover the electronics. They assume you already have a tracked chassis with two
independently driven motors — that part is yours to choose, and nothing here depends on which one
you picked.

## Parts

| Part | Qty | What matters |
|---|---|---|
| WeAct Black Pill, STM32F411CE | 1 | The USB-C revision. This is the board the firmware is built for. |
| ELRS receiver | 1 | Must expose a CRSF-capable output pad. Crossfire works too. |
| Brushless ESC, BLHeli-S | 2 | Must support **bidirectional** mode. One per track. |
| Brushless motor | 2 | Sized for your chassis, matched to the ESCs' current rating. |
| LiPo battery | 1 | Sized for the motors. Powers the ESCs, **not** the board. |
| USB-C cable | 1 | A data cable. Charge-only cables are a common and confusing failure. |
| ST-Link/V2 | 1 | For the first flash only. You can borrow one — it is not needed again. |

## Why the ESCs must be bidirectional

A tracked vehicle needs reverse, and it needs to be able to spin one track backwards while the
other goes forwards in order to pivot on the spot. In bidirectional mode the ESC treats
centre-stick as stop, above centre as forwards and below centre as reverse.

You set this in BLHeli Configurator, on the ESC itself. The firmware already expects it:
`esc0.direction` and `esc1.direction` both default to `bidirectional`.

An ESC left in its normal unidirectional mode will only ever drive one way, and the vehicle will
not steer.

## Receivers

The firmware speaks two protocols, both over CRSF:

- **ELRS** — recommended, and the default.
- **Crossfire** — the alternative, fully supported.

Nothing else is supported. A PWM, PPM, SBUS or IBUS receiver will not work without modifying the
firmware yourself.

## Power

The battery powers the ESCs. The board is powered over USB while you are configuring it.

Never run the motors from the board's 5V pin — see [Wiring](wiring.md) for what happens if you
try, and for the grounding you do still need between the two supplies.
