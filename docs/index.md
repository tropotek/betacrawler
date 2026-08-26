# BetaCrawler

![A BetaCrawler](assets/hero.png)

BetaCrawler is a radio-controlled vehicle. An STM32 board reads your RC receiver, mixes the
sticks into motor commands, and drives your motors — two independently driven sides for a
skid-steer or tracked model, or a single drive motor and a steering servo for a car. Everything
about how it drives — which channels the sticks live on, the speed and steering limits, the
arming switch — is set from a browser, with the vehicle plugged in over USB.

There is no firmware rebuild to change how it behaves. The board publishes what it can do, and
the app builds the controls from that.

## How the parts fit together

```
  STM32 board  ──USB serial──  browser
  reads the receiver,          the configurator,
  drives the motors            running as a web page
```

The configurator talks to the board directly, from the page:
**[tropotek.github.io/betacrawler/app/](https://tropotek.github.io/betacrawler/app/)**. There is
nothing to install and no server to run — but it needs a Chromium-based browser (Chrome, Edge,
Brave, Opera), because Chromium is the only engine that has the USB APIs it uses. Firefox and
Safari cannot drive a board, and the app says so on load rather than failing later.

## What you can do from the app

- **Set it up** — assign throttle and steering to receiver channels, pick the arming switch,
  calibrate your motor outputs.
- **Tune it** — cap forward speed, reverse speed and steering authority, each independently.
- **Watch it live** — every receiver channel, link quality, and what each motor is being told to do.
- **Flash and update the firmware** — over USB, from the browser, without a programmer. A blank
  board included.
- **Back up your settings** to a file, and restore them.

## Where to start

[Choose your build](build/index.md) asks two questions — how your vehicle steers, and what kind of
motors it has — and sends you to one of four complete build guides. Each covers its own parts,
wiring and settings end to end.

After that, [Connect to the board](drive/install-and-connect.md) opens the app and finds your
vehicle, and [First setup](drive/first-setup.md) decides whether it actually drives.

If something is not behaving, [Troubleshooting](troubleshooting.md) lists the usual causes.
