# Changelog

Summaries of completed work, one to two lines each. Detail, reasoning and hardware-verification
records live in the git history, not here.

Merged work lands under `## Unreleased`. Cutting a release renames that heading to the version
number being tagged, and a fresh `## Unreleased` opens above it.

## Unreleased

- **feat: the `/app-dev/` build says so.** A red strip above the navbar marks it a development
  build and links to the stable configurator; its version badge reads `-dev`.
- **Saved settings reset to defaults on the first boot after flashing** — the servo parameter set
  shrank, changing the stored-record fingerprint.
- **feat: the steering servo follows `drive.mode`.** The firmware derives it, so an INI restore or
  a Terminal `set` configures a car correctly; `servo.mode`, `servo.angle`, `servo.sweep_s` and
  `servo.src` are gone.
- **fix: the steering holds its last position when the link drops**, instead of centring, while
  the motors still fail to neutral.
- **feat: the steering output is reported as telemetry.** `drv_s` joins `drv_l`/`drv_r`, and the
  Controller page shows it in place of the duplicated throttle reading on a car.
- **docs: `rx.source = sim` sweeps a servo with no receiver bound**, through the whole mixer chain.

## 4.2.2

- **feat: new BetaCrawler mark, and the name is capitalised.** A badge split between tracked
  running gear and a road wheel replaces the tank hero, and the same mark becomes the favicon and
  PWA icon. `betacrawler` stays the identifier — repo, URL, PlatformIO envs, and the `fw` string.
- **docs: an About page**, with the logos to download as SVG and PNG, the palette, and the
  licence terms for using them.
- **docs: the Build section is four self-contained build paths.** Skid steer or car, brushed or
  brushless, each with its own parts, wiring, settings and starter INI.
- **docs: optional hardware moved to its own Add-ons section**, with wiring that adds onto a base
  build rather than replacing its diagram. Battery sense is the first.
- **docs: a wiring diagram for a brushed car**, the one base build that had none.
- **fix: tagging publishes the site again.** Pages deploys run from `main` and resolve the newest
  tag themselves, because a deployment created from a tag ref never goes live.

## 4.2.1

- **fix: a tagged release can actually publish.** Release assets are qualified by board before
  upload, so the second image no longer collides with the first. Stored settings survive this
  update.

## 4.2.0

- **Saved settings reset to defaults on the first boot after flashing** — `drive_steer` joins the
  source lists, changing the stored-record fingerprint.

- **docs: releases are cut as tags, and the published site is built from the newest one.** `main`
  becomes staging and is served unlisted at `/app-dev/`; `docs/development/releasing.md` is the
  policy.

- **fix: the Save button disables itself while the flash write runs.** It reads "Saving…" for the
  second the erase stalls the board, so the pause no longer looks like a click that did nothing.

- **fix: Discard changes is disabled until there is something to discard.** It follows the same
  dirty flag as the "applied — not saved to flash" note.

- **fix: the Firmware page says what flashing needs.** A line under the flash button explains that
  the board has to be connected or already in DFU, and links to the by-hand instructions below.

- **feat: car mode drives both motor slots and gets its own steering output.** Throttle reaches
  either motor pin, so whichever wheel is wired is driven, and steer moves to its own drive-bus
  slot that `servo.src` now selects by default. A car needs no Terminal: pick the drive mode, the
  motor type, and switch the servo on.

- **feat: Drive Mode switches the steering servo on and off.** `car` enables it and shows its
  settings, `skid` disables them and hides the card — a skid build has no steering servo, so it is
  never asked about. `servo.mode` stays a Terminal override for bench testing.

## 4.1.0

- **feat: `motor<N>.type` gains `none`, and it is now the default.** An output with no type
  chosen detaches its pin, so choosing the type is the one step that brings a motor to life —
  `motor<N>.mode` goes back to defaulting to `input`. Adding the option changes the settings
  fingerprint, so stored settings are discarded once on this update.

## 4.0.0

- **feat: `esc0`/`esc1` are now `motor0`/`motor1`.** They drive a motor through whichever output
  stage is configured, so naming them after one of the two was wrong. Every parameter key,
  telemetry key and board-header symbol renames with them.

- **feat: `motor<N>.direction` is gone — neutral is always 1500 µs.** The ESC must treat
  centre-stick as stop: a surface ESC already does, and a BLHeli_S drone ESC does once set to
  bidirectional.

- **feat: `tank_drive` is now `drive`, with `drive.mode` = `skid` or `car`.** `skid` is the
  existing differential mix; `car` sends throttle to one motor and steer to a servo.

- **feat: the servo gains `invert` and `trim_us`, and can follow the drive bus**, so a
  single-motor car with steering is a supported build. It moves to TIM2/PB10 and ships enabled
  on both boards.

- **Saved settings reset to defaults on the first boot after flashing** — the stored-record
  fingerprint changes with the renamed parameters.

- **feat: `esc0`/`esc1` can drive a brushed-motor H-bridge (DRV8833-class) as well as a brushless
  ESC**, switchable per motor from the app with no reflash. New `type`/`freq`/`invert`/`brake`
  params; wiring diagram and setup docs for brushed builds.

- **feat: CRSF moved to PA2/PA3, freeing PA9/PA10 as a spare UART for forks.** `esc1` moved to PB8
  and WiFi to PB6/PB7 to make room; bench-verified against the STM32 ROM bootloader's DFU race
  (`_notes/docs/research/rx-uart-bootloader-race.md`).

- **chore: the deprecated `app/` desktop configurator and FastAPI backend removed.**
  `bundle_firmware.py` and the hero-image tool move to `tools/`, which absorbs `docs-tools/`;
  the stale screenshot-capture script is dropped.

- **feat: a simulated board can be tried from the Home page, no hardware required.**
  `sim://board` runs entirely in the browser tab, behind the same `Api` seam a real board uses.

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

## 1.0.0 (2026-07-29)

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
