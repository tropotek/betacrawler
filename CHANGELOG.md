# Changelog

Summaries of completed work, one to two lines each. Detail, reasoning and hardware-verification
records live in the git history, not here.

## Version 4.0

- **docs: flashing needs no programmer, first flash included.** BOOT0+NRST reaches a blank
  board's ROM bootloader, so the ST-Link leaves the parts list.

- **feat: the configurator is published alongside the docs**, at
  `https://tropotek.github.io/betacrawler/app/`.

- **docs: user docs rewritten around the browser app**, covering the F401 as a board option;
  `dev-docs/api.md` replaced by `dev-docs/protocol.md`.

- **feat: pages needing a board (Configuration, Controller, Modes, Terminal) live in their own
  sidebar section**, hidden while disconnected rather than greyed out. Terminal gains the shared
  Save/Discard/Load-defaults bar.

- **feat: `web-app/` shows a modal on load when the browser cannot drive a board**, replacing a
  banner that cleared itself after five seconds. Missing WebUSB warns about flashing without
  blocking the rest of the app.

- **feat: `web-app/` flashes firmware over WebUSB**, no backend or `dfu-util` needed. `js/dfu.js`
  implements DfuSe against an injected `USBDevice`; both DFU entry paths (`dfu` op and
  BOOT0+NRST) work.

- **feat: the firmware images `web-app/` ships are committed to the repo.** `bundle_firmware.py`
  writes `web-app/firmware/` and records a source hash that a guard test checks against.

- **feat: `web-app/` is a standalone browser configurator**, talking to a board directly over Web
  Serial. No build step, no npm dependencies; installable as a PWA.

- **fix: the ST7789 240x240 display module is removed.** No board ships it enabled; driver,
  params, pin maps and the GFX library dependency are gone.

- **test: board-header parity is enforced.** `test_board_headers` asserts `blackpill_f411ce.h`
  and `blackpill_f401ce.h` carry the same `FEATURE_` flags with the same values.

- **fix: a board with no divider fitted no longer reports a phantom battery.** `vbat` now probes
  the sense pin with internal pull-up/pull-down at startup and reports "off" if nothing is wired.

- **fix: `blackpill_f401ce` ships what `blackpill_f411ce` ships.** `vbat`, `tank_drive` and both
  ESC frame periods had drifted between the two headers; now identical apart from `BOARD_ID`.

- **feat: the Configuration page gains a Battery card** — pack voltage, volts per cell, remaining
  percent and cell count. `FW_MAX_TLM` grows 40 → 48.

- **fix: CRSF receive moves from PA10 to PB7**, so USB DFU works with a receiver connected — the
  ROM bootloader's auto-select otherwise commits to whichever UART sees traffic first. Wiring
  change: receiver TX now goes to PB7.

- **feat: the board can measure pack voltage and send it to the handset.** New `vbat` module reads
  a divider on PA1 and publishes pack millivolts, cell count and remaining percent onto a
  `core::Battery` bus; `rx` forwards it as a CRSF battery-sensor frame.

- **chore: the outbound line budget grows from 7168 to 8192 bytes**, so the schema response fits
  with the battery module registered.

- **firmware: both ESCs default to a 200Hz PWM frame**, down from 50Hz, on `blackpill_f411ce.h`.
  Existing saved configurations are unaffected.

- **fix: the control loop no longer stalls 200ms at a time when nothing is listening on USB.**
  `writeLine()` now tells "no host" apart from "host mid-packet" and skips the retry when nothing
  is listening.

- **docs: the wiring diagram now shows the whole power chain** — LiPo → PDB → ESCs → motors —
  rather than stopping at the two ESC signal leads and the receiver.

- **docs: the documentation site is now a Betacrawler build guide**, not the upstream template's
  docs with the project name substituted in. Twelve new pages across four sections; parameter
  tables generated from the firmware's golden schema.

- **fix: four stale statements in the app and firmware corrected** — a leftover "betacrawler
  layout" phrase, an outdated no-mixing claim, a nonexistent Terminal example param, and a Home
  page link to a page the nav doesn't have.

- **feat: a simulated board can be selected instead of a real one.** `sim://board` runs an
  in-process device behind the same JSON-lines protocol, so every page works with no hardware
  attached.

- **feat: forward and steer ratios join reverse ratio in the drive mixer.** New
  `tank_drive.forward_ratio`/`steer_ratio`, both 0–100%, defaulting to 100.

- **fix(firmware): tank drive defaults to throttle=ch2, steer=ch1**, matching a Mode 2 handset's
  stick layout.

