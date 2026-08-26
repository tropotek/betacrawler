# Add-ons

Your vehicle drives without any of these. Each one is self-contained: a small amount of hardware, a
few wires onto pins your base build left free, and a setting or two. Add them in any order, once
the vehicle drives.

If you have not built anything yet, start at [Choose your build](../build/index.md).

| Add-on | What it gives you | Board pin |
|---|---|---|
| [Battery sense](battery-sense.md) | Pack voltage on your handset and in the Configurator | PA1 |

## Pins still free after a base build

| Pin | Free after |
|---|---|
| PA1 | every build |
| PB10 | skid builds — a car uses it for the steering servo |
| PA9, PA10 | every build — a standard, unclaimed UART pair |
| PA7, PB9 | brushless builds — a brushed build uses them as H-bridge inputs |

Each add-on page says which pin it wants and what else it needs from your base build.
