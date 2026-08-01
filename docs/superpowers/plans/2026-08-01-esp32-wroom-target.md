# ESP32 WROOM Target Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a new PlatformIO firmware environment (`esp32_wroom32`) for a generic ESP32 WROOM
dev board with no external modules — onboard LED and onboard WiFi radio only — flashed over USB
via esptool, and fix a pre-existing `blackpill_f401ce` build failure the same mechanism resolves.

**Architecture:** Three files (`storage.cpp`, `hardware/system/system_driver.cpp`,
`hardware/wifi/wifi_driver.cpp`) are STM32-specific at the implementation level. Each gets a new
`FW_MCU_ESP32`-guarded twin (`storage_esp32.cpp`, `system_esp32_driver.cpp`,
`wifi_esp32_driver.{h,cpp}`) implementing the same class/interface. Every file — old and new —
guards its own body with `#if`, the pattern `dfu.cpp` already uses for `#if FEATURE_DFU`, so
PlatformIO compiling every `.cpp` under `src/` as its own translation unit (regardless of what
includes it) never breaks a board that doesn't apply to that file.

**Tech Stack:** PlatformIO, Arduino framework, `platform = espressif32` (`board = esp32dev`),
ESP32 Arduino core's `WiFi.h` and `EEPROM.h`, ArduinoJson (already a dependency).

## Global Constraints

- `pio` is not on PATH — invoke as `~/.platformio/penv/bin/pio` (from `docs/api.md`'s companion
  `CLAUDE.md`).
- Compile-check from `firmware/`: `~/.platformio/penv/bin/pio run -e <env>`.
- Native suite (unaffected by this plan except `modules.cpp`, which is include-filtered into it):
  `~/.platformio/penv/bin/pio test -e native`.
- Flash is written only on explicit `save` — unchanged by this plan; `storage_esp32.cpp` must
  preserve that contract exactly as `storage.cpp` does.
- `config_hash.py` stays in `extra_scripts` for every environment that includes a `BOARD_HEADER` —
  the new `esp32_wroom32` env needs it too, or board-header edits won't trigger rebuilds.
- No panel/module presence detection — not applicable here, this board has no display.
- `core/` never names a feature, module-local parameter indices only, observers are const — none
  of this plan touches `core/`, so these hold automatically; called out only so no task is tempted
  to take a shortcut through `core/` to save a file.
- Schema-driven UI: this plan changes zero parameter/telemetry *shape* — `wifi_params.h`/
  `system_params.cpp`'s descriptors are reused unchanged by the new drivers — so `app/web/app.js`
  needs no changes and none are in scope here.

---

### Task 1: Fix `blackpill_f401ce`'s build failure and add the `FW_MCU_ESP32` macro

`wifi_driver.cpp` is compiled unconditionally by PlatformIO on both STM32 envs (neither sets a
`build_src_filter`) and `#error`s unless `WIFI_RX_PIN`/`WIFI_TX_PIN`/`WIFI_BAUD` are defined.
`blackpill_f401ce.h` never defines them (it never turned `FEATURE_WIFI` on), so
`pio run -e blackpill_f401ce` currently fails. Confirmed by running it during design.

This task adds the `FW_MCU_ESP32` macro every later task depends on, and gives `wifi_driver.cpp`
the file-body guard it should have had from the start — which incidentally fixes the f401 bug,
since a board with `FEATURE_WIFI` off now compiles this file to an empty translation unit instead
of erroring.

**Files:**
- Modify: `firmware/include/config.h`
- Modify: `firmware/src/hardware/wifi/wifi_driver.cpp`

**Interfaces:**
- Produces: `FW_MCU_ESP32` (0/1 macro, defaults to 0 in `config.h`, alongside the existing
  `FEATURE_*` defaults) — every later task's `#if` guards depend on this existing.

- [ ] **Step 1: Confirm the current failure**

Run: `cd firmware && ~/.platformio/penv/bin/pio run -e blackpill_f401ce`
Expected: FAILS with `#error "FEATURE_WIFI is on but the board header defines no WIFI_RX_PIN"`
(and two more `#error`s for `WIFI_TX_PIN`/`WIFI_BAUD`, then `'WIFI_RX_PIN' was not declared`
errors further down).

- [ ] **Step 2: Add `FW_MCU_ESP32` to `config.h`**

In `firmware/include/config.h`, find:

```c
#ifndef FEATURE_WIFI
#define FEATURE_WIFI 0
#endif
```

Add immediately after it:

