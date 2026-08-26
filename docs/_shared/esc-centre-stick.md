## The ESC must treat centre-stick as stop

The firmware always commands 1500 µs for stop, above that for forwards and below it for reverse.
The ESC has to agree, or nothing else works — a vehicle needs reverse.

Two kinds of ESC do this:

- A **surface ESC**, sold for cars and boats. Centre-stick neutral is how they already behave —
  nothing to configure.
- A **BLHeli_S drone ESC set to bidirectional** (also called 3D mode) in BLHeli Configurator. Out
  of the box these treat 1000 µs as stop and only drive one way; switching to bidirectional moves
  neutral to centre.

An ESC left in its default aircraft mode will not arm on this firmware, because it never sees the
low throttle it waits for at power-on.

New to flashing and configuring BLHeli_S ESCs? Oscar Liang's
**[connecting and flashing BLHeli_S ESCs guide](https://oscarliang.com/connect-flash-blheli-s-esc/)**
covers it well. You don't need BLHeli Configurator installed either — the browser-based
**[ESC Configurator](https://esc-configurator.com/)** talks to the ESC directly, the same way
Betacrawler's own configurator talks to the board.
