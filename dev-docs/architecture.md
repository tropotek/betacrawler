# Architecture notes

The reasoning behind the rules listed in `CLAUDE.md`. That file is loaded into every session;
this one is read when a change actually touches the area. Where the two disagree, this file has
the detail and `CLAUDE.md` has the rule.

## The `Api` seam

`app/web/` talks to the backend exclusively through the `Api` object in `app.js`. That object is
the entire porting surface for a hypothetical future Electron rewrite, and the rules are cheap to
keep and expensive to retrofit:

- No `fetch`/`WebSocket` anywhere outside it.
- No `Api` method may take or return a browser-only type (a `File`, a `Blob`, the `WebSocket`
  itself) — bytes, strings, plain objects and callbacks only.
- Every path goes through `Api.base`, so nothing assumes an origin.
- The push channel stays `Api.subscribe(handler) -> unsubscribe`, owning its own reconnection,
  rather than handing a socket back to a caller that would touch `.onclose`.

`flashUpload` really did take a DOM `File` once, because `fetch` accepts one — which is exactly
how this kind of coupling gets in. Full contract, plus a grep check for the mechanical part:
`_notes/_archive/review-electron-port-readiness.md`.

## Modules

A board header's `FEATURE_*` flags decide what compiles in; `src/modules.cpp` registers those
modules into a `core::Registry` at boot, which flattens their parameters into one table and their
telemetry fields into one frame. `core/` never names a feature.

Each module is split in two:

- `<name>_params.cpp` — its `ModuleDesc` (id, label, `ParamDef[]`, `TlmDef[]`). **Zero Arduino
  includes**, because the native build compiles it.
- `<name>_driver.cpp` — the `core::Module` subclass that touches hardware. Board builds only.

That split is load-bearing, not stylistic: it is why `pio test -e native` can assemble the *real*
device's schema and keep `test/golden/schema.json` honest with no board attached. Full recipes for
adding a module or a board: `_notes/_archive/spec-firmware-modules.md`.

## The hardware/persistence seam

`core/` never touches a GPIO pin or a flash write directly. It talks through `core::Module`
(`onParamChanged`/`tick`/`readTelemetry`, in `core/module.h`) and `Persistence::save()/load()`
(`core/dispatch.h`), which `main.cpp` wires to real Arduino/EEPROM code.

Native tests inject fake modules, so "setting a parameter produced exactly one hardware call, on
the owning module, with that module's own local index" is provable with no board attached. This is
the one invariant most worth preserving if you touch `dispatch.cpp` or `registry.cpp`. Modules
always receive a **module-local** index, never a global one — `Module::globalParam(local)` maps
back when a driver needs to read a value.

## Observing modules

`Module::attach(const Registry&, const Params&)` is called on every module before any module's
`begin()`, for modules that must *read* the rest of the device. Access is const on purpose: an
observer may look at the device, but must never reconfigure
it behind `dispatch`'s back, which would skip validation and the change notification everything
else depends on. Resolve keys there once (`findParam`/`findTlm`), never per tick — the registry is
fixed after boot.

## The `core::Inputs` bus — the one asymmetry in "observers are const"

`core::Inputs` (`core/inputs.h`) is a small shared control-signal bus: a fixed array of µs
values that lets `rx` publish decoded channels for other modules to read, without `rx` or its
readers naming each other. `servo`'s `mode=input` is the first consumer; more are expected.

It exists because "observers are const" has no path for one module driving another at all —
correctly, since `attach()`'s `const Registry&`/`const Params&` exists precisely so a module can
never reconfigure another behind `dispatch`'s back. Rather than relax that, `rx` gets a
single, narrow exception: `modules.cpp` (the wiring file that already hands concrete hardware
handles to driver constructors) constructs the one `core::Inputs` instance and passes `RxDriver`
a mutable `core::Inputs&` directly through its constructor, never through `attach()`. Every other
module, `servo` included, only ever sees `Registry::inputs()`'s `const Inputs&`, reached the
normal way through `attach()`.