- **feat: the ESC PWM frame rate is selectable, 50/100/200/400Hz.** New `esc0.rate`/`esc1.rate`
  parameters, one shared **PWM Rate** control in the Configuration page.

- **feat(firmware): loop rate and worst-pass time are system telemetry.** New `loop` (Hz) and
  `loopworst` (µs) fields in the Configuration page's System card.

- **feat(firmware): the onboard LED is a firmware health indicator, not a configurable module.**
  Blinks 1Hz when healthy, faster on a fault. Breaking for forks: `FEATURE_LED` →
  `FEATURE_STATUS_LED`, `led.mode`/`led.blink_hz` removed.

- **fix(firmware): a module that doesn't fit the registry now raises a fault instead of
  vanishing.** `Registry::add()` raises `Fault::Registry` on overflow rather than silently
  dropping the module.

- **feat(web): the Configuration page names the firmware's fault**, rendered in red when
  non-zero.

## Version 1.0.0 (2026-07-29)

- **feat(firmware): split the single-instance `esc` module into independent `esc0`/`esc1`
  modules**, enabled by default on `blackpill_f411ce.h` alongside `rx`. Breaking rename for
  forks: `FEATURE_ESC`/`ESC_*` → `FEATURE_ESC0`/`ESC0_*` (and `ESC1_*`).

- **docs: a full MkDocs + Material documentation site**, published to GitHub Pages via
  `.github/workflows/docs.yml`. `readme.md` trimmed to a landing page linking out to it.

- **feat(app): the Firmware page can flash `esp32_wroom32` over `esptool`**, alongside STM32
  boards over DFU. New `EsptoolFlasher` backend and an explicit serial-port picker for ESP32.

- **feat(hardware): new `esp32_wroom32` firmware target**, using its onboard LED and WiFi radio
  directly. Flashed via `esptool` over its USB-UART bridge.

- **feat(hardware): WiFi module for the ESP-01.** New `wifi` module on USART2, exposing
  `wifi.ssid`/`password` plus status/rssi/ip telemetry and an SSID scan.

- **feat(app): Configuration page fields show real descriptive help text**, via a new
  `field_help.js` mapping each param key to authored copy.

- **fix(terminal): Save button now enables after a typed `set`.** The Terminal's free-text
  commands previously never touched the shared dirty flag.

- **RX mapping (phase 2): `servo` can be driven from a receiver channel.** New `core::Inputs`
  bus; `servo.mode` gains an `input` value and a `servo.src` param.

- **Board support: STM32F401CE (WeAct Black Pill V3.0).** New `[env:blackpill_f401ce]` and board
  header, same pinout as the F411 with 96KB RAM at 84MHz.

- **RX: `rate` no longer under-reports for a second after the link recovers.**
  `LinkState::onFrame()` starts a fresh counting window on the down→up edge.

- **RX: ExpressLRS support, protocol-agnostic receiver.** `hardware/crsf/` became `hardware/rx/`;
  `rx.protocol` selects `crossfire`/`elrs` at runtime, no reflash needed.

- **RX: Crossfire receiver module (phase 1, receive and display).** USART1 on PA10 at 420000
  8N1; channels plus link/lq/rssi/rate/err telemetry.

- **Servo module (single channel).** `FEATURE_SERVO`, hardware PWM on TIM4_CH1 (PB6); off/hold/
  sweep modes.

- **ESC module: single-channel hardware PWM with an arm-hold gate.** New `esc` module on
  TIM3_CH1/PA6; off/armed/input modes with a 2s arm-hold. Amendment: bidirectional ESC support
  via `esc.direction`.

- **Discard unsaved changes ("revert").** New `Op::Revert` reads flash back into RAM.

- **Firmware release process.** `app/tools/bundle_firmware.py` builds every board image in one
  run and prunes stale ones.

- **Electron port readiness (phase 0).** `Api` is the entire porting surface: relative paths,
  self-owned reconnection, no DOM `File` in its calls.

- **In-app DFU firmware upload.** A Firmware page flashes the bundled image over USB DFU, entered
  by the `dfu` op or BOOT0+NRST.

- **Boot health status.** `core/boot_log.h` buffers boot lines and replays them after every
  `hello`.

- **ST7789 240x240 display module.** `FEATURE_ST7789_240X240`, with an info page and a stats
  page.

- **Serial terminal page.** Echoes device messages and sends commands to the board.

- **Terminal settings backup/restore.** `dump` prints all settings as INI; `list` documents every
  setting; **Restore from INI…** reads one back.

- **Modular firmware, board configs, project versioning.** Board headers under
  `firmware/include/boards/` enable modules at compile time.