```c
#ifndef FEATURE_WIFI
#define FEATURE_WIFI 0
#endif

// Set only by an ESP32 environment's own build_flags (see platformio.ini's
// [env:esp32_wroom32]) to select the ESP32-native bodies of storage.cpp,
// hardware/system/system_driver.cpp and hardware/wifi/wifi_driver.cpp --
// each guards its own STM32-specific body with `#if !FW_MCU_ESP32` and is
// paired with a `*_esp32_*` file guarded the other way, so every board
// compiles cleanly with no per-environment build_src_filter bookkeeping.
// FW_TARGET_ARDUINO alone still answers "is this a real target at all,"
// exactly as it always has -- this only disambiguates which real target.
#ifndef FW_MCU_ESP32
#define FW_MCU_ESP32 0
#endif
```

- [ ] **Step 3: Guard `wifi_driver.cpp`'s body**

In `firmware/src/hardware/wifi/wifi_driver.cpp`, replace the top of the file:

```cpp
#include <Arduino.h>
#include "hardware/wifi/wifi_driver.h"
#include "hardware/wifi/wifi_params.h"
#include "config.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

#ifndef WIFI_RX_PIN
#error "FEATURE_WIFI is on but the board header defines no WIFI_RX_PIN"
#endif
```

with:

```cpp
#include "hardware/wifi/wifi_driver.h"
#include "hardware/wifi/wifi_params.h"

// This whole file is the STM32-side driver: an external ESP-01 module
// talked to over UART with AT commands. FW_MCU_ESP32 selects
// wifi_esp32_driver.cpp instead, which drives an onboard ESP32's own radio
// directly through WiFi.h -- no UART, no AT parser. The body (not just the
// class) must be guarded: PlatformIO compiles every .cpp under src/ as its
// own translation unit no matter what includes it, so an unguarded file
// here would still demand WIFI_RX_PIN/WIFI_TX_PIN/WIFI_BAUD (or fail to
// build against a different HardwareSerial constructor) on any board that
// never defines them -- exactly what silently broke blackpill_f401ce
// (FEATURE_WIFI off, no WIFI_* pins) until this guard was added.
#if FEATURE_WIFI && !FW_MCU_ESP32

#include <Arduino.h>
#include "config.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

#ifndef WIFI_RX_PIN
#error "FEATURE_WIFI is on but the board header defines no WIFI_RX_PIN"
#endif
```

Then find the very end of the file:

```cpp
size_t WifiDriver::pollPush(char* out, size_t cap) {
  if (!scanResultReady_) return 0;
  scanResultReady_ = false;

  JsonDocument doc;
  JsonArray arr = doc["scan"].to<JsonArray>();
  for (uint8_t i = 0; i < scanCount_; ++i) {
    JsonObject e = arr.add<JsonObject>();
    e["ssid"] = scanResults_[i].ssid;
    e["rssi"] = scanResults_[i].rssi;
  }
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

}  // namespace wifi
```

and replace with:

```cpp
size_t WifiDriver::pollPush(char* out, size_t cap) {
  if (!scanResultReady_) return 0;
  scanResultReady_ = false;

  JsonDocument doc;
  JsonArray arr = doc["scan"].to<JsonArray>();
  for (uint8_t i = 0; i < scanCount_; ++i) {
    JsonObject e = arr.add<JsonObject>();
    e["ssid"] = scanResults_[i].ssid;
    e["rssi"] = scanResults_[i].rssi;
  }
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

}  // namespace wifi

#endif  // FEATURE_WIFI && !FW_MCU_ESP32
```

(Everything between the pin `#error` checks and `pollPush` — the state machine, `handleLine`,
`drainUart`, etc. — is unchanged; only the top and bottom of the file move.)

- [ ] **Step 4: Verify the fix**

Run: `~/.platformio/penv/bin/pio run -e blackpill_f401ce`
Expected: SUCCESS.

Run: `~/.platformio/penv/bin/pio run -e blackpill_f411ce`
Expected: SUCCESS (regression check — this board has `FEATURE_WIFI=1`, so it still compiles the
real driver body).

- [ ] **Step 5: Commit**

```bash
cd /home/godar/Projects/stm32/silkscreen
git add firmware/include/config.h firmware/src/hardware/wifi/wifi_driver.cpp
git commit -m "$(cat <<'EOF'
fix(wifi): guard wifi_driver.cpp body, fixing blackpill_f401ce build

wifi_driver.cpp was compiled unconditionally (no build_src_filter on
either STM32 env) and unconditionally required WIFI_RX_PIN/WIFI_TX_PIN/
WIFI_BAUD, which blackpill_f401ce.h never defines since it never turned
FEATURE_WIFI on. Wrapping the body in #if FEATURE_WIFI fixes the build
and establishes the guard pattern the upcoming ESP32 target relies on.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Storage — split `storage.cpp` / add `storage_esp32.cpp`

**Files:**
- Modify: `firmware/src/storage.cpp`
- Create: `firmware/src/storage_esp32.cpp`

**Interfaces:**
- Consumes: `core::FlashStore` (declared `firmware/src/storage.h`, unchanged) — `bool save(const
  core::Params&)`, `bool load(core::Params*)`, constructed with `const core::Registry&`.
- Consumes: `FW_MCU_ESP32` from Task 1.
- Produces: `FlashStore::save`/`FlashStore::load` now exist in exactly one of the two files per
  build, both implementing the same header — no other file changes.

- [ ] **Step 1: Guard `storage.cpp`'s body**

In `firmware/src/storage.cpp`, replace the top:

```cpp
#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>
#include "storage.h"

