# Architecture notes

The reasoning behind the rules listed in `CLAUDE.md`. That file is loaded into every session;
this one is read when a change actually touches the area. Where the two disagree, this file has
the detail and `CLAUDE.md` has the rule.

## The `Api` seam

`web-app/`'s UI reaches the device exclusively through the `Api` object in `js/api.js`. That
object is the entire porting surface for a hypothetical future Electron rewrite, and the rules are
cheap to keep and expensive to retrofit:

- No serial, WebUSB or network call anywhere outside it.
- No `Api` method may take or return a browser-only type (a `File`, a `Blob`, a stream) — bytes,
  strings, plain objects and callbacks only.
- The push channel stays `Api.subscribe(handler) -> unsubscribe`, delivering `{type, data}` frames
  and owning its own reconnection, rather than handing a transport back to a caller that would
  touch its lifecycle events.

Two exceptions are deliberate and documented rather than accidental. `showPage()` fetches
`pages/<name>.html` — this app's own static markup, not a device request. And `Api.connect()`
takes a Web Serial `SerialPort` object, because Web Serial's permission model has no other way to
name a port: a port only exists as the object the browser's own chooser handed back.

A third path, `Api.connectSim()`, needs no exception at all: it swaps `activeDevice` to a
`DeviceModel` wrapping `SimLink` (`js/sim-link.js`), which implements `SerialLink`'s exact public
surface and runs an in-process device behind it. No browser type crosses the seam for it.

`flashUpload` really did take a DOM `File` once, because the API it fed accepted one — which is
exactly how this kind of coupling gets in. Full contract, plus a grep check for the mechanical
part: `_notes/_archive/review-electron-port-readiness.md`.

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

See [Status LED](../docs/reference/status-led.md) for the patterns and fault codes themselves.

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
`app.js` compares `built` and `version` — `FW_VERSION` alone isn't enough to tell two builds of
the same release apart).

So `bundle_firmware.py` deletes `version.cpp.o` before each release build
(`force_version_rebuild()`): dev builds stay fast, and every image that actually **ships** carries
a truthful stamp. This is not hypothetical — the image that first contained the `revert` op
claimed the previous build's timestamp, and the Firmware page called it "currently running" on a
board that had never seen it.

## Wire protocol and the schema-driven UI

One JSON object per line, `\n`-terminated, over USB CDC serial (115200). Requests carry an `id`;
responses echo it. Messages with no `id` are unsolicited (telemetry, log) — that's what lets push
telemetry interleave safely with request/response on one connection. Firmware validates every
input itself and never trusts the host. Full spec in
`_notes/_archive/spec-configurator-core.md`; live contract in [`protocol.md`](protocol.md).

The registered modules' descriptors are the single source of truth for **validation** — type,
bounds, options — everywhere a value crosses into the device: `device-model.js` caches the
`schema` wire op's response (`{params, tlm}`) and validates `set()` calls against it before ever
touching the wire, and Terminal `set`/INI restore go through the same path regardless of what any
page shows.

