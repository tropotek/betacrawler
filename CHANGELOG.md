# Changelog

Summaries of completed work. Detail, reasoning and hardware-verification records live in
`_notes/` (specs and plans) and in the git history.

## Unreleased

- **fix: the control loop no longer stalls 200ms at a time when nothing is listening on USB.**
  Telemetry is on by default and is not gated on a host being connected, so an untethered board
  still wrote a telemetry line every `1000/tlm.rate` ms. `USBSerial::write()` returns 0
  immediately when `CDC_connected()` is false, which covers both "no host has the port open" and
  a momentary in-flight packet overrun, and `writeLine()` treated every such zero as a stalled
  host worth waiting out — burning its full 200ms retry budget inside `loop()`. Nothing else ran
  meanwhile: no receiver drain, no mix, no ESC pulse write, and the 256-byte CRSF ring (about
  20ms of an ELRS 500Hz stream) overflowed on every stall. Since the telemetry deadline is
  checked against a timestamp taken at the top of `loop()`, the next pass was immediately overdue
  and stalled again, so the control path effectively ran at 5Hz and stick-to-ESC latency was
  quantised to roughly 200ms — the delay was worst on exactly the untethered vehicle that has
  nobody listening. `writeLine()` now tells the two cases apart by whether any byte was accepted:
  a partial line is still a stalled host worth the retry budget, but zero bytes on the first call
  means nothing is listening and returns at once. Measured on hardware across a 9-second
  disconnect, which now holds 27698Hz with a 2313us worst pass.

- **docs: the wiring diagram now shows the whole power chain.** It stopped at the two ESC signal
  leads and the receiver, leaving a builder to guess at everything carrying current: the battery
  went unshown, the ESCs had no power source, and the motors — the reason the ESCs are there —
  were absent entirely. The diagram now runs LiPo → PDB → ESCs → motors, with the PDB's 5V BEC
  feeding the board's 5V rail and the receiver through it, on a canvas grown to 1600x1240 with the
  power chain as its own band along the bottom so no lead crosses another. Two new peripheral
  colors: teal for the power chain, navy for the motor phases. The docs page links a 4x capture of
  the diagram, opened in a new tab, because the inline image is scaled to the content column and
  the pin labels are unreadable at that size. The parts list gains the PDB, and the pages that
  described the board and ESCs as running off two separate supplies now describe the PDB feeding
  both.

- **docs: the documentation site is now a Betacrawler build guide.** The previous set was the
  upstream template's docs put through a find-and-replace — the home page opened by explaining
  what a silkscreen is on a PCB with the project name substituted for the word, and the whole set
  framed the project as a board-agnostic configurator to fork rather than as the tracked vehicle
  this firmware builds. Twelve new pages in four sections, ordered the way a builder works:
  Build (parts, wiring, flashing), Drive it (connect, first setup, tuning), Reference
  (parameters, arming, status LED, terminal) and Troubleshooting. Parameter tables are generated
  from the firmware's golden schema and checked back against it, so every default and range on the
  site matches the device. `architecture.md` and `api.md` move to `dev-docs/`, off the published
  site: they are developer material, and the site is for people building the machine. The README
  is rewritten around the hero image, rendered to PNG because GitHub's markdown sanitiser will not
  reliably display the SVG. Screenshots are recaptured against the `sim://board` simulator, whose
  schema is a copy of the golden fixture — the capture tool previously carried its own
  hand-written schema which had drifted, still declaring a display parameter this board does not
  ship.

- **fix: four stale statements in the app and firmware.** The Wiring page described a "physical
  betacrawler layout" (another find-and-replace casualty — it is the silkscreen a schematic map is
  contrasted with) and claimed the two ESCs are independently commanded with no mixing, which
  `tank_drive` has since made false. The Terminal's placeholder offered `get led.mode`, naming a
  parameter no board in this tree ships, and the Home page pointed at a Telemetry page the nav does
  not have. In the firmware, `esc0_params.cpp` said "Defaults to unidirectional" directly above a
  line defaulting to `esc::DIR_BIDIRECTIONAL`.

