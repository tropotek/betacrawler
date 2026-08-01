# ESP32 WROOM target — design

## Purpose

Add a new PlatformIO firmware environment for a generic ESP32 WROOM (clone) dev board: no
external modules wired up, onboard LED only, and the onboard ESP32 WiFi radio doing what an
external ESP-01 AT-command module does on the STM32 boards. Flashed over its own USB-UART bridge
(esptool), never an ST-Link.

## Scope

This is not "copy `_template.h` and flip flags" — three pieces are STM32-specific at the
implementation level and need a genuine second body for ESP32:

- `storage.cpp` calls STM32duino's buffered-EEPROM-emulation functions
  (`eeprom_buffer_fill`/`eeprom_buffered_write_byte`/`eeprom_buffer_flush`) — not available on the
  ESP32 Arduino core.
- `hardware/system/system_driver.cpp` reads STM32F411 factory calibration addresses
  (`VREFINT_CAL`/`TS_CAL1`/`TS_CAL2`) directly — meaningless on ESP32 silicon.
- `hardware/wifi/wifi_driver.cpp` is an AT-command client talking over UART to an *external*
  ESP-01 module. An onboard ESP32 has no AT firmware to talk to — it needs to drive its own radio
  through the Arduino core's `WiFi.h`.

Everything else (`led`, `button`, `servo`, `esc`, `rx`, `st7789_240x240`, `dfu`) needs no new code:
the new board header simply leaves those features off, and each already compiles harmlessly when
its `FEATURE_*` flag is `0` — except `wifi_driver.cpp`, which currently does not (see Bug below).

Out of scope, deliberately:
- Any other ESP32 board variant (S2/S3/C3/etc.) — this targets the classic dual-core WROOM-32 only.
- Flashing this board through the app's Firmware page / `dfu` wire op. That mechanism is
  STM32-DFU-specific; this board has no `FEATURE_DFU` and is flashed with `pio run -t upload`
  directly, same as any other PlatformIO env. It is not added to `app/firmware/`'s release catalog.
- A real onboard temperature or supply-voltage reading. Stubbed to `0` (see Telemetry below).
- Native test coverage for the new driver bodies. No existing `*_driver.cpp` is covered by
  `pio test -e native` (drivers are hardware, verified on hardware) — the ESP32 ones follow the
  same rule as the STM32 ones they sit beside.

## Bug found during design: `blackpill_f401ce` currently fails to build

Confirmed by running `pio run -e blackpill_f401ce`. `wifi_driver.cpp` is compiled unconditionally
by PlatformIO (neither STM32 env sets a `build_src_filter`), and the file `#error`s unless
`WIFI_RX_PIN`/`WIFI_TX_PIN`/`WIFI_BAUD` are defined — which `blackpill_f401ce.h` never does, since
that board never turned `FEATURE_WIFI` on. This predates this design and is fixed as a side effect
of it (see below) rather than as unrelated cleanup, because the fix is the same mechanism this
design already needs.

## Mechanism: self-guarded per-architecture files

Rather than juggling a `build_src_filter` per environment (the fragile pattern that let the
f401 bug happen unnoticed), each architecture-specific file guards its own body with `#if`, the
way `dfu.cpp` already guards its STM32-register code with `#if FEATURE_DFU`. A file compiles to an
empty translation unit on an environment it doesn't apply to, rather than needing to be excluded
from that environment's file list.

New macro, added to `include/config.h` alongside the existing `FEATURE_*` defaults:

```c
#ifndef FW_MCU_ESP32
#define FW_MCU_ESP32 0
#endif
```

Set to `1` only by the new environment's `build_flags`.

| File | Guard | New ESP32 file / guard | Class |
|---|---|---|---|
| `storage.cpp` | `#if !FW_MCU_ESP32` (added) | `storage_esp32.cpp`, `#if FW_MCU_ESP32` | Same `FlashStore` (declared in `storage.h`, which has no architecture-specific fields — `private: const core::Registry& reg_` only). ESP32 body uses the ESP32 core's `EEPROM.h` emulation (`begin(size)`/`write(addr,val)`/`read(addr)`/`commit()`), a near 1:1 swap for the STM32 buffered-byte calls: same `Header` struct, same CRC/fingerprint/magic scheme, same read-back-after-write check. |
| `hardware/system/system_driver.cpp` | `#if !FW_MCU_ESP32` (added) | `system_esp32_driver.cpp`, `#if FW_MCU_ESP32` | Same `SystemDriver` (declared in `system_driver.h`, which is just `void readTelemetry(core::TlmValue*) override` — no fields). `T_UP`/`T_CLK`/`T_RAM` populated for real (`millis()`, `getCpuFrequencyMhz()`, `ESP.getFreeHeap()`); `T_TEMP`/`T_VDD` stubbed to `0`. |
| `hardware/wifi/wifi_driver.cpp` | `#if FEATURE_WIFI && !FW_MCU_ESP32` (added — this is the f401 fix) | `wifi_esp32_driver.h` / `.cpp`, `#if FEATURE_WIFI && FW_MCU_ESP32` | New class `wifi::WifiEsp32Driver`, own header (the STM32 header carries AT-parser-specific private state — UART line buffer, AT state enum — not worth sharing). Implements the same `core::Module` + `core::WifiScanner` interface against the same `wifi_params.h` descriptor: `P_SSID`/`P_PASSWORD` params, `T_STATUS`/`T_RSSI`/`T_IP` telemetry, `STATUS_OFF/CONNECTING/CONNECTED/FAILED` enum. Schema is identical either way — only the driver body differs. Uses `WiFi.begin()`/`WiFi.status()` in place of the AT join state machine, `WiFi.scanNetworks(true)` + polled `WiFi.scanComplete()` in place of `AT+CWLAP` line parsing. `modules.cpp`'s existing wifi wiring block (already branches on `FW_TARGET_ARDUINO`) gains one more `#if FW_MCU_ESP32` branch to pick this class instead of `WifiDriver`. |