using namespace core;
```

with:

```cpp
#include "storage.h"

// STM32-side body -- see storage_esp32.cpp for the ESP32 counterpart, and
// wifi_driver.cpp's own comment (firmware/src/hardware/wifi/wifi_driver.cpp)
// for why each architecture-specific file guards its own body rather than
// relying on a per-environment build_src_filter.
#if !FW_MCU_ESP32

#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>

using namespace core;
```

And at the very end of the file, after the closing brace of `FlashStore::load`, add:

```cpp
  memcpy(p->rawMutable(), buf, payloadLen);
  return true;
}

#endif  // !FW_MCU_ESP32
```

(replacing the previous bare `return true;\n}` ending).

- [ ] **Step 2: Regression-check the STM32 envs**

Run: `~/.platformio/penv/bin/pio run -e blackpill_f411ce`
Run: `~/.platformio/penv/bin/pio run -e blackpill_f401ce`
Expected: both SUCCESS, unchanged behavior (`FW_MCU_ESP32` is 0 for both, so the body still
compiles exactly as before).

- [ ] **Step 3: Write `storage_esp32.cpp`**

Create `firmware/src/storage_esp32.cpp`:

```cpp
#include "storage.h"

#if FW_MCU_ESP32

#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>

using namespace core;

static const uint32_t kMagic = 0x4D444C31;  // "MDL1"
static const uint16_t kVersion = 2;

struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t crc;
  uint32_t fingerprint;
};

// arduino-esp32's EEPROM library emulates this many bytes of byte-addressable
// storage inside the "eeprom" NVS partition every default partition table
// (esp32dev's included) already reserves -- comfortably above what this
// board ever needs (a Header plus FW_MAX_PARAMS Values, well under 1.5KB).
static const size_t kEepromSize = sizeof(Header) + sizeof(Value) * FW_MAX_PARAMS;

static uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= (uint16_t)d[i] << 8;
    for (uint8_t b = 0; b < 8; ++b)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

bool FlashStore::save(const core::Params& p) {
  const uint8_t* payload = reinterpret_cast<const uint8_t*>(p.raw());
  const size_t payloadLen = sizeof(Value) * reg_.paramCount();

  Header h;
  h.magic = kMagic;
  h.version = kVersion;
  h.crc = crc16(payload, payloadLen);
  h.fingerprint = reg_.fingerprint();

  EEPROM.begin(kEepromSize);
  size_t off = 0;
  const uint8_t* hp = reinterpret_cast<const uint8_t*>(&h);
  for (size_t i = 0; i < sizeof(Header); ++i) EEPROM.write(off++, hp[i]);
  for (size_t i = 0; i < payloadLen; ++i)     EEPROM.write(off++, payload[i]);

  // Unlike the STM32 buffered-EEPROM API, commit() reports failure directly
  // -- but the header is still read back below anyway, exactly like the
  // STM32 side, so a partial/corrupted write is caught the same way either
  // path.
  if (!EEPROM.commit()) return false;

  Header check;
  uint8_t* cp = reinterpret_cast<uint8_t*>(&check);
  size_t roff = 0;
  for (size_t i = 0; i < sizeof(Header); ++i) cp[i] = EEPROM.read(roff++);

  return check.magic == h.magic && check.version == h.version &&
         check.crc == h.crc && check.fingerprint == h.fingerprint;
}

bool FlashStore::load(core::Params* p) {
  EEPROM.begin(kEepromSize);

  Header h;
  uint8_t* hp = reinterpret_cast<uint8_t*>(&h);
  size_t off = 0;
  for (size_t i = 0; i < sizeof(Header); ++i) hp[i] = EEPROM.read(off++);

  // Any mismatch -- corrupt, older firmware, or a different set of enabled
  // modules -- falls back to defaults rather than guessing.
  if (h.magic != kMagic || h.version != kVersion) return false;
  if (h.fingerprint != reg_.fingerprint()) return false;

  const size_t payloadLen = sizeof(Value) * reg_.paramCount();
  static uint8_t buf[sizeof(Value) * FW_MAX_PARAMS];
  for (size_t i = 0; i < payloadLen; ++i) buf[i] = EEPROM.read(off++);

  if (crc16(buf, payloadLen) != h.crc) return false;

  memcpy(p->rawMutable(), buf, payloadLen);
  return true;
}