- **feat: a simulated board can be selected instead of a real one.** `sim://board` heads the
  port dropdown as "Simulated board"; connecting to it runs an in-process device behind the same
  JSON-lines protocol the firmware speaks, so every page, the Terminal, INI backup/restore and the
  save/revert flow work with no hardware attached. Telemetry is reactive rather than canned —
  `sim_model.py` ports the firmware's own mixer, ESC arm state machine and RC sweep, so changing a
  ratio or a source moves the same readings it would move on a board. Its schema is a copy of the
  firmware's golden test fixture, guarded by a test that fails if the two drift. It reports
  `board: "simulator"` with no capabilities, so nothing offers to flash or DFU it.

- **feat: forward and steer ratios join reverse ratio in the drive mixer.** New
  `tank_drive.forward_ratio` and `tank_drive.steer_ratio`, both 0–100% defaulting to 100
  (unscaled), rendered beside Reverse Ratio in the Controller page's Drive Mixer card. Each scales
  one input's distance from centre and nothing else, so capping straight-line speed leaves a
  zero-throttle pivot untouched and capping turn authority leaves speed untouched. They cap at the
  mixer rather than via `esc<N>.min_us`/`max_us` on purpose: that range is the ESC's calibration,
  and narrowing it also moves `neutralUs()`, which is where "stop" lives on a bidirectional setup.

- **fix(firmware): tank drive defaults to throttle=ch2, steer=ch1.** A Mode 2 handset puts
  elevator on ch2 and aileron on ch1, so the previous ch1/ch2 pair put the driver's throttle stick
  on the steer input. The result on a default board is a pivot command rather than a drive command
  — one track full forward, the other full reverse — and `tank_drive.reverse_ratio` appears to do
  nothing, because its scaling is gated on the throttle input being below centre and that input
  never leaves centre. Defaults are not part of `Registry::fingerprint()`, so this changes only
  what a fresh board comes up with; a board with saved settings keeps its own mapping.

- **feat: the ESC PWM frame rate is selectable, 50/100/200/400Hz.** New `esc0.rate`/`esc1.rate`
  parameters, surfaced as one **PWM Rate** control in a new ESC card on the Configuration page
  that writes both (the `esc0.direction`/`esc1.direction` pattern — two motors on one vehicle
  running different frame rates has no use case, and `Registry::notify()` only ever reaches the
  owning module, so one shared parameter would leave the other ESC un-notified). At the shipped
  50Hz default a new command waits up to 20ms for the next timer update event regardless of how
  fast anything upstream runs; 400Hz cuts that to 2.5ms. 50 remains the default because it is what
  every analog ESC auto-detects — a board header's `ESC<N>_FRAME_US` still states the hardware
  default, and this parameter is the runtime override. Changing the rate forces a fresh arm-hold,
  because a BLHeli_S-class ESC frame-detects as it arms. `esc<N>.max_us` is settable to the whole
  400Hz frame, so `effectiveMaxUs()` reserves a low period inside each frame at the point of use —
  `core::Params` validates one value at a time and has no cross-parameter seam. Adding parameters
  changes `Registry::fingerprint()`, so saved settings fall back to defaults once on first boot.

- **feat(firmware): loop rate and worst-pass time are system telemetry.** New `loop` (Hz) and
  `loopworst` (µs) fields, shown in the Configuration page's System card. Measured on hardware at
  ~27.6kHz with a ~2.8ms worst pass, which is what establishes that the main loop was never the
  latency constraint. `loopworst` is the diagnostic half: the loop is unbounded, so a slow module
  stalls the whole control chain, and an average rate hides that entirely. New `core::LoopStats`
  is a singleton for the same reason `core::Health` is one, and takes its timestamp as an argument
  rather than reading the clock, so it is pure and native-tested.

- **feat(firmware): the onboard LED is a firmware health indicator, not a configurable module.**
  It blinks at an even 1Hz while the firmware is healthy and pulses at ~5Hz on a blocking fault; a hard fault blinks faster still, from a handler that previously left
  the board frozen with its pin latched and nothing said. The blink exists because a stopped board
  holds its pins: a steady-on LED cannot distinguish healthy from dead, so the healthy signal has
  to be one a stopped loop cannot counterfeit. New `core::Health` records one fault code
  (first-fault-wins) and writes a `boot: fault=<name>` line that replays on every `hello`; new
  `core::patternState()` plays the on/off sequences, so a distinct pattern per fault code is a
  table entry rather than new logic. **Breaking for any fork's board header:** `FEATURE_LED` is
  now `FEATURE_STATUS_LED` (`LED_PIN` and `LED_ACTIVE_LOW` are unchanged), and the `led.mode` /
  `led.blink_hz` parameters are gone — which changes `Registry::fingerprint()`, so saved settings
  fall back to defaults once on the first boot of this firmware. `core/led_curve.cpp` survives as
  `core/triangle.cpp`: its symmetric triangle wave is used by servo sweep mode and the
  `rx.source=sim` channel generator, neither of which is an LED, so `breathingDuty()` is now
  `trianglePercent()`.

