# Changelog

Summaries of completed work. Detail, reasoning and hardware-verification records live in
`_notes/` (specs and plans) and in the git history.

## Version 1.0.0 (2026-07-29)

- **RX mapping (phase 2): `servo` can now be driven from a receiver channel.** New
  `core::Inputs`, a small fixed bus of µs values that `rx` publishes decoded channels onto after
  every accepted frame, and that other modules read without either module naming the other.
  `servo.mode` gains a fourth value, `input`, plus a new `servo.src` param (`ch1`..`ch12`, matching
  the RX module's own `ch1`..`ch16` telemetry naming) selecting which channel to track, **defaulting
  to `ch4`** (changed 2026-07-30 from an initial `ch1` default) — `ch1` is reserved as the throttle
  channel (the ESC module, `_notes/spec-esc.md`, defaults `esc.src` to it), so `servo` defaults
  elsewhere to avoid both modules tracking the throttle stick out of the box with no config
  changes; the driver clamps the read value through the existing `min_us`/`max_us` before
  commanding a pulse, same as `hold`/`sweep` already do. A never-written or invalidated slot reads
  as `0`, which is not a reachable channel value, so `servo` holds its last commanded pulse rather
  than actuating —
  matching how a link dropout already holds telemetry in phase 1, and keeping "hold last value"
  the only failsafe behaviour this phase introduces (a configurable one is still phase 3). This
  adds a param and an enum value, so **`Registry::fingerprint()` changes**: any board with settings
  saved before this change falls back to defaults on its next boot, by design — the guarded flash
  record's fingerprint mismatch is exactly the safety net for a layout change like this one.
  **Verified on hardware 2026-07-30**: a real stick tracked across its full range, failsafe hold
  and clean recovery on a link drop, and live `hold`/`input`/`sweep` mode transitions with no
  glitches — see the Test Notes in `_notes/todo.md`.

- **Board support: STM32F401CE (WeAct Black Pill V3.0).** New `[env:blackpill_f401ce]` and
  `boards/blackpill_f401ce.h` — same pinout as the F411 board (WeAct kept the layout identical
  across revisions), just the F401's 96KB RAM and 84MHz clock instead of 128KB/100MHz. Traced back
  to two "defected" spare boards that silkscreen as F411CE but read back a `DEV_ID` of `0x433` and
  only 96KB of readable SRAM over SWD: a real STM32F401CEU6 under an F411 label, confirmed against
  the physical chip marking. The F411 env's linker script puts the initial stack pointer past a
  real 96KB part's actual RAM, HardFaulting before USB ever comes up — every symptom a "dead
  bootloader" would produce, with no bootloader involved. Both boards now run the new env cleanly
  (verified: boots without a HardFault, enumerates as a USB VCP, telemetry and LED confirmed live)
  and `app/firmware/manifest.json` carries both board images side by side with no backend or
  `app.js` changes — the schema-driven design meant a second board was purely additive.

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

- **ESC module: single-channel hardware PWM with an arm-hold gate.** New `esc` module
  (`TIM3_CH1`/`PA6`), the second timed-output actuator after `servo`. `esc.mode` is
  `off`/`armed`/`input`; `esc.throttle_us` is a direct 1000–2000µs command (no angle-style mapping
  needed, since the param and the wire are already the same unit) clamped through
  `esc.min_us`/`esc.max_us`. Drives any BLHeli/BLHeli_S/BLHeli32 ESC via its standard PWM input
  (not DShot/Oneshot/Multishot), on its own timer (`TIM3`), independent of `servo`'s `TIM4`, so the
  two modules' `HardwareTimer` instances never contend for one peripheral's shared overflow
  register. Entering `armed` or `input` from `off` always starts at `min_us` and holds it for
  `ESC_ARM_HOLD_MS` (2s default) — but promotion to fully armed requires BOTH that hold to elapse
  AND the commanded value to be confirmed low (`esc.throttle_us` in `armed` mode, the selected
  `esc.src` channel in `input` mode) for the whole window, restarting the hold if it isn't; a saved-
  to-flash high throttle or a mis-mapped input channel now simply never arms, rather than arming
  late. `core::Inputs` (the RX-to-actuator channel bus `servo.mode=input` already reads) gained a
  bus-wide `markFresh()`/`lastFreshMs()` signal that `rx` stamps once per decoded frame; `esc.mode=
  input` reads it to detect a dead link and force `min_us`, closing a runaway-motor gap `servo`
  never had (a servo correctly holds position through a dropout — a motor holding its last
  throttle forever is a hazard). An already-armed `input`-mode session demotes back to `ARMING`,
  forcing a fresh full re-arm hold, if the link goes stale **or** `esc.src` is changed while armed
  — both close the same underlying gap: re-pointing the output at a different or newly-live signal
  source must never be honoured without first re-proving it low. This adds a module, so
  **`Registry::fingerprint()` changes**: any board with settings saved before this change falls
  back to defaults on its next boot. **Not yet verified on hardware** — no ESC has been on the
  bench; see `_notes/todo.md`.

  **Amendment: bidirectional ESC support.** New `esc.direction` param (`unidirectional`/
  `bidirectional`, defaulting to `unidirectional` so no already-shipped board's behaviour changes
  unless explicitly switched over). For a bidirectional ESC — center is stop, above is forward,
  below is reverse — "safe" is no longer `min_us`: a new `neutralUs()` helper resolves it to either
  `min_us` (unidirectional) or the midpoint of `min_us`/`max_us` (bidirectional), and the arm-hold
  precondition, the link-loss/`esc.src`-change failsafe and the low-throttle check documented above
  all reference that resolved center instead of the bare low end. Also corrects `esc.src`'s default
  from an earlier unconfirmed `ch1` guess to `ch3` (pitch), paired with `servo.src`'s own `ch2`
  (roll) default — both confirmed on real receiver hardware, both self-centering, which is what
  makes a bidirectional ESC's throttle safe to release and puts throttle and steering on one stick
  for single-stick car/crawler control. This adds a param, so **`Registry::fingerprint()` changes**
  again.

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