#endif  // FW_MCU_ESP32
```

- [ ] **Step 4: Note on verification**

This file cannot be compile-checked until the `esp32_wroom32` environment exists (Task 5) — there
is no ESP32 toolchain wired up yet. Its correctness at this point rests on being a structural
match to the already-working `storage.cpp` (same `Header`, same CRC/fingerprint/magic scheme, same
read-back-after-write check), swapping only the STM32 buffered-byte calls for their ESP32
`EEPROM.h` equivalents. Task 5's compile is where this gets its first real check.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/storage.cpp firmware/src/storage_esp32.cpp
git commit -m "$(cat <<'EOF'
feat(storage): add ESP32-native FlashStore body

storage.cpp's STM32 buffered-EEPROM calls have no ESP32 equivalent.
storage_esp32.cpp implements the same FlashStore class (same header/CRC/
fingerprint format) against the ESP32 core's own EEPROM.h emulation.
Each guards its own body with #if FW_MCU_ESP32 so exactly one compiles
per environment.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: System telemetry — split `system_driver.cpp` / add `system_esp32_driver.cpp`

**Files:**
- Modify: `firmware/src/hardware/system/system_driver.cpp`
- Create: `firmware/src/hardware/system/system_esp32_driver.cpp`

**Interfaces:**
- Consumes: `sys::SystemDriver` (declared `firmware/src/hardware/system/system_driver.h`,
  unchanged) — `void readTelemetry(core::TlmValue*) override`, no fields, no other methods.
- Consumes: `T_UP`, `T_CLK`, `T_RAM`, `T_TEMP`, `T_VDD` (declared
  `firmware/src/hardware/system/system_params.h`, unchanged).
- Consumes: `FW_MCU_ESP32` from Task 1.

- [ ] **Step 1: Guard `system_driver.cpp`'s body**

Replace the top of `firmware/src/hardware/system/system_driver.cpp`:

```cpp
#include <Arduino.h>
#include "hardware/system/system_driver.h"

namespace sys {
```

with:

```cpp
#include "hardware/system/system_driver.h"

// STM32-side body -- see system_esp32_driver.cpp for the ESP32 counterpart,
// and wifi_driver.cpp's own comment for why each architecture-specific file
// guards its own body rather than relying on a per-environment
// build_src_filter.
#if !FW_MCU_ESP32

#include <Arduino.h>

namespace sys {
```

And at the end of the file, replace:

```cpp
void SystemDriver::readTelemetry(core::TlmValue* out) {
  int32_t vdd = readVddaMv();
  out[T_UP].u   = millis();
  out[T_CLK].u  = SystemCoreClock / 1000000UL;
  out[T_RAM].i  = freeRamBytes();
  out[T_TEMP].f = readTempC(vdd);
  out[T_VDD].i  = vdd;
}

}  // namespace sys
```

with:

```cpp
void SystemDriver::readTelemetry(core::TlmValue* out) {
  int32_t vdd = readVddaMv();
  out[T_UP].u   = millis();
  out[T_CLK].u  = SystemCoreClock / 1000000UL;
  out[T_RAM].i  = freeRamBytes();
  out[T_TEMP].f = readTempC(vdd);
  out[T_VDD].i  = vdd;
}

}  // namespace sys

#endif  // !FW_MCU_ESP32
```

- [ ] **Step 2: Regression-check the STM32 envs**

Run: `~/.platformio/penv/bin/pio run -e blackpill_f411ce`
Run: `~/.platformio/penv/bin/pio run -e blackpill_f401ce`
Expected: both SUCCESS.

- [ ] **Step 3: Write `system_esp32_driver.cpp`**

Create `firmware/src/hardware/system/system_esp32_driver.cpp`:

```cpp
#include "hardware/system/system_driver.h"

#if FW_MCU_ESP32

#include <Arduino.h>

namespace sys {

// Order must match kTlm in system_params.cpp. temp/vdd are stubbed to 0:
// the classic WROOM-32's internal temperature sensor is undocumented on
// original silicon (temperatureRead() is unofficial and chip-revision
// dependent) and there is no VDD reading to take here -- ESP32 runs its
// logic from a fixed onboard 3.3V regulator, not a measurable rail the way
// the STM32 boards' VREFINT trick reads.
void SystemDriver::readTelemetry(core::TlmValue* out) {
  out[T_UP].u   = millis();
  out[T_CLK].u  = (uint32_t)getCpuFrequencyMhz();
  out[T_RAM].i  = (int32_t)ESP.getFreeHeap();
  out[T_TEMP].f = 0.0f;
  out[T_VDD].i  = 0;
}

}  // namespace sys