- **fix(firmware): a module that does not fit the registry raises a fault instead of vanishing.**
  `registerModules()` discarded `Registry::add()`'s return value, so exceeding `FW_MAX_MODULES`,
  `FW_MAX_PARAMS` or `FW_MAX_TLM` silently dropped a module from the schema. `add()` now raises
  `Fault::Registry` itself, so no call site can forget to check, and a native test asserts the
  shipping module set registers cleanly.

- **feat(web): the Configuration page names the firmware's fault.** The system telemetry frame
  carries a new `fault` field and the System card renders it by name, in red when non-zero. The
  row is guarded on the key being present, so a board whose firmware does not publish it renders
  as before.

## Version 1.0.0 (2026-07-29)

- **feat(firmware): split the single-instance `esc` module into shared math plus two independent
  `esc0`/`esc1` modules, and enable `rx`, `esc0` and `esc1` by default on `blackpill_f411ce.h`.**
  The pulse-width clamping, arm-state transitions and input-staleness logic that used to live
  inside `esc_driver.cpp` moved to a pure, native-tested `esc::` library
  (`firmware/src/hardware/esc/esc_math.h`/`.cpp`, no Arduino includes); the old `hardware/esc/`
  module is gone, replaced by two near-identical instances, `hardware/esc0/` and `hardware/esc1/`,
  each with its own `FEATURE_ESC0`/`FEATURE_ESC1` flag, its own `esc0.*`/`esc1.*` wire keys, and its
  own arm state, so enabling or disabling one never affects the other. Two ESC instances must claim
  different physical timer peripherals — `blackpill_f411ce.h` carries a compile-time `#error` guard
  that fires if a fork ever turns `FEATURE_SERVO` on there too, since `esc1` and Servo both claim
  `TIM4`/`PB6` on that board. **Breaking rename for any fork's board header:** `FEATURE_ESC`,
  `ESC_PIN`, `ESC_TIMER`, `ESC_FRAME_US`, `ESC_ARM_HOLD_MS`, `ESC_INPUT_STALE_MS` and
  `ESC_ARM_LOW_MARGIN_US` no longer exist — rename to `FEATURE_ESC0`/`ESC0_PIN`/`ESC0_TIMER`/etc.
  (or the `ESC1_*` equivalents for a second instance). `blackpill_f411ce.h` — the tank build — now
  ships `rx`, `esc0` and `esc1` enabled by default, alongside the pre-existing `button` and `led`.

- **docs: a full MkDocs + Material documentation site, published to GitHub Pages.** New `docs/`
  site (`mkdocs.yml`, `docs/index.md`, `getting-started.md`, `architecture.md`, `api.md`,
  `troubleshooting.md`, per-module pages under `docs/modules/`, and guides under `docs/guides/`)
  built with `docs/.venv` and deployed on push to `main` by a new
  `.github/workflows/docs.yml` Pages-deploy workflow, `mkdocs build --strict` gating the deploy so
  a broken link or nav entry fails CI instead of publishing. `readme.md` is trimmed to a landing
  page that links out to the site rather than duplicating it. A new dev tool,
  `docs-tools/capture_screenshots.py`, boots the real app against a fake, fully-populated device
  (`app/tests/fake_serial.py`) and drives it with Playwright to regenerate every page's screenshot
  under `docs/assets/screenshots/` on demand — including a fixed, fake STM32-labeled port list so
  the captures never leak whatever serial hardware happens to be attached to the machine that ran
  the tool. Also adds `firmware/docs/` (`BOM.md`, `ASSEMBLY.md`) and `app/docs/` (`USER_GUIDE.md`)
  — placeholder skeletons for a fork's own bill of materials, assembly instructions and user
  guide, called out in `readme.md`'s "Making it yours" section as part of the same fork on-ramp.

