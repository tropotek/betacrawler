# RX (Receiver)

**Type:** hardware module (`firmware/src/hardware/rx/`)

Decodes an RC receiver's serial link. On the reference board this is `USART1` — receiver TX into
`PA10`.

## Board header

```c
#define RX_RX_PIN       PA10
#define RX_TX_PIN       PA9        // reserved, unused in phase 1
#define RX_BAUD         420000
```

`RX_TX_PIN` is claimed but not yet used — sending telemetry back to the handset (battery, GPS,
flight mode) is the natural next use of this peripheral, and reserving the pin now is cheaper
than discovering it's taken later.

420000 baud is TBS's own spec value for the dual-wire vehicle-side link (Betaflight and most
others use 420000 too — 416666 and 420000 are 0.8% apart, well inside UART tolerance, and either
side talks to either). A board pairing with a straight 400k half-duplex link would change this in
its own header rather than in any source file.

## Protocol is a runtime setting, not a build flag

`rx.protocol` picks which protocol parses the wire (Crossfire, ELRS) **at runtime**. One receiver
is wired at a time, but swapping which brand of receiver is plugged in should never need a
reflash. This is also why the module is named `rx` for its **role**, not for a specific protocol:
a module that speaks a wire protocol is named after what it does (receive RC input), with the
protocol itself pushed down into a `proto_<name>.*` file inside the module (`proto_crsf.*`) —
because more than one receiver brand can speak the same wire format.

## Build requirement

Enabling `FEATURE_RX` requires `-D SERIAL_RX_BUFFER_SIZE=256` in that board's `platformio.ini`
env — the Arduino default RX ring (64 bytes) tears on nearly every frame at CRSF's ~150fps. The
driver `#error`s at compile time if this is missing or too small, rather than silently dropping
frames.

## Wiring

Receiver 5V and GND from the board's 5V pin; receiver CRSF TX → the board's `RX_RX_PIN`. Many
receivers (e.g. TBS Nano RX) default their pads to PWM output — one pad must be reassigned to
CRSF in the receiver's own configuration menu before anything appears on the wire at all.

## Turning it off

Set `FEATURE_RX 0` in the board header and reflash.