#endif  // FW_MCU_ESP32
```

- [ ] **Step 4: Note on verification**

As with `storage_esp32.cpp`, this cannot be compile-checked until the `esp32_wroom32` environment
exists in Task 5. `getCpuFrequencyMhz()` and `ESP.getFreeHeap()` are standard ESP32 Arduino core
functions (`esp32-hal-cpu.h` / `Esp.h`, pulled in transitively by `Arduino.h`).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/hardware/system/system_driver.cpp firmware/src/hardware/system/system_esp32_driver.cpp
git commit -m "$(cat <<'EOF'
feat(system): add ESP32-native SystemDriver telemetry body

system_driver.cpp reads STM32F411 factory calibration addresses
directly, meaningless on ESP32 silicon. system_esp32_driver.cpp
implements the same SystemDriver class with real uptime/clock/RAM and
temp/vdd stubbed to 0 (no equivalent reading on this chip). Each
guards its own body with #if FW_MCU_ESP32.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: WiFi — add `wifi_esp32_driver.{h,cpp}` and wire it into `modules.cpp`

**Files:**
- Create: `firmware/src/hardware/wifi/wifi_esp32_driver.h`
- Create: `firmware/src/hardware/wifi/wifi_esp32_driver.cpp`
- Modify: `firmware/src/modules.cpp`

**Interfaces:**
- Consumes: `wifi::P_SSID`, `wifi::P_PASSWORD`, `wifi::T_STATUS`, `wifi::T_RSSI`, `wifi::T_IP`,
  `wifi::STATUS_OFF/CONNECTING/CONNECTED/FAILED` (declared `firmware/src/hardware/wifi/wifi_params.h`,
  unchanged).
- Consumes: `core::Module` (`attach`/`begin`/`tick`/`onParamChanged`/`readTelemetry`/`pollPush`,
  all virtual with do-nothing defaults except where overridden — `firmware/src/core/module.h`) and
  `core::WifiScanner` (`bool startScan() override` — `firmware/src/core/dispatch.h`).
- Consumes: `core::kMaxStrLen` (31, `firmware/src/core/types.h`).
- Consumes: `FW_MCU_ESP32`, `FEATURE_WIFI` from Task 1 / existing `config.h`.
- Produces: `wifi::WifiEsp32Driver` — same public interface as `wifi::WifiDriver`
  (`firmware/src/hardware/wifi/wifi_driver.h`), instantiable as a `core::Module` +
  `core::WifiScanner`.

- [ ] **Step 1: Write `wifi_esp32_driver.h`**

Create `firmware/src/hardware/wifi/wifi_esp32_driver.h`:

```cpp
#pragma once
#include "core/module.h"
#include "core/dispatch.h"
#include "hardware/wifi/wifi_params.h"

namespace wifi {

constexpr uint8_t kMaxScanResults = 10;

// One scanned network. Deliberately not proto_at.h's ScanResult: that struct
// lives in the AT-protocol file this driver has nothing to do with, and
// duplicating two fields here keeps this file buildable with zero
// dependency on the STM32/AT-only driver.
struct EspScanResult {
  char    ssid[33];   // 32 bytes is the WiFi spec's own SSID ceiling
  int16_t rssi;
};

// Onboard-radio counterpart to wifi::WifiDriver: same core::Module +
// core::WifiScanner interface, same wifi_params.h descriptor (ssid/password
// params, status/rssi/ip telemetry), but driven through the ESP32 Arduino
// core's own WiFi.h instead of AT commands over UART -- there is no
// external module to talk to, the radio is on this chip.
class WifiEsp32Driver : public core::Module, public core::WifiScanner {
 public:
  void attach(const core::Registry& reg, const core::Params& p) override;
  void begin() override;
  void tick(uint32_t nowMs) override;
  void onParamChanged(uint8_t local, const core::Params& p) override;
  void readTelemetry(core::TlmValue* out) override;
  size_t pollPush(char* out, size_t cap) override;

  // core::WifiScanner
  bool startScan() override;

 private:
  void beginJoin();

  char ssid_[core::kMaxStrLen + 1]     = {0};
  char password_[core::kMaxStrLen + 1] = {0};

  int32_t  status_   = STATUS_OFF;
  int16_t  rssi_     = 0;
  uint32_t ip_       = 0;
  uint32_t failedAt_ = 0;

  bool          scanning_        = false;
  bool          scanResultReady_ = false;
  EspScanResult scanResults_[kMaxScanResults];
  uint8_t       scanCount_ = 0;
};

}  // namespace wifi
```

- [ ] **Step 2: Write `wifi_esp32_driver.cpp`**

Create `firmware/src/hardware/wifi/wifi_esp32_driver.cpp`:

```cpp
#include "hardware/wifi/wifi_esp32_driver.h"

// Onboard-ESP32 counterpart to wifi_driver.cpp -- see that file's own header
// comment for why each is guarded to compile to nothing on the other
// architecture rather than being excluded by a build_src_filter.
#if FEATURE_WIFI && FW_MCU_ESP32

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <string.h>

// Backoff before retrying a failed join, matching WIFI_FAIL_BACKOFF_MS's
// default on the STM32/AT driver. Not board-header-overridable here: unlike
// that driver, nothing about this timing depends on board wiring.
static const uint32_t kFailBackoffMs = 5000;

namespace wifi {

void WifiEsp32Driver::attach(const core::Registry& reg, const core::Params& p) {
  (void)reg;
  strncpy(ssid_,     p.str(globalParam(P_SSID)),     sizeof(ssid_) - 1);
  strncpy(password_, p.str(globalParam(P_PASSWORD)), sizeof(password_) - 1);
}

void WifiEsp32Driver::beginJoin() {
  WiFi.begin(ssid_, password_);
  status_ = STATUS_CONNECTING;
}

void WifiEsp32Driver::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (ssid_[0] != '\0') beginJoin();
}

