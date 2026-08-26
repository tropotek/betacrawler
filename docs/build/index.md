# Choose your build

Betacrawler drives two kinds of vehicle. Answer two questions and follow the page you land on —
each one is a complete build, start to finish, with nothing to cross-reference.

## 1. How does it steer?

- **Skid steer** — two independently driven sides that turn by running at different speeds.
  Tracks, a 4WD rover, a 2WD skid-steer chassis. → [Skid steer](skid/index.md)
- **Car** — a drive motor plus a steering servo on the front wheels. → [Car](car/index.md)

## 2. What kind of motors?

- **Brushed** — two wires per motor, driven through an H-bridge module. Cheaper, simpler, lower
  power. Common on small and toy-derived chassis.
- **Brushless** — three wires per motor, driven through an ESC. More power, more speed, more cost,
  and the ESC has to be one that treats centre-stick as stop.

Each drive-mode page compares the two for that kind of vehicle.

## The four builds

| | Brushed | Brushless |
|---|---|---|
| **Skid steer** | [Skid + brushed](skid/brushed.md) | [Skid + brushless](skid/brushless.md) |
| **Car** | [Car + brushed](car/brushed.md) | [Car + brushless](car/brushless.md) |

Whichever you pick, you also need a computer running a Chromium-based browser — Chrome, Edge,
Brave or Opera. That is what the configurator runs in, and it is the only kind of browser that can
talk to the board. There is no programmer to buy and nothing to install.

Every build uses the same board, the same receiver wiring and the same flashing procedure. What
changes between them is the motor hardware, a handful of pins, and three or four settings.

Once your vehicle drives, [Add-ons](../addons/index.md) covers the optional hardware: pack voltage
sensing today, and more later.
