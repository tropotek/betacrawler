# Car

A car has a driven axle and a steering servo on the front wheels. The board sends throttle to the
motor outputs and steering to the servo, and the two are independent — unlike a skid-steer
vehicle, where steering and throttle are mixed together before they reach either motor.

Two things are worth knowing before you pick a motor type:

- **Both motor outputs carry throttle.** It makes no difference which of the two pins a single
  motor is wired to, and a two-wheel-drive car works by wiring both.
- **The servo needs its own 5 V.** It shares a ground with the board but never takes power from
  the board's own 5 V pin, and never from 3V3.

A donor RC car is often the cheapest way in: the chassis, steering servo and motor are already
there, and only the electronics change.

## Brushed or brushless?

| | Brushed | Brushless |
|---|---|---|
| Wires per motor | 2 | 3 |
| Driver | One H-bridge module | One ESC |
| Typical cost | Low | Higher |
| Power | Modest | High |
| Reverse | Native — the H-bridge reverses polarity | Only if the ESC is a surface ESC or set to bidirectional |
| Servo supply | A separate 5 V BEC | Usually the ESC's own BEC |
| Extra setup | Fit pulldowns on the H-bridge inputs | Configure a drone ESC for bidirectional |

**Brushed** is what most donor cars already have, and it needs the least new hardware. The
trade-off is that you must supply 5 V for the servo yourself, because there is no ESC BEC to
borrow.

**Brushless** is the choice for speed, and it simplifies the power side: a surface ESC treats
centre-stick as stop with no configuration, and its BEC powers the board, the receiver and the
servo from one place.

Pick one:

- [Car with a brushed motor](brushed.md)
- [Car with a brushless motor](brushless.md)
