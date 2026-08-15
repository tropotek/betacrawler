# betacrawler

A Betaflight-Configurator-style tool for small microcontroller boards, and a template to fork
for your own.

The betacrawler on a PCB is the printed layer that tells you what every pad and pin actually is.
This does the same for firmware: the board declares its parameters and telemetry, and a browser
UI builds itself from that declaration. Adding a setting to the firmware makes a control appear
in the app. There is no second list to keep in sync.

Three tiers:

```
  STM32 board  ──USB serial──  Python backend  ──HTTP+WebSocket──  browser UI
  (firmware/)   JSON lines      (app/backend/)                      (app/web/)
```

The reference board throughout this site is a WeAct **Black Pill (STM32F411CE)** with an LED, a
button and an optional ST7789 240x240 panel. Other boards are called out only where they differ.

## What you get

- **Live config** — every firmware parameter as a form control, validated on the device as well
  as in the app. Values apply instantly; flash is written only when you press Save.
- **Telemetry** — pushed from the board at a configurable rate, rendered as cards.
- **Terminal** — type `get rx.protocol`, `set rx.deadband_us 5`, `save`; see the raw JSON both ways.
- **Firmware updates in-app** — the app carries an image matching its own version and flashes it
  over USB DFU (or `esptool` on ESP32), no ST-Link needed after the first time.
- **Settings backup/restore** as INI files.
- **An on-device dashboard** on the optional SPI panel.

Start with [Getting Started](getting-started.md), then see the [Modules](modules/overview.md)
section to add or write your own module, or [Adding a Board](guides/adding-a-board.md) to port
to different hardware.
