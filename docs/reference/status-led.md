# Status LED

The onboard LED tells you whether the firmware is running and healthy, without a computer
attached. It is the first thing to look at when a board does not behave.

| What you see | What it means |
|---|---|
| An even one-beat-per-second blink | Healthy. The firmware is running normally. |
| An even, much faster pulse (about five per second) | A fault. Connect the app and check the Fault value. |
| A very fast blink | The firmware crashed. Power-cycle the board. |
| Solid on, solid off, or never changing | Frozen, unflashed, or unpowered. |

## Why healthy blinks rather than sits on

A stopped board holds its pins wherever they were last written. An LED left solid on looks
identical whether the firmware is running perfectly or died half a second ago — the healthy signal
and the dead signal would be the same light.

An LED that is simply off is no better: off is also unpowered, unflashed, or a dead regulator. It
is the state the board is in *before* the firmware runs at all.

So the healthy signal is one a stopped board cannot fake. The blink only continues while the
firmware's main loop keeps running. If the light stops changing, the board stopped.

## Fault codes

When the LED shows a fault, connect the app and read the **Fault** value on the Configuration
page:

| Fault | What happened |
|---|---|
| `None` | No fault. This is what a healthy board reports. |
| `registry` | The firmware has more modules or parameters than it has room for, and something was dropped. |
| `panic` | The firmware crashed and the emergency handler took over. |

If two faults happen, the first one is kept — when one failure cascades into another, the root
cause is the one worth acting on.

The fault is also recorded at boot and replayed to the app when it connects, so a fault that
happened before you plugged in is not lost.