- **feat(app): the Firmware page can flash `esp32_wroom32` over `esptool`, not just STM32 boards
  over DFU.** The manifest's `method` field (`"dfu"`/`"esptool"`) now has a real dispatch behind
  it: a new `EsptoolFlasher` backend class alongside the existing `DfuFlasher`, a `bundle_firmware.py`
  step that merges the ESP32 build's four PlatformIO outputs into one flashable image via
  `esptool merge-bin`, and an explicit serial-port picker on both the bundled-image and Advanced
  local-file flows — an ESP32 in its ROM bootloader has no distinct USB identity the way a Black
  Pill in DFU mode does, so the port has to be chosen by hand rather than detected. A new `--all`
  bundler flag builds every board target in one run (opt-in; the bare command's single-board
  default is unchanged). Full design in
  `_notes/_archive/superpowers/specs/2026-08-02-esp32-esptool-flashing-design.md`, implementation
  record in `_notes/_archive/superpowers/plans/2026-08-02-esp32-esptool-flashing.md`.
  **Hardware-verified 2026-08-02**
  — see `_notes/todo.md`'s "Second board target" entry for the real-board flash record and the one
  real bug (`esptool` never resolving under the documented `.venv/bin/uvicorn` run command) that
  verification caught and fixed.

- **feat(hardware): new `esp32_wroom32` firmware target.** A generic ESP32 WROOM-32 dev board,
  using its onboard LED and onboard WiFi radio directly (`WifiEsp32Driver`, driven through
  `WiFi.h`, not the AT-command driver the STM32 boards use to talk to an external ESP-01) — no
  external modules wired up. Flashed via `esptool` over the board's own USB-UART bridge, never an
  ST-Link. Required an `FW_MCU_ESP32` guard/`_esp32` counterpart for every file with an
  STM32-specific body (`storage.cpp`, `hardware/system/system_driver.cpp`,
  `hardware/wifi/wifi_driver.cpp`), each compiling to nothing on the other architecture rather
  than being excluded by a per-environment `build_src_filter`. Side effect: adding those guards
  fixed a latent `blackpill_f401ce` build break (`wifi_driver.cpp`'s body previously compiled
  unconditionally and demanded `WIFI_RX_PIN`/`WIFI_TX_PIN`/`WIFI_BAUD`, which that board's header
  never defined). See `_notes/_archive/superpowers/specs/2026-08-01-esp32-wroom-target-design.md` /
  `_notes/_archive/superpowers/plans/2026-08-01-esp32-wroom-target.md` for the full design.

- **feat(hardware): WiFi module for the ESP-01.** New `wifi` module on `blackpill_f411ce`'s
  USART2 (`PA2`/`PA3`), independent of `rx`/`esc`'s pins. Exposes `wifi.ssid`/`wifi.password`
  (password masked via a new generic `ParamDef.secret` schema hint, with a reveal-eye toggle in
  the UI) plus `wifi.status`/`wifi.rssi`/`wifi.ip` telemetry (IP via a new `"ip"` display
  renderer). SSID scan is a bespoke feature — new `Op::WifiScan` + `core::WifiScanner` seam
  mirroring `Bootloader`/`dfu`, plus a new generic `Module::pollPush`/`Registry::pollPush`
  unsolicited-push mechanism, results pushed as a `scan` WS frame. **Verified on hardware
  2026-07-31**: `AT+CWLAP` scan returned real, currently-visible networks with plausible RSSI, and
  joining a real network through the app UI produced a real IP end to end (app UI → backend →
  firmware → ESP-01 and back), with no reset/brownout during the scan. See `_notes/plan-wifi.md` /
  `_notes/spec-wifi.md` for the full design.

- **feat(app): Configuration page fields show real descriptive help text.** New
  `app/web/field_help.js` maps each param's dotted schema key to authored copy, replacing the
  old auto-generated bound-only hint (`1–20`, `max 16 chars`). An unmapped key falls back to that
  bound automatically, so adding a firmware parameter still needs zero `app.js` changes. Config
  page only — Telemetry keeps its existing unit-only caption. Guarded against a missing/broken
  `field_help.js` breaking the whole Configuration form (`FIELD_HELP_MAP` in `app.js` falls back
  to `{}`), since this repo is a fork template and a fork could copy `app.js` without its sibling
  data file.

