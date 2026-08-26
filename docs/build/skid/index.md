# Skid steer

A skid-steer vehicle has two independently driven sides and no steering linkage at all. It turns
by running one side faster than the other, and pivots on the spot by running them in opposite
directions. Tracks, a 4WD rover and a 2WD skid-steer chassis are all the same build as far as the
electronics are concerned.

Two consequences shape the whole build:

- **Both sides need reverse.** A pivot runs one side backwards while the other goes forwards. Any
  motor driver that only goes one way cannot steer this vehicle.
- **The two sides should be matched.** They are commanded independently, and the mixer assumes
  equal response. Mismatched gearing or motors show up as a vehicle that will not hold a straight
  line, which no amount of tuning fully hides.

The board drives the left side from `motor0` and the right from `motor1`. If they turn out swapped
once you are driving, you swap them in software rather than rewiring.

## Brushed or brushless?

| | Brushed | Brushless |
|---|---|---|
| Wires per motor | 2 | 3 |
| Driver | One dual H-bridge module for both sides | One ESC per side |
| Typical cost | Low | Higher — two ESCs plus two motors |
| Power | Modest | High, and easy to over-power a small chassis with |
| Reverse | Native — the H-bridge reverses polarity | Only if the ESC is a surface ESC or set to bidirectional |
| Extra setup | Fit pulldowns on the H-bridge inputs | Configure each ESC for bidirectional, if it is a drone ESC |
| Power source | The pack feeds the H-bridge directly | The pack feeds a distribution board, which feeds both ESCs |

**Brushed** suits a small or toy-derived chassis, a first build, and anything where you would
rather spend the effort on the vehicle than on the electronics. One module drives both sides, and
reverse comes free.

**Brushless** suits a larger chassis that needs real torque, and it is the only sensible choice
once the vehicle is heavy enough that a brushed motor would stall. The cost is two ESCs, a power
distribution board, and making sure each ESC treats centre-stick as stop — a stock drone ESC does
not.

A pivot stalls both sides against each other, so whichever you pick, size the driver for stall
current rather than for running current.

Pick one:

- [Skid steer with brushed motors](brushed.md)
- [Skid steer with brushless motors](brushless.md)
