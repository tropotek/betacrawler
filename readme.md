# BetaCrawler

![BetaCrawler](docs/assets/hero.png)

**This project is currently under construction. If you want to contribute hit up our disscussion forum and get involved.**

----

BetaCrawler is a radio-controlled vehicle. An STM32 Black Pill reads an ELRS receiver, mixes the
sticks into motor commands, and drives your motors — two independently driven sides for a
skid-steer or tracked model, or a drive motor and a steering servo for a car. You set it up and
tune it from a browser, with the vehicle plugged in over USB — channel assignment, speed and
steering limits, the arming switch, firmware updates.

**[Read the docs →](https://tropotek.github.io/betacrawler/)**

## What you need

A Black Pill (STM32F411CE or STM32F401CE), an ELRS receiver, motors with either brushless ESCs in
bidirectional mode or a brushed H-bridge module, a battery, and a chassis. There are four build
paths — skid-steer or car, brushed or brushless — each with its own parts list and wiring:
**[Choose your build](https://tropotek.github.io/betacrawler/build/)**.

## The configurator

**[tropotek.github.io/betacrawler/app/](https://tropotek.github.io/betacrawler/app/)** — a web page
that talks to the board over USB. Nothing to install, no server to run; it needs a Chromium-based
browser (Chrome, Edge, Brave, Opera), because Web Serial and WebUSB are what it drives the board
with.

## Contributing

Want to work on the firmware, the web app, or these docs?
**[Contributing →](https://tropotek.github.io/betacrawler/development/contributing/)** covers
setup, filing issues, and opening a PR.

## License

Copyright (C) 2026 Micks Shed

This program is free software: you can redistribute it and/or modify it under the terms of the
**GNU General Public License, version 3** or (at your option) any later version, as published by
the Free Software Foundation. It is distributed in the hope that it will be useful, but **without
any warranty** — without even the implied warranty of merchantability or fitness for a particular
purpose. See [`LICENSE`](LICENSE) for the full text, or <https://www.gnu.org/licenses/>.

That covers the firmware and the configurator. If you fork this as the base for your own
board, the GPL comes with it: distributing a device running derived firmware means offering the
corresponding source to whoever you gave the device to.

Third-party components keep their own licenses and are not covered by the above: Bootstrap and
Alpine.js (both MIT) are vendored in `web-app/vendor/`, and ArduinoJson (MIT), the GFX Library
for Arduino and the STM32 Arduino core are pulled in at build time by PlatformIO. All are
GPL-compatible.
