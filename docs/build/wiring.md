# Wiring

Five connections: two ESC signals, the receiver, the battery, and USB to your computer.

![Wiring a Black Pill to two ESCs and a CRSF receiver](../assets/screenshots/wiring-diagram.png)

This is a schematic map, not a picture of the board. Match connections by pin label, not by
position on the diagram.

## Pin map

| Signal | Board pin | Goes to |
|---|---|---|
| ESC 0 | PA6 | ESC 0 signal wire — the **left** track |
| ESC 1 | PB6 | ESC 1 signal wire — the **right** track |
| Receiver | PA10 | The receiver's CRSF **TX** pad |
| Receiver power | 5V, GND | The receiver's + and − |
| Status LED | PC13 | On the board already, nothing to wire |

PA9 is wired on the diagram but does nothing today. It is the board's CRSF transmit line,
reserved for sending telemetry back to your handset later.

Both ESC grounds connect to the board's ground.

!!! warning "Power the ESCs from the battery, never from the board's 5V pin"

    An ESC under load draws far more current than the board's 5V rail can supply. The board
    browns out, USB drops, and the app shows a disconnect — so it looks like a software or cable
    problem rather than the electrical one it is.

    The ESCs still need a **common ground** with the board even on a separate supply, or the
    signal wires have no reference and the ESCs read noise.

!!! warning "The receiver's pads ship in PWM mode"

    Out of the box, a receiver's output pads are configured as PWM servo outputs. One pad must be
    reassigned to CRSF in the receiver's own configuration menu. Until you do, nothing arrives on
    the wire at all and every channel reads empty — with no error to tell you why.

## Which track is which

`esc0` drives the left track and `esc1` the right. If they turn out swapped once you are driving,
you do not need to rewire: change `esc0.src` and `esc1.src` between `drive_left` and
`drive_right` in the app.

If a single track runs backwards, swap any two of the three motor wires on that ESC.

Next: [Flashing the firmware](flashing.md).
