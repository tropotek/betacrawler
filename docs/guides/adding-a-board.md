# Adding a Board

Adding a board is two steps and touches no source file: the parameters, telemetry fields and UI
controls a module declares appear automatically (firmware schema → backend → web form) once its
`FEATURE_*` flag is on.

## 1. Copy the template header

```bash
cp firmware/include/boards/_template.h firmware/include/boards/<your-board>.h
```

Fill in:

- `BOARD_ID` — reported by the `hello` op and shown in the app.
- The `FEATURE_*` flags for whatever the board actually has. Anything left out defaults to `0`.
- The pin map for each feature you turned on — each module's driver documents which macros it
  expects; a missing one is a compile error in that driver, never a silent misconfiguration.

`firmware/include/boards/blackpill_f411ce.h` is the fully worked reference example — copy its
comments' *reasoning* (why a given timer or pin was picked) as a model for your own board's
header, not its literal values.

## 2. Add a PlatformIO environment

In `firmware/platformio.ini`:

```ini
[env:<your-board>]
platform     = ststm32          ; or espressif32, ...
board        = <pio board id>
framework    = arduino
monitor_speed = 115200          ; must match FW_SERIAL_BAUD
build_flags  =
    -Wswitch -Iinclude
    -D FW_TARGET_ARDUINO=1
    -D BOARD_HEADER='"boards/<your-board>.h"'
    ; Required if FEATURE_RX is 1 on this board: the Arduino default RX ring
    ; (64 bytes) tears on nearly every frame at CRSF's ~150fps.
    -D SERIAL_RX_BUFFER_SIZE=256
lib_deps     = bblanchon/ArduinoJson@^7.0.4
extra_scripts = pre:scripts/config_hash.py
```

`extra_scripts = pre:scripts/config_hash.py` is required on every board, not optional: editing a
board header doesn't trigger a rebuild without it, because `#include BOARD_HEADER` is invisible
to PlatformIO's SCons-based dependency scanner.

## A different MCU family (not STM32)

Swapping `platform` alone isn't enough once the family changes. `[env:esp32_wroom32]` in
`platformio.ini` plus `firmware/include/boards/esp32_wroom32.h` is a worked example — follow that
pair, not the STM32-only template, when your MCU family differs. Three things it had to solve:

1. **MCU-specific source bodies.** Any shared file with hardware-specific code (`storage.cpp`,
   `system_driver.cpp`, `wifi_driver.cpp`, …) needs a family-specific twin (`storage_esp32.cpp`,
   etc.) if the new MCU can't reuse the existing body's peripheral calls, each guarded by a new
   `FW_MCU_<FAMILY>` macro.
2. **C++ standard mismatch.** `espressif32` defaults to `gnu++11`. Add `-std=gnu++17` to
   `build_flags` **and** `build_unflags = -std=gnu++11` — the framework appends its own `-std`
   after `build_flags`, so the flag alone is silently overridden without the unflag.
3. **`config_hash.py` is still required.** Easy to forget while chasing the two points above.

## Verify it

```bash
cd firmware
~/.platformio/penv/bin/pio run -e <your-board>
```

A compile error naming a missing macro means a driver needs a pin definition your header didn't
provide — that's the intended failure mode, not a bug.

## Documenting your own build

Once your board is up, `firmware/docs/` (`BOM.md`, `ASSEMBLY.md`) and `app/docs/`
(`USER_GUIDE.md`) are placeholder templates for your fork's own bill of materials, assembly
instructions and user guide — fill them in rather than writing from scratch.