This is a one-producer/many-const-observers asymmetry, not a general loosening of the rule — no
module other than `rx` may ever receive a mutable handle to anything through `attach()`, and `rx`
itself gets one only because `modules.cpp` chose to wire it that way, not because `attach()`
grants it. It doesn't violate the spirit of "observers are const" because that rule protects
*param* state — validated, dispatch-owned, and meaningless to mutate outside a `set`. `core::Inputs`
is not param state; it's a purpose-built, one-way signal bus with exactly one writer decided by
construction, not by convention a second module could quietly bend.

`tank_drive` (added later) uses the identical pattern for a second, independent bus
(`Registry::driveOutputs()`) rather than a second writer sharing `rx`'s own -- each bus still has
exactly one constructor-wired producer, the pattern is just applied twice. `esc0`/`esc1` read
whichever bus their own `.src` selection points at.

Three shapes for carrying
`rx`'s channels to `servo` were weighed before settling on this bus, and the full reasoning —
including why the other two were rejected — lives in `_notes/spec-rx-mapping.md`. That file is
gitignored and local-only, so it won't follow a fresh clone, but it's there for anyone working in
this checkout who wants the history behind the choice.

The bus also carries one piece of state beyond the channel values themselves: `markFresh()`/
`lastFreshMs()`, a bus-wide "a frame was just decoded" timestamp `rx` stamps once per accepted
frame (real or simulated). It exists for the same reason the channel values do — a consumer that
needs to know whether the link is actually alive cannot infer that from a channel value holding
steady, since a real stick at its mechanical endpoint is indistinguishable from a dead link by
value alone. `esc0`/`esc1`'s `mode=input` failsafe is the first consumer of this signal; `servo` does not
need it (position-hold-on-dropout is its own correct, deliberate design, not a gap).

## Control latency, and where it actually lives

The stick-to-motor chain is `rx` decode → `tank_drive` mix → `esc0`/`esc1` write. All three run in
one `Registry::tick()` pass, in that registration order, so a decoded frame reaches the compare
register in the **same** loop iteration — there is no per-module pipeline delay to tune, and no
smoothing or ramping anywhere in `esc_math` or `tank_drive_math` to unwind.

The main loop is not the constraint either. It is free-running, unthrottled, and the `loop`
telemetry field reports it in the tens of kHz on an F411 — already well past the 8kHz a flight
controller treats as a maximum. Splitting it into priority tiers would buy nothing today.

The costs that remain are the two frame rates at either end:

- **The RF link**, set on the handset, not here. The `rfrate` field reports it. At a 50Hz packet
  rate that is 20ms before the board has even been told anything changed.
- **The PWM output frame**, `esc<N>.rate`. `writeUs()` writes a *shadow* compare register; it only
  becomes a pulse at the next timer update event. At 50Hz that is 0–20ms of wait and an effective
  command rate of 50Hz no matter how fast everything upstream runs. This is the term worth
  changing, and the parameter exists to change it.

Two consequences worth knowing before touching this area. `setOverflow()` may re-derive the
prescaler, which changes what a microsecond means to `setCaptureCompare()` — so every frame-rate
change must be followed by a re-write of the pulse, which is why `apply()` and `tick()` both end on
`writeUs()`. And a BLHeli_S-class ESC frame-detects as it arms, so a rate change under an armed ESC
forces a fresh arm-hold (`rateChangeDemotesArmed`), the same way a src change already does.

`esc<N>.rate` and `esc<N>.max_us` interact, and that interaction is resolved in the driver by
`effectiveMaxUs()`, not in the schema. `core::Params::setNum` validates one value against its own
declared bounds and has no cross-parameter seam — the same limitation `esc<N>.min_us`/`max_us`
already work around by declaring bounds that cannot cross. `effectiveMaxUs` reserves `kMinLowUs` of
low time inside each frame so the ESC always sees a pulse train rather than a line held high; it is
deliberately **not** applied to `neutralUs()`, because clamping neutral would silently move where
"stop" is whenever the rate changed.

