# What you need

These docs cover the electronics. They assume you already have a tracked chassis with two
independently driven motors — that part is yours to choose, and nothing here depends on which one
you picked.

## Parts

| Part | Qty | What matters |
|---|---|---|
| WeAct Black Pill, STM32F411CE or STM32F401CE | 1 | The USB-C revision. Both chips are supported — see [Which Black Pill](#which-black-pill) below. |
| ELRS receiver | 1 | Must expose a CRSF-capable output pad. Crossfire works too. |
| Brushless ESC, BLHeli-S | 2 | Must support **bidirectional** mode. One per track. |
| Brushless motor | 2 | Sized for your chassis, matched to the ESCs' current rating. |
| Power distribution board | 1 | With a 5V BEC. The reference build uses a Matek PDB with 5V and 12V outputs. |
| LiPo battery | 1 | Sized for the motors. Feeds the PDB, **not** the board directly. |
| USB-C cable | 1 | A data cable. Charge-only cables are a common and confusing failure. |
| ST-Link/V2 | 1 | For the first flash only. You can borrow one — it is not needed again. |

You also need a computer running a Chromium-based browser — Chrome, Edge, Brave or Opera. That is
what the configurator runs in, and it is the only kind of browser that can talk to the board.

## Which Black Pill

WeAct sells the same board with either an **STM32F411CE** or an **STM32F401CE** on it. Betacrawler
supports both, and ships a firmware image for each. The pinout, the wiring and every setting in
this documentation are identical — the F401 is the smaller chip (96 KB of RAM at 84 MHz against
the F411's 128 KB at 100 MHz), and nothing here needs the difference.

What you cannot do is flash one chip's image onto the other. The two have different memory maps,
and an F411 image on an F401 hard-faults before USB even comes up: the board goes dark and never
appears to your computer at all.

So check which one you have before the first flash. The chip's own marking is the only reliable
answer — read the top line on the square chip in the middle of the board, `STM32F411CEU6` or
`STM32F401CEU6`, with a magnifier if you need one. Boards sold as F411 that turn out to be
populated with an F401 are common enough to be worth ruling out; the silkscreen and the listing
are not evidence.

Once the board is running Betacrawler, the **Help** page reports which chip its firmware was built
for, and the **Firmware** page offers the matching image by default.

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

The battery goes to the power distribution board, and everything else takes its power from there:
the ESCs off the PDB's battery pads, the board and the receiver off its 5V BEC. USB powers the
board too, and can stay plugged in with the battery connected.

Never run the motors from the board's 5V pin — see [Wiring](wiring.md) for what happens if you
try, and for the grounding the ESCs need.

Next: [Wiring](wiring.md).