void WifiEsp32Driver::onParamChanged(uint8_t local, const core::Params& p) {
  switch (local) {
    case P_SSID:
      strncpy(ssid_, p.str(globalParam(P_SSID)), sizeof(ssid_) - 1);
      ssid_[sizeof(ssid_) - 1] = '\0';
      break;
    case P_PASSWORD:
      strncpy(password_, p.str(globalParam(P_PASSWORD)), sizeof(password_) - 1);
      password_[sizeof(password_) - 1] = '\0';
      break;
    default:
      return;
  }
  // Either field changing invalidates whatever join is in flight or already
  // holds -- re-arm from Off exactly like the STM32/AT driver does.
  if (ssid_[0] == '\0') {
    WiFi.disconnect();
    status_ = STATUS_OFF;
    rssi_ = 0;
    ip_ = 0;
  } else {
    beginJoin();
  }
}

void WifiEsp32Driver::tick(uint32_t nowMs) {
  if (scanning_) {
    // WiFi.scanComplete()'s contract: -1 (WIFI_SCAN_RUNNING) while still
    // scanning, -2 (WIFI_SCAN_FAILED) if the scan could not start or
    // errored, otherwise the network count.
    int16_t n = WiFi.scanComplete();
    if (n >= 0) {
      scanCount_ = (uint8_t)(n > kMaxScanResults ? kMaxScanResults : n);
      for (uint8_t i = 0; i < scanCount_; ++i) {
        strncpy(scanResults_[i].ssid, WiFi.SSID(i).c_str(), sizeof(scanResults_[i].ssid) - 1);
        scanResults_[i].ssid[sizeof(scanResults_[i].ssid) - 1] = '\0';
        scanResults_[i].rssi = (int16_t)WiFi.RSSI(i);
      }
      WiFi.scanDelete();
      scanning_ = false;
      scanResultReady_ = true;
    } else if (n == -2) {
      WiFi.scanDelete();
      scanCount_ = 0;
      scanning_ = false;
      scanResultReady_ = true;   // report an empty result rather than wedge
    }
  }

  wl_status_t s = WiFi.status();
  switch (status_) {
    case STATUS_CONNECTING:
      if (s == WL_CONNECTED) {
        status_ = STATUS_CONNECTED;
      } else if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL) {
        status_ = STATUS_FAILED;
        failedAt_ = nowMs;
      }
      break;
    case STATUS_CONNECTED:
      if (s != WL_CONNECTED) {
        status_ = STATUS_CONNECTING;   // WiFi.setAutoReconnect(true) is already retrying
      } else {
        rssi_ = (int16_t)WiFi.RSSI();
        IPAddress ip = WiFi.localIP();
        ip_ = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) |
              ((uint32_t)ip[2] << 8)  |  (uint32_t)ip[3];
      }
      break;
    case STATUS_FAILED:
      if (nowMs - failedAt_ > kFailBackoffMs && ssid_[0] != '\0') beginJoin();
      break;
    default:
      break;
  }
}

void WifiEsp32Driver::readTelemetry(core::TlmValue* out) {
  out[T_STATUS].u = (uint32_t)status_;
  out[T_RSSI].i   = rssi_;
  out[T_IP].u     = ip_;
}

bool WifiEsp32Driver::startScan() {
  if (scanning_) return false;
  scanning_ = true;
  scanResultReady_ = false;
  WiFi.scanNetworks(true);   // async
  return true;
}

size_t WifiEsp32Driver::pollPush(char* out, size_t cap) {
  if (!scanResultReady_) return 0;
  scanResultReady_ = false;

  JsonDocument doc;
  JsonArray arr = doc["scan"].to<JsonArray>();
  for (uint8_t i = 0; i < scanCount_; ++i) {
    JsonObject e = arr.add<JsonObject>();
    e["ssid"] = scanResults_[i].ssid;
    e["rssi"] = scanResults_[i].rssi;
  }
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

}  // namespace wifi

#endif  // FEATURE_WIFI && FW_MCU_ESP32
```

- [ ] **Step 3: Wire it into `modules.cpp`**

In `firmware/src/modules.cpp`, replace:

```cpp
#if FEATURE_WIFI
#  include "hardware/wifi/wifi_params.h"
#  if FW_TARGET_ARDUINO
#    include "hardware/wifi/wifi_driver.h"
     static wifi::WifiDriver g_wifi;
#    define WIFI_DRV (&g_wifi)
#  else
#    define WIFI_DRV nullptr
#  endif
#endif
```

with:

```cpp
#if FEATURE_WIFI
#  include "hardware/wifi/wifi_params.h"
#  if FW_TARGET_ARDUINO
#    if FW_MCU_ESP32
#      include "hardware/wifi/wifi_esp32_driver.h"
       static wifi::WifiEsp32Driver g_wifi;
#    else
#      include "hardware/wifi/wifi_driver.h"
       static wifi::WifiDriver g_wifi;