`loopworst` is the companion to `loop` and the more diagnostic of the two: the loop being unbounded
means any slow module stalls the whole control chain, and an average rate hides that completely. A
single 200ms pass (`writeLine()`'s stall path is the known worst case) barely dents a rate measured
in tens of kHz, but it is 200ms in which no ESC is sent anything.

## Boot health

`core/boot_log.h` holds a fixed buffer of lines recorded during `setup()` — identity, whether
saved settings survived the fingerprint check, module/param/telemetry counts, free RAM, plus
whatever modules add. `main.cpp` emits it at the end of boot *and again after every `hello`*.

The replay is the point: USB CDC enumerates well after `setup()` runs and the app connects later
still, so a line merely printed at boot reaches nobody. `hello`'s response shape is deliberately
unchanged — the record follows it as separate unsolicited `{"log":...}` lines, which `app.js`
renders in the Terminal as `[device] …`.

## Health and the status LED

`core/health.h` holds one fault code for the whole firmware, reached through `core::health()`. It
is a singleton for the same reason `bootLog()` is one: a health verdict has exactly one
destination, and threading a reference through every module's `attach()` would be a lot of
plumbing to carry one enum. Raising a fault is the same gesture as writing a boot line, so
adding a new source needs no interface change anywhere.

**First fault wins.** When one fault cascades into another, the root cause is the actionable one
and the symptom is not. `fail()` also writes a `boot: fault=<name>` line, so the verdict replays
on every `hello`, and the code rides the telemetry frame as `fault`.

`Registry::add()` raises `Fault::Registry` on its own capacity checks rather than leaving callers
to inspect a `bool`. A module that does not fit vanishes from the schema entirely; putting the
fault at the point that detects it means no call site can forget to look.

The LED that reports all this is **not** a module — no parameters, no telemetry — so it is wired
directly in `main.cpp` beside `FlashStore` and `DfuTrigger`. Two things about its lifetime are
deliberate: it ticks *outside* the registry, so it survives the registry failing, and it
`begin()`s *before* `registerModules()`, so it is lit before anything is capable of failing. A
health indicator must not depend on the subsystem it reports on.

Healthy is an even 1Hz heartbeat. A stopped board latches its pins, so a steady-on LED looks
identical whether the firmware is running or died moments ago — the healthy signal has to be one
a stopped loop cannot counterfeit, and the blink is exactly that. Off would be worse still: it is
also unpowered, unflashed, or a pin never configured. Both steps stay wider than the slowest loop
iteration on purpose: a pass longer than a step would skip it entirely.

The panic handler overrides the Arduino core's weak `HardFault_Handler`, which otherwise falls
through to a silent infinite loop. It cannot use `delay()` or `millis()`: HardFault runs at
priority −1 and masks every interrupt that advances the tick, so the wait is a bare counting loop.

See [Status LED](status-led.md) for the patterns and fault codes themselves.

## The config-hash build gotcha

`config.h` reaches the board header via `#include BOARD_HEADER`, a macro-expanded include SCons
cannot resolve, so board-header edits did not trigger rebuilds — verified at the time by toggling
`FEATURE_STATUS_LED` and getting a byte-identical binary, reported as a successful build.
`firmware/scripts/config_hash.py` folds a hash of `include/**/*.h` into a `-D FW_CONFIG_HASH` so
any config edit forces a rebuild. Both envs reference it via `extra_scripts`; removing that line
silently reintroduces stale-binary builds.

That hash covers `include/**/*.h` only, and deliberately so — hashing `src/**/*.h` too would make
every source-header edit a full rebuild during ordinary development. The cost is that
`version.cpp`'s `__DATE__`/`__TIME__` do not re-stamp when only `src/` changed, and that stamp is
the app's **only** way to tell a running board apart from a bundled image (`isRunning()` in
`app.js` compares `built` and `version`, and `FW_VERSION` stays 1.0.0 by policy).

So `bundle_firmware.py` deletes `version.cpp.o` before each release build
(`force_version_rebuild()`): dev builds stay fast, and every image that actually **ships** carries
a truthful stamp. This is not hypothetical — the image that first contained the `revert` op
claimed the previous build's timestamp, and the Firmware page called it "currently running" on a
board that had never seen it.