What the UI *displays* is not generated from the descriptor. `web-app/pages/{config,controller,
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
  the firmware still validates and accepts it, so Terminal `set` and INI restore keep working on a
  hidden parameter. This is what gives `rx.protocol` its per-protocol settings groups without
  `app.js` learning any protocol name.

`firmware/test/golden/schema.json` is a checked-in fixture, regenerated and diffed by a native
test (`test_schema_golden_fixture_matches_firmware`), so a firmware schema change that isn't
reflected there fails a test instead of drifting silently.

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

The app ships the firmware that matches it: built images live in `web-app/firmware/` with a
`manifest.json`, produced by `app/tools/bundle_firmware.py` at release time (it builds first, then
derives every manifest field from the sources and the binary — nothing is typed in). A file picker
survives only as a collapsed *Advanced* path, where a vector-table check is all that stands
between picking `firmware.elf` out of `.pio/build` and a board that no longer enumerates.

**Those binaries are committed**, unlike most build output: a static site has no server to build
an image on demand, so what it flashes has to travel with the deployment. The guarantee that they
match their sources is a test rather than a convention — the manifest carries `fw_source_sha256`,
a path-sensitive hash of `firmware/{include,src}` and `platformio.ini`, and
`web-app/tests/firmware-bundle.test.js` recomputes it. That guard exists because the failure it
catches is silent: a version bump is not required for every source change, so binaries that have
fallen behind their sources can still checksum perfectly against a manifest that is equally stale.

A multi-board run is **all-or-nothing**: `plan_entry()` builds and validates every env before
`release()` writes anything, so a second board failing to compile cannot leave a manifest that
looks like a complete release and isn't. By default the manifest describes exactly the envs named
in that one command and images from a previous run are pruned; `--add` merges instead. Pruning
only ever deletes files a *previous manifest listed* — never a wildcard sweep of the directory,
which would take a binary someone had put there by hand. The bundler's own test suite covers those
invariants against a fixture tree with the `pio run` call injected out.

## DFU

How a board is flashed. Which mechanism an image uses is not inferred from its board name
anywhere — it is the manifest's `method` field, derived once by the bundler, so a second mechanism
would be a manifest value rather than a special case spread through the app.

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

Flashing happens in the page itself: `js/dfu.js` speaks DfuSe directly over WebUSB, with no host
tool involved. It takes an injected `USBDevice`-shaped object and never touches `navigator.usb`
itself, the same shape as `webserial-link.js` and for the same reason — the whole protocol tests against a
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

Errors split by when they happen, not by kind: pre-flight failures (no device, a flash already
running, a bad image, a checksum mismatch) throw from the `Api` call the page made, while a
failure once writing has begun arrives as a `{type:'flash'}` frame on the `subscribe` channel.
A flash outlives the page that started it — the user can navigate away mid-write — so past that
point the progress channel is the only place a result can reliably land.

### The permission ladder

WebUSB cannot see a bootloader this origin has never been granted — `getDevices()` returns nothing
and never prompts — and `requestDevice()` only opens its chooser while the page holds *transient
user activation*, measured at exactly 5 seconds in Chrome. A flash spends more than that before it
knows it needs one: the confirm dialog (whose open time counts), the image fetch and checksum, the
`dfu` wire op, and the bounded wait for the bootloader to enumerate. Past the window the call
throws `SecurityError` with nothing shown, which is indistinguishable from a refusal unless the
error name is read.

So `api.js` climbs three rungs, the same shape Betaflight Configurator uses:

1. `waitForDfuArrival()` polls what this origin may already see. No activation needed, so a browser
   that has flashed this board before never leaves this rung. Bounded by `DFU_WAIT_MS`, kept short
   so rung two still has a chance.
2. `promptForDfuDevice()` calls `requestDevice()` anyway — the original click is sometimes still
   live, and then the chooser opens with no extra step. It reports `blocked` for `SecurityError`
   specifically; every other empty answer is genuinely no device.
3. `parkForGrant()` holds the image, keeps `flashBusy` set and publishes a `needsdevice` frame. The
   page opens a modal whose button calls `Api.grantDfuAndResume()`, and *that* click is an
   activation the chooser accepts. Nothing has been written at this point, so the parked flash
   resumes rather than restarts; `Api.cancelDfuGrant()` is the other way out and fails it.

The modal's markup lives in `index.html`, not `pages/firmware.html`: a flash keeps running while
another page is mounted, so it has to exist wherever the frame arrives.

## Disconnect detection

Two independent layers. `webserial-link.js` detects port loss (unplug) as soon as the browser
reports the stream closed. The UI watchdog additionally declares a distinct "stale" badge state if
telemetry hasn't arrived in 3x the configured interval while the port is still open — catching a
wedged-but-still-enumerated board. The 3x threshold is deliberate slack: a `save`'s ~1s
flash stall must never look like a disconnect.

## Versioning

Firmware and app share one project-wide version number, bumped together. Firmware: `FW_VERSION`
in `firmware/include/config.h`, reported over the wire by `hello` (`name`/`ver`/`built`/`mods`,
alongside the unchanged `fw` display string). App: `APP_VERSION` at the top of
`web-app/js/app.js`. Both are kept equal by hand — a mismatch means one was bumped without the
other.