#    endif
#    define WIFI_DRV (&g_wifi)
#  else
#    define WIFI_DRV nullptr
#  endif
#endif
```

(`wifiScanner()` further down in the same file already does `return &g_wifi;` under `#if
FEATURE_WIFI && FW_TARGET_ARDUINO` — no change needed there, since both `WifiDriver` and
`WifiEsp32Driver` derive from `core::WifiScanner` and `&g_wifi` resolves to whichever one is
actually declared above.)

- [ ] **Step 4: Regression-check the STM32 envs and native suite**

Run: `~/.platformio/penv/bin/pio run -e blackpill_f411ce`
Run: `~/.platformio/penv/bin/pio run -e blackpill_f401ce`
Run: `~/.platformio/penv/bin/pio test -e native`
Expected: all SUCCESS/PASS — `modules.cpp` is filtered into the native build too
(`platformio.ini`'s `build_src_filter` includes `+<modules.cpp>`), and `FW_MCU_ESP32` defaults to
0 there same as everywhere else, so this new branch is never taken outside the ESP32 env.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/hardware/wifi/wifi_esp32_driver.h firmware/src/hardware/wifi/wifi_esp32_driver.cpp firmware/src/modules.cpp
git commit -m "$(cat <<'EOF'
feat(wifi): add ESP32-native WifiEsp32Driver, wire into modules.cpp

wifi_driver.cpp talks AT commands over UART to an external ESP-01 --
meaningless for an onboard ESP32 radio. wifi_esp32_driver.cpp drives
WiFi.h directly against the same wifi_params.h descriptor (identical
schema, only the driver differs). modules.cpp's wifi wiring block picks
between the two on FW_MCU_ESP32.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Board header, PlatformIO environment, and first real ESP32 compile

**Files:**
- Create: `firmware/include/boards/esp32_wroom32.h`
- Modify: `firmware/platformio.ini`

**Interfaces:**
- Consumes: everything produced by Tasks 1-4 (`FW_MCU_ESP32`, the guarded/split `storage`,
  `system`, `wifi` files).
- Produces: `[env:esp32_wroom32]`, a working PlatformIO environment.

- [ ] **Step 1: Write the board header**

Create `firmware/include/boards/esp32_wroom32.h`:

```cpp
#pragma once
// Generic ESP32 WROOM-32 dev board (clone), no external modules wired up.
// Onboard WiFi radio only -- see hardware/wifi/wifi_esp32_driver.cpp, not
// the AT-command driver the STM32 boards use to talk to an external ESP-01.
//
// Selected at compile time by platformio.ini:
//   -D BOARD_HEADER='"boards/esp32_wroom32.h"'
//   -D FW_MCU_ESP32=1
//
// Flashed over the board's own USB-UART bridge with esptool, never an
// ST-Link -- see [env:esp32_wroom32] in platformio.ini.

#define BOARD_ID "esp32_wroom32"

// --- features ---------------------------------------------------------------
#define FEATURE_LED     1
#define FEATURE_BUTTON  0
#define FEATURE_ST7789_240X240 0
#define FEATURE_SERVO   0
#define FEATURE_ESC     0
#define FEATURE_RX      0
#define FEATURE_WIFI    1
// No STM32-style ROM DFU on this part; esptool over USB is the flash path
// instead. dfu.cpp's existing FEATURE_DFU-off stub (already exercised by
// any board with DFU off) covers this with no new code.
#define FEATURE_DFU     0

// --- pin map ----------------------------------------------------------------
// GPIO2 is the onboard LED on most WROOM-32 devkit clones (silkscreened
// "LED" or "D2" next to it) -- verify against your specific board once
// flashed; cheaper clones sometimes omit it or use a different pin.
#define LED_PIN         2
#define LED_ACTIVE_LOW  0

// No WIFI_RX_PIN/WIFI_TX_PIN/WIFI_BAUD here: those belong to the STM32
// AT-UART driver only. wifi_esp32_driver.cpp drives the radio in-chip
// through WiFi.h and needs no UART pins.
```

- [ ] **Step 2: Add the PlatformIO environment**

In `firmware/platformio.ini`, insert a new `[env:esp32_wroom32]` block between `[env:blackpill_f401ce]`
and `[env:native]`:

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
monitor_speed = 115200          ; must match FW_SERIAL_BAUD in include/config.h
; Without this, editing a board header does NOT trigger a rebuild -- same
; reason the other two envs carry it, see the note on [env:blackpill_f411ce].
extra_scripts = pre:scripts/config_hash.py
lib_deps =
    bblanchon/ArduinoJson@^7.0.4
; No upload_protocol/debug_tool: esptool over the board's own USB-UART
; bridge is the espressif32 platform's default, and there is no ST-Link
; involved for this board at all.
```

- [ ] **Step 3: First real compile**

Run: `~/.platformio/penv/bin/pio run -e esp32_wroom32`

This is the first time any ESP32-specific code in this plan actually gets compiled — PlatformIO
will also download the `espressif32` platform/toolchain on first run if it isn't cached already,
which can take several minutes and needs internet access.