## Wire protocol and the schema-driven UI

One JSON object per line, `\n`-terminated, over USB CDC serial (115200). Requests carry an `id`;
responses echo it. Messages with no `id` are unsolicited (telemetry, log) — that's what lets push
telemetry interleave safely with request/response on one connection. Firmware validates every
input independently of the backend and never trusts the host. Full spec in
`_notes/_archive/spec-configurator-core.md`; live contract in `docs/api.md`.

The registered modules' descriptors are the single source of truth for **validation** — type,
bounds, options — everywhere a value crosses into the device: `DeviceModel` (backend) caches the
`schema` wire op's response (`{params, tlm}`) and validates `set()` calls against it before ever
touching the wire, and Terminal `set`/INI restore go through the same path regardless of what any
page shows.

What the UI *displays* is not generated from the descriptor. `app/web/pages/{config,controller,
modes}.html` are hand-curated: each names the specific keys it shows, in whatever order and
grouping reads best for that page, via `Alpine.store('config').field(key)` /
`Alpine.store('telemetry').field(key)` — a lookup by key, not an iteration. Adding a firmware
parameter needs an explicit page decision and a hand-written label before it appears anywhere. A
curated page must degrade a key its connected board doesn't publish (`field(key).def === null`,
e.g. `esc1.*` on a board with `FEATURE_ESC1 0`) to an absent/disabled slot rather than crash.

Display hints never change what goes over the wire:

- `div`/`dec` — the wire always carries the device's native units (`vdd` is integer millivolts);
  only the browser divides and rounds.
- `showIf` (`{"key":...,"val":...}`) — `app.js` hides a parameter whose condition is unmet, but
  firmware and backend still validate and accept it, so Terminal `set` and INI restore keep
  working on a hidden parameter. This is what gives `rx.protocol` its per-protocol settings groups
  without `app.js` learning any protocol name.

`firmware/test/golden/schema.json` is a checked-in fixture, regenerated and diffed by a native
test (`test_schema_golden_fixture_matches_firmware`) and loaded directly by the Python tests, so a
firmware schema change that isn't reflected there fails a test instead of drifting silently.

## Persistence

The F411 has no real EEPROM (flash-emulated), and an erase stalls the MCU ~1s. Values apply to
RAM/hardware instantly on `set`; flash is written **only** on explicit `save`, guarded by a
magic/version/**fingerprint**/CRC header that falls back to defaults on any mismatch.

The fingerprint (`Registry::fingerprint()`) hashes every parameter's key, type and bounds, so
changing the enabled module set — or a parameter's range — discards saved settings rather than
reinterpreting stored bytes against a different table. `storage.cpp`'s `save()` does a read-back
verification after the flush so a real flash failure has an actual way to report `{"err":"flash"}`
instead of that path being dead code.

**Three states, three buttons.** A device's parameters can be at factory defaults, at what is
stored in flash, or at whatever RAM currently holds — and each Configuration button reaches
exactly one:

| Button | Direction | Dirty after |
|---|---|---|
| Save to flash | RAM → flash | no |
| Discard changes | flash → RAM (`revert` op) | no — RAM now equals flash |
| Load defaults | factory → RAM | yes, deliberately |

`revert` is the only op that reads the `Persistence::load()` seam back; before it, that seam was
called solely by `main.cpp` at boot, so flash was write-only from the host's point of view. It
falls back to defaults when nothing valid is stored (a fresh board, or a fingerprint mismatch) and
reports `src` so the host can say which happened — that field also decides the dirty flag, since
the fallback case *does* leave something worth writing. A revert notifies **every** module, so the
"exactly one call, module-local index" invariant applies across the whole table at once. Full
detail: `_notes/_archive/spec-config-revert.md`.

## Firmware bundling and in-app updates