No other file changes. `dfu.cpp` needs nothing: `FEATURE_DFU` defaults to `0` and its existing
`#else` stub (already exercised by any board with DFU off) covers this board with zero new code.

## Board header: `boards/esp32_wroom32.h`

```c
#define BOARD_ID "esp32_wroom32"

#define FEATURE_LED     1   // onboard LED
#define FEATURE_BUTTON  0
#define FEATURE_ST7789_240X240 0
#define FEATURE_SERVO   0
#define FEATURE_ESC     0
#define FEATURE_RX      0
#define FEATURE_WIFI    1   // onboard radio, not an external ESP-01
#define FEATURE_DFU     0   // no STM32-style ROM DFU on this part; esptool over USB instead

#define LED_PIN         2   // common on WROOM-32 devkit clones -- verify on your specific board
#define LED_ACTIVE_LOW  0
```

`WIFI_RX_PIN`/`WIFI_TX_PIN`/`WIFI_BAUD` are **not** defined here — those belong to the STM32 AT-UART
driver only, and `wifi_esp32_driver.cpp` needs none of them (it drives the radio in-chip, no UART).

## PlatformIO environment

```ini
[env:esp32_wroom32]
platform = espressif32
board = esp32dev
framework = arduino
build_flags =
    -Wswitch
    -Iinclude
    -D FW_TARGET_ARDUINO=1
    -D FW_MCU_ESP32=1
    -D BOARD_HEADER='"boards/esp32_wroom32.h"'
monitor_speed = 115200
extra_scripts = pre:scripts/config_hash.py
lib_deps =
    bblanchon/ArduinoJson@^7.0.4
```

No `upload_protocol`/`debug_tool` (esptool is the `espressif32` platform default — no ST-Link
involved). No GFX display library in `lib_deps`: this board never enables
`FEATURE_ST7789_240X240`, and unlike the STM32 envs' comment about keeping it "harmless but
unused" for the native build, this env has no native-build reason to carry it either.

## Flashing procedure (USB only)

```
~/.platformio/penv/bin/pio run -e esp32_wroom32 -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

esptool auto-detects the serial port and resets the board into its ROM bootloader via DTR/RTS
toggling through the onboard USB-UART bridge (CP2102 or CH340, depending on the clone). If a cheap
clone lacks the auto-reset transistors and upload hangs at `Connecting....`, hold the board's
**BOOT/IO0** button until it starts writing (`Writing at 0x...`), then release. If two serial
devices are attached and the wrong one is picked, add `upload_port = <device>` to this env, same
remedy `platformio.ini` already documents for the two-ST-Link case.

## Verification

1. `pio run -e blackpill_f411ce` and `pio run -e blackpill_f401ce` both still compile — the second
   one is the f401 bug fix, confirmed by it now succeeding instead of `#error`ing.
2. `pio run -e esp32_wroom32` compiles.
3. Flash the board; `pio device monitor` shows the boot log (`boot: silkscreen 1.0.0
   (esp32_wroom32) built ...`, `modules=... params=... tlm=...`).
4. Point the backend (`uvicorn backend.main:app`) at the board's serial port; confirm `hello`
   reports `wifi`, `led`, `system`, `device` modules and no others.
5. From the app UI: join a real WiFi network, confirm `wifi.status`/`wifi.rssi`/`wifi.ip`
   telemetry updates through `connecting` → `connected`; run a scan and confirm results list;
   toggle the LED on/off.
6. Save settings, power-cycle the board (unplug/replug USB), confirm SSID/password and LED state
   survive — exercising the new `storage_esp32.cpp` path.

## Capacity check

`FW_MAX_MODULES=8`, `FW_MAX_PARAMS=32`, `FW_MAX_TLM=40` (sized for `blackpill_f411ce`'s eight
modules) comfortably cover this board's four (`device`, `system`, `led`, `wifi`). No `config.h`
change needed.