Expected: SUCCESS. If it fails, the error will point at exactly one of the three split files
(`storage_esp32.cpp`, `system_esp32_driver.cpp`, `wifi_esp32_driver.cpp`) or the board header —
fix the specific line the compiler names (most likely culprits: an ESP32 core API signature this
plan assumed but a newer/older `espressif32` platform version changed, e.g.
`getCpuFrequencyMhz()`'s return type, or `EEPROM.commit()`'s signature) and re-run.

- [ ] **Step 4: Regression-check everything else once more**

Run: `~/.platformio/penv/bin/pio run -e blackpill_f411ce`
Run: `~/.platformio/penv/bin/pio run -e blackpill_f401ce`
Run: `~/.platformio/penv/bin/pio test -e native`
Expected: all still SUCCESS/PASS.

- [ ] **Step 5: Commit**

```bash
git add firmware/include/boards/esp32_wroom32.h firmware/platformio.ini
git commit -m "$(cat <<'EOF'
feat(hardware): add esp32_wroom32 firmware target

No external modules -- onboard LED and onboard WiFi radio only,
flashed over USB via esptool (platform = espressif32, board =
esp32dev). Built on the storage/system/wifi ESP32 driver split from
the previous three commits.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Flash the real board and verify

The user has an ESP32 WROOM clone plugged in over USB already. This task flashes it and confirms
the whole stack — firmware, backend, web UI — actually works end to end, per the project's own
"flash and measure" convention for hardware (no automated UI/hardware test suite exists by
design).

**Files:** none — this task runs commands and drives the app, no source changes.

- [ ] **Step 1: Find the board's serial port**

Run: `~/.platformio/penv/bin/pio device list`

Note the ESP32's port (typically `/dev/ttyUSB0` for a CP2102 bridge or `/dev/ttyACM0`/another
`/dev/ttyUSB*` for CH340 — distinguishable from any other connected serial device by description).

- [ ] **Step 2: Flash**

Run: `cd firmware && ~/.platformio/penv/bin/pio run -e esp32_wroom32 -t upload`

esptool auto-detects the port and resets the board into its bootloader via DTR/RTS toggling
through the onboard USB-UART bridge. If it hangs at `Connecting....`, hold the board's BOOT/IO0
button until it starts writing (`Writing at 0x...`), then release. If the wrong port gets picked
(unlikely with only one board attached), add `upload_port = <device>` to `[env:esp32_wroom32]` in
`platformio.ini` using the port from Step 1.

- [ ] **Step 3: Watch the boot log**

Run: `~/.platformio/penv/bin/pio device monitor -b 115200`

Expected: a line like `boot: silkscreen 1.0.0 (esp32_wroom32) built ...`, then `boot:
settings=defaults` (first boot, nothing saved yet) and `boot: modules=4 params=... tlm=... ram=...`
(`device`, `system`, `led`, `wifi` — four modules). Ctrl-C to exit the monitor before the next step
(only one process may hold the serial port at a time).

- [ ] **Step 4: Connect through the app**

Run: `cd ../app && .venv/bin/uvicorn backend.main:app --port 8080`

Open `http://localhost:8080` in a browser, connect to the ESP32's port from Step 1. Confirm:
- The device page shows exactly the LED, WiFi, and System (uptime/clock/RAM/temp/VDD) sections —
  no Servo/ESC/RX/Display sections, matching this board's `FEATURE_*` set.
- Temp and VDD read `0` (expected — stubbed per this plan's design).

- [ ] **Step 5: Verify WiFi**

From the WiFi section: enter a real network's SSID/password, save. Confirm `wifi.status` moves
`connecting` → `connected` within a few seconds, `wifi.rssi` shows a plausible dBm value, and
`wifi.ip` shows a real dotted-quad address matching what the network's router assigns. Run a scan;
confirm the results list includes networks known to be in range.

- [ ] **Step 6: Verify LED**

Toggle the LED on/off from the UI. Confirm the board's onboard LED responds. If it doesn't, the
clone likely uses a different GPIO than 2 — check the board's silkscreen/schematic, update
`LED_PIN` in `firmware/include/boards/esp32_wroom32.h`, and re-flash (Step 2).

- [ ] **Step 7: Verify persistence**

With WiFi still configured from Step 5, unplug and replug the ESP32's USB cable (a real power
cycle, not just a soft reset). Reconnect through the app. Confirm the WiFi section still shows the
saved SSID and reconnects on its own, and `pio device monitor`'s boot log now says `boot:
settings=restored` instead of `defaults` — this is what exercises `storage_esp32.cpp`'s
`EEPROM.commit()`/read-back path for real.

- [ ] **Step 8: Report results**

No commit for this task (no source changes) — report back what was confirmed working, and file
anything that didn't match (e.g., a different LED pin, a scan result oddity) as a follow-up rather
than silently patching around it, since Task 5's compile-only verification couldn't have caught a
runtime-only issue.