The app ships the firmware that matches it: built images live in `app/firmware/` with a
`manifest.json`, produced by `app/tools/bundle_firmware.py` at release time (it builds first, then
derives every manifest field from the sources and the binary — nothing is typed in). A file picker
survives only as a collapsed *Advanced* path, where a vector-table check is all that stands
between picking `firmware.elf` out of `.pio/build` and a board that no longer enumerates.

**`app/firmware/` is gitignored build output, not source — the current, deliberate decision,
reversed from the opposite one.** The binaries used to be committed so that app/firmware pairing
was a checked-in fact; they now aren't, because the folder is what a release *produces* on the way
to a packaged executable, and committing a binary per firmware change per board does not scale
past one board. The pairing guarantee did not go away, it moved: the script is the only thing that
writes there, and it derives every field from the tree it just built. The manifest is ignored
alongside the binaries on purpose — a committed manifest whose `sha256` fields describe absent
files is precisely the drift the script exists to prevent. A source checkout having no firmware
until someone runs the script is the expected state, and only a developer ever sees it; a packaged
app is built after the script has run.

A multi-board run is **all-or-nothing**: `plan_entry()` builds and validates every env before
`release()` writes anything, so a second board failing to compile cannot leave a manifest that
looks like a complete release and isn't. By default the manifest describes exactly the envs named
in that one command and images from a previous run are pruned; `--add` merges instead. Pruning
only ever deletes files a *previous manifest listed* — never a wildcard sweep of the directory,
which would take a binary someone had put there by hand. `app/tests/test_bundle_firmware.py`
covers those invariants against a fixture tree with the `pio run` call injected out.

## DFU

The STM32 flashing mechanism, and the only one until the ESP32 target grew one (see the next
section). Which mechanism an image uses is not inferred anywhere — it is the manifest's `method`
field.

Getting into DFU has two paths, and **both must keep working**: the `dfu` wire op (one click) and
BOOT0+NRST by hand (for a board whose firmware is broken). That is also why the Firmware page,
like the Terminal, is *not* in `CONNECTION_REQUIRED_PAGES` — gating the recovery tool on a working
device is exactly backwards.

Two orderings in the firmware are load-bearing and easy to "simplify" into bugs:
`Bootloader::enterDfu()` only **arms** the reboot so `main.cpp` can flush the response first
(otherwise the host cannot tell a reboot from a dead board), and `initVariant()` **clears the RTC
magic before jumping** (otherwise a failed jump is an unrecoverable boot loop). `src/dfu.cpp` is
Arduino glue beside `storage.cpp`, not a module — it has no params and no telemetry, so the
registry would buy it nothing. Full detail: `_notes/_archive/spec-dfu-upload.md`.

**Nothing can identify a board in DFU mode** — every STM32F4 bootloader reports `0483:df11` and
nothing else. The app carries the `board` string forward from the last `hello` and says so plainly
when it has none, rather than guessing. Any future "auto-detect the right firmware" idea runs into
this wall first.

## DFU in the browser

`web-app/` flashes the same boards with no backend at all: `js/dfu.js` speaks DfuSe directly over
WebUSB. It takes an injected `USBDevice`-shaped object and never touches `navigator.usb` itself,
the same shape as `webserial-link.js` and for the same reason — the whole protocol tests against a
fake, and overwriting a device's flash is not something to leave covered only by hardware runs.

Three details are measured rather than assumed, and each is load-bearing:

- **A freshly entered bootloader reports `errFIRMWARE`/`dfuERROR` on its first status read.**
  Clearing that state is part of connecting, not error handling; without it the first poll throws
  before anything is written.
- **Chrome reports this bootloader's `interfaceName` as `null`**, so the sector map comes from
  `F4_DEFAULT_LAYOUT`. `parseMemoryLayout()` is what would adapt to a different flash geometry if
  the descriptor ever appears. The map cannot be a constant stride: F4 sectors are non-uniform.
- **The device detaches during the leave that ends a successful flash**, so the final transfer and
  status read may go unanswered. That is success, not a transfer failure.

Errors split along the line the backend build's HTTP/WS split already draws: pre-flight failures
(no device, a flash already running, a bad image, a checksum mismatch) throw from `Api`, while a
failure once writing has begun arrives as a `{type:'flash'}` frame on the `subscribe` channel —
the same frame shape the FastAPI build pushes over its WebSocket, so the page's handler is
identical in both.

