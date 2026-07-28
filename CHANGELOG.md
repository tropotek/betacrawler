# Changelog

Summaries of completed work. Detail, reasoning and hardware-verification records live in
`_notes/` (specs and plans) and in the git history.

## Version 1.0.0 (2026-07-28)

- **RX: `rate` no longer under-reports for a second after the link recovers.**
  `LinkState::onFrame()` starts a fresh counting window on the down→up edge. Previously the
  window the dropout froze was left open, so the first tick back found it overdue and closed it
  on the spot, publishing a count spanning only the instant since frames resumed — measured on
  real Crossfire hardware as `rate 1` beside `rfrate 150` for about a second after a 10s
  outage — and consuming the recovery frame in the process. `rate` now stays at the 0 the drop
  set until a whole window has genuinely been measured, then reports the true figure. Both
  symptoms are covered by native tests.

- **RX: ExpressLRS support, protocol-agnostic receiver.** `hardware/crsf/` became
  `hardware/rx/`, with the wire format split into `proto_crsf.*` and selected at runtime by
  `rx.protocol` (`crossfire`/`elrs`) — no reflash to swap receivers. 16 channels, per-protocol
  timeouts, and `rfrate`/`pwr` decoded from the CRSF link-statistics frame. Added a generic
  `showIf` schema hint so mutually-exclusive settings groups hide themselves without `app.js`
  learning any protocol's name. UART receive is proven on Crossfire against a real TBS module
  (2026-07-28) and remains unproven on ELRS.

- **RX: Crossfire receiver module (phase 1, receive and display).** USART1 on PA10 at 420000
  8N1, behind `FEATURE_CRSF`. Channels plus `link`/`lq`/`rssi`/`rate`/`err` telemetry; CRC-8,
  framing, resync, 11-bit unpacking and the link timeout all in the natively tested pure half.

- **Servo module (single channel).** `FEATURE_SERVO`, hardware PWM on TIM4_CH1 (PB6). Modes
  off/hold/sweep with calibration endpoints and a UI slider; boots in `off` so nothing moves
  until asked. TIM4 CH2-4 left free for a multi-channel fork.

- **Discard unsaved changes ("revert").** New `Op::Revert` reads flash back into RAM, so the
  three parameter states (factory / saved / RAM) each have their own button. Falls back to
  defaults when nothing valid is stored, reporting which happened.

- **Firmware release process.** `app/firmware/` is build output, not committed binaries.
  `app/tools/bundle_firmware.py` builds a list of boards in one all-or-nothing run and prunes
  stale images, so one command is one release.

- **Electron port readiness (phase 0).** Made `Api` literally the entire porting surface:
  relative paths via `Api.base`, `Api.subscribe()` owning its own reconnection, and
  `flashUpload(bytes, filename)` taking no DOM `File`. No behaviour change.

- **In-app DFU firmware upload.** A Firmware page flashes the bundled image over USB DFU,
  entered either by the firmware's `dfu` op or by BOOT0+NRST. Works while disconnected, so a
  broken board stays recoverable; an Advanced path flashes a local `.bin` behind a
  vector-table check.

- **Boot health status.** `core/boot_log.h` buffers boot lines (identity, whether saved
  settings survived, module/param/telemetry counts, free RAM) and replays them after every
  `hello`, so a late-connecting host still sees them. Rendered as `[device] …` in the Terminal.

- **ST7789 240x240 display module.** `FEATURE_ST7789_240X240`, with an info page and a stats
  page and `disp.page = cycle` alternating them. Named for the controller and resolution so a
  different panel becomes its own module; keys stay `disp.*` so backups survive a swap.

- **Serial terminal page.** Echoes device messages and sends commands to the board.

- **Terminal settings backup/restore.** `dump` prints all settings as INI, `list` documents
  every setting's type/range/default, and **Restore from INI…** reads a file back — unknown
  keys and rejected values are skipped and reported rather than aborting. Applies to RAM only.

- **Modular firmware, board configs, project versioning.** Board headers under
  `firmware/include/boards/` enable modules at compile time; each module owns its params,
  telemetry and driver. Firmware and app are versioned independently.