- **fix(terminal): Save button now enables after a typed `set`.** `terminal.run()`'s result gained
  a `dirty` field (`None` for read-only commands, `True`/`False` for `set`/`save`/`defaults`/
  `revert`) carried through `/api/terminal` and read by `termRun()` in `app.js`. Previously the
  Terminal page's free-text commands never touched the shared dirty flag at all, so a `set` typed
  there looked applied but left the Save button disabled — the change was RAM-only and silently
  lost on the next reboot, since nothing prompted the user to actually write it to flash.

- **RX mapping (phase 2): `servo` can now be driven from a receiver channel.** New
  `core::Inputs`, a small fixed bus of µs values that `rx` publishes decoded channels onto after
  every accepted frame, and that other modules read without either module naming the other.
  `servo.mode` gains a fourth value, `input`, plus a new `servo.src` param (`ch1`..`ch12`, matching
  the RX module's own `ch1`..`ch16` telemetry naming) selecting which channel to track, **defaulting
  to `ch2`** (roll, confirmed on bench hardware — see commit `f32420a`) — paired deliberately with
  the ESC module's own `esc.src` default, `ch3` (pitch, see commit `39d8958`) at the time, both
  self-centering channels, so `servo` and `esc` did not both default to tracking the same stick out
  of the box. `esc.src`'s default later changed to `ch1` (throttle) once the ESC module settled on
  `unidirectional` as its own default — see the Amendment further down. The
  driver clamps the read value through the existing `min_us`/`max_us` before
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
  to two "defected" spare boards that betacrawler as F411CE but read back a `DEV_ID` of `0x433` and
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
  back to defaults on its next boot.

  **Verified on hardware 2026-07-30**, on a pair of E-flite 18A brushless ESCs: `off`/`armed`
  modes, the arm-hold gate (holds at the safe pulse for the full window, only then honours the
  commanded value), and proportional throttle response all confirmed working end to end. Ground
  the ESC's signal return to the board — a missing common ground presents as the ESC beeping
  "no signal" indefinitely and never responding to any commanded value, which is easy to mistake
  for a firmware fault.

  **Amendment: bidirectional ESC support.** New `esc.direction` param (`unidirectional`/
  `bidirectional`, defaulting to `unidirectional` so no already-shipped board's behaviour changes
  unless explicitly switched over). For a bidirectional ESC — center is stop, above is forward,
  below is reverse — "safe" is no longer `min_us`: a new `neutralUs()` helper resolves it to either
  `min_us` (unidirectional) or the midpoint of `min_us`/`max_us` (bidirectional), and the arm-hold
  precondition, the link-loss/`esc.src`-change failsafe and the low-throttle check documented above
  all reference that resolved center instead of the bare low end. `esc.direction` is declared
  *first* in the param table, ahead of `esc.mode` — `apply()` re-reads every param on each change,
  so an INI restore (which applies params in schema order) could otherwise apply a saved `esc.mode`
  before the saved `esc.direction`, arming against the wrong reference point. This adds a param, so
  **`Registry::fingerprint()` changes** again. **The bidirectional path itself is not yet verified
  on hardware** — the ESCs on the bench turned out to be unidirectional, which is what the hardware
  verification above actually confirms; a real bidirectional ESC is still needed to check that path.

  `esc.src` defaults to `ch1` (the conventional Mode-2 throttle channel, matching
  `esc.direction`'s own unidirectional default) and is now shown only while `esc.mode == off` — the
  opposite of every other `showIf` in this codebase, deliberately: it's a pre-arm configuration
  choice made once while safely off, not a live control, so hiding it once armed avoids an
  accidental change sitting in view (already guarded by `srcChangeDemotesArmed`, but still
  disruptive). A bidirectional ESC needs a self-centering channel instead (this bench uses `ch3`,
  pitch, confirmed on real receiver hardware, paired with `servo.src`'s own `ch2`/roll default —
  both self-centering, which puts throttle and steering on one stick for single-stick car/crawler
  control) and is expected to be set explicitly per deployment, not assumed by the default.

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