`web-app/firmware/` is committed, unlike `app/firmware/`: a static site has no backend to build an
image on demand, so the bundle travels with the deployment. The manifest carries
`fw_source_sha256`, a path-sensitive hash of `firmware/{include,src}` and `platformio.ini`, and
`web-app/tests/firmware-bundle.test.js` recomputes it. That guard exists because the failure it
catches is silent: both versions stay `1.0.0` in this template, so binaries that have fallen
behind their sources still checksum perfectly against a manifest that is equally stale.

## Flashing an ESP32 (esptool)

The second flashing mechanism, dispatched off the manifest's `method` field (`"dfu"` /
`"esptool"`). Four decisions here are worth the reasoning:

**`method` lives in the manifest, not in a board-name check.** The bundler derives it once, from
the env's `-D FW_MCU_ESP32=1` build flag in `platformio.ini` — the same macro the firmware already
uses to guard ESP32-only driver bodies, so exactly one place in the tree says "this env is an
ESP32". Everything downstream (bundler validation, backend flasher choice, the UI's port picker)
reads the field. Inferring from `board` would put that knowledge in three places and let them
drift.

**An ESP32 flash needs an explicit port; a DFU flash does not.** A Black Pill in DFU mode leaves
its serial port and reappears as a distinct USB device (`0483:df11`) that `dfu-util` finds by
scanning USB, so the app never has to know where it was. An ESP32 never changes identity —
`esptool` resets it into its ROM bootloader over DTR/RTS on the *same* port it speaks JSON on.
There is nothing to poll for, so the port comes from the user, via a picker, and the `waiting`
phase is skipped entirely. Nothing is auto-selected unless `link.py`'s `_KNOWN_BOARDS` recognizes
it: guessing a port for a destructive write is worse than a disabled button and a hint. And
because the app may itself be holding that port open, the route releases the link first when the
requested port is the connected one (a connection on a *different* port is left alone).

**One `FlashSession` serves both.** The invariant it exists to enforce is "one flash at a time in
this app", not one per mechanism — two overlapping writes to a device's flash must be impossible,
whichever tool performs them. So the flasher is chosen per call (`start(..., flasher=, wait=)`)
rather than bound at construction, and both mechanisms share the one busy lock, the one event
stream, and the one `{"op", "pct", "line"}` progress shape the UI already renders.
`EsptoolFlasher` and `DfuFlasher` stay separate classes on purpose: same injected-`runner` testing
seam, but different argv shapes, different progress formats, and independent upstream release
cycles.

**The bundler merges four artifacts into one image.** An Arduino-framework ESP32 build emits
`bootloader.bin`, `partitions.bin`, `boot_app0.bin` and `firmware.bin` at four flash offsets.
Shipping them as four manifest entries would push ESP32-shaped special cases into the manifest
schema, the `Catalog`, and the UI; instead `esptool merge-bin` folds them into one file at release
time, so an ESP32 entry has the same one-file/one-sha256 shape as an STM32 one. The cost is
padding: the merged image is **sparse**, with `0xFF` from `0x0` to `0xFFF`, which is why its
magic-byte check reads offset `0x1000` and not `0` — `blob[0] == 0xE9` would reject every genuine
merged image *and* accept a bare, unbootable `firmware.bin`. The offsets and flash settings are
hardcoded to today's `esp32dev`/4MB/DIO/40MHz config; changing the partition table means updating
that table by hand.

**Neither `esptool` invocation may rely on `PATH`.** It is a pip dependency of `app/.venv/`, and
neither documented entry point puts that venv on `PATH` — the backend runs as
`app/.venv/bin/uvicorn ...` (only `activate` sets `PATH`) and the bundler runs under the *system*
`python3`. So the backend invokes the running interpreter's own `-m esptool`, and the bundler
resolves `app/.venv/bin/esptool` before falling back to `PATH`, the same way `find_pio()` resolves
PlatformIO's own venv copy first. A bare `"esptool"` argv[0] fails on a machine where esptool is
installed perfectly correctly.

