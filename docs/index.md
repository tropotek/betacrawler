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
  STM32 board  ──USB serial──  backend  ──HTTP+WebSocket──  browser
  reads the receiver,          bridges the                  the app you
  drives the ESCs              serial link                  configure with
```

## What you can do from the app

- **Set it up** — assign throttle and steering to receiver channels, pick the arming switch,
  calibrate both ESCs.
- **Tune it** — cap forward speed, reverse speed and steering authority, each independently.
- **Watch it live** — every receiver channel, link quality, and what each ESC is being told to do.
- **Update the firmware** — over USB, without a programmer, once the board has been flashed the
  first time.
- **Back up your settings** to a file, and restore them.

## Where to start

Work through the Build section in order, then Drive it. Build gets you from a box of parts to a
board with firmware on it; Drive it gets the app talking to the board and the vehicle moving the
way you want.
