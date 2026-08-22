# Betacrawler

![A Betacrawler](assets/tank-hero.png)

Betacrawler is a radio-controlled tracked vehicle. An STM32 board reads your RC receiver, mixes
the sticks into left and right track speeds, and drives two brushless ESCs. Everything about how
it drives — which channels the sticks live on, the speed and steering limits, the arming switch —
is set from a browser, with the vehicle plugged in over USB.

There is no firmware rebuild to change how it behaves. The board publishes what it can do, and
the app builds the controls from that.

## How the parts fit together

```
  STM32 board  ──USB serial──  browser
  reads the receiver,          the configurator,
  drives the ESCs              running as a web page
```

The configurator talks to the board directly, from the page:
**[tropotek.github.io/betacrawler/app/](https://tropotek.github.io/betacrawler/app/)**. There is
nothing to install and no server to run — but it needs a Chromium-based browser (Chrome, Edge,
Brave, Opera), because Chromium is the only engine that has the USB APIs it uses. Firefox and
Safari cannot drive a board, and the app says so on load rather than failing later.

## What you can do from the app

- **Set it up** — assign throttle and steering to receiver channels, pick the arming switch,
  calibrate both ESCs.
- **Tune it** — cap forward speed, reverse speed and steering authority, each independently.
- **Watch it live** — every receiver channel, link quality, and what each ESC is being told to do.
- **Flash and update the firmware** — over USB, from the browser, without a programmer. A blank
  board included.
- **Back up your settings** to a file, and restore them.

## Where to start

Work through the Build section in order — [What you need](build/what-you-need.md),
[Wiring](build/wiring.md), [Flashing the firmware](build/flashing.md) — then Drive it, where
[Connect to the board](drive/install-and-connect.md) opens the app and finds your vehicle, and
[First setup](drive/first-setup.md) decides whether it actually drives.

If something is not behaving, [Troubleshooting](troubleshooting.md) lists the usual causes.