## Disconnect detection

Two independent layers. The backend's `SerialLink` detects OS-level port loss (unplug)
immediately. The frontend watchdog additionally declares a distinct "stale" badge state if
telemetry hasn't arrived in 3x the configured interval while the port is still OS-connected —
catching a wedged-but-still-enumerated board. The 3x threshold is deliberate slack: a `save`'s ~1s
flash stall must never look like a disconnect.

## The simulated board

`sim://board` is a reserved port string that `SerialLink._default_open()` answers with a
`SimSerial` instead of a `serial.Serial`. That is the whole integration: the simulator sits at the
same seam the test suite already injects through, so `DeviceModel`, the HTTP routes, the WebSocket
push, the Terminal and INI restore run against it unmodified, and the `Api` seam is untouched.

Its schema is `app/backend/sim_profile.json`, a verbatim copy of `firmware/test/golden/schema.json`
— the fixture the native firmware test generates from a real `Dispatcher::handle("schema")` call.
`test_simulator.py` asserts the two are equal, so a firmware schema change fails the backend suite
rather than shipping a stale simulator. Refresh it with `pio test -e native` followed by a copy.

Telemetry is reactive, not canned: `sim_model.py` ports the firmware's own `mix()`, ESC arm state
machine and RC sweep, so changing a ratio, a source or a PWM rate moves the same readings it would
move on hardware. The ports use `trunc_div()` wherever the C divides, because C truncates toward
zero where Python's `//` floors — the two disagree on negative offsets, which is most of the mixer.

Two things differ from a real board on purpose. `rx.source` boots at `sim` rather than `uart`: a
simulated board has no UART to receive on, so `uart` would report a link that cannot exist. And the
system readings follow fixed triangles rather than a random walk, which keeps the model
reproducible and unit-testable. Selecting `uart` still correctly drops the link and zeroes the
channels, so the fidelity holds in both directions.

The board reports `board: "simulator"` and no `caps`. That keeps `/api/firmware/catalog` from
recommending a real flash image for a board that cannot be flashed, and leaves "Reboot to DFU"
honestly greyed out. Simulating DFU is deliberately out of scope: it would mean faking `dfu-util`'s
enumeration, which lives behind a different seam entirely.

A simulator session must never overwrite the remembered hardware identity. `DeviceModel` keeps
`_last_real_board` alongside `_info`, updated only when the connected port is not `sim://board`,
and the firmware catalog recommends from that rather than from `status()["board"]`. A board in DFU
mode cannot identify itself, so the recommendation rests entirely on the last `hello` — and
connecting the simulator between unplugging a board and flashing it would otherwise destroy the
recommendation at exactly the moment it matters, on a destructive operation.

Because the picker can now be showing a device that is not a port, the port scan runs on every
watchdog tick rather than only while disconnected. "Connected" no longer implies the port list is
settled: a board plugged in during a simulator session still has to appear in it. For the same
reason the simulator is only ever a provisional selection — the picker adopts a recognized board as
the desired port, so a board appearing later displaces the simulator, while choosing it by hand
makes it stick.

The default `tank_drive.arm_src` is `ch5`, which the sweep holds at a constant value below the
default 1700–2000µs arm window, so the ESCs sit at neutral in `ARM_ARMING`. Set `arm_src` to `none`
to arm whenever the link is fresh, to one of the high static channels near the top of the sweep's
spread to arm continuously, or to `ch1`/`ch2` to watch it arm and disarm as the sweeping stick
passes through the window.

## Versioning

Firmware and app are separate projects with independent version numbers that are not meant to
track each other. Firmware: `FW_VERSION` in `firmware/include/config.h`, reported over the wire by
`hello` (`name`/`ver`/`built`/`mods`, alongside the unchanged `fw` display string). App (backend +
web UI): `APP_VERSION` at the top of `app/web/app.js`. **Both stay 1.0.0 in this template** —
bumps happen in real forked projects, not here.
