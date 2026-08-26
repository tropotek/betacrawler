## The receiver

The firmware speaks two protocols, both over CRSF:

- **ELRS** — recommended, and the default.
- **Crossfire** — the alternative, fully supported.

Nothing else works. A PWM, PPM, SBUS or IBUS receiver needs firmware changes of your own.

The receiver takes its 5 V and ground from the board, and two signal wires connect it:

| Signal | Board pin | Goes to |
|---|---|---|
| Receive | PA3 | The receiver's CRSF **TX** pad |
| Telemetry | PA2 | The receiver's CRSF **RX** pad |

PA2 carries telemetry back to your handset — pack voltage today, and anything added later.

Receive is on **PA3**, not the PA10 you may expect from USART1. The STM32's built-in bootloader
picks its host interface by watching several UART pins at once for a sync byte, and a receiver that
happens to send that byte at the wrong moment wins the race — leaving the board unable to appear in
DFU mode for flashing. A linked ELRS receiver never does, and putting CRSF here leaves PA9/PA10 as
a standard, unclaimed UART pair for your own additions.

Crossfire has not been checked against this. If the board stops appearing for flashing while the
receiver is powered, unplug the receiver's TX lead before entering DFU.

!!! warning "The receiver's pads ship in PWM mode"

    Out of the box, a receiver's output pads are configured as PWM servo outputs. One pad must be
    reassigned to CRSF in the receiver's own configuration menu. Until you do, nothing arrives on
    the wire at all and every channel reads empty — with no error to tell you why.
