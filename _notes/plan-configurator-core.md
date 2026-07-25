# Configurator Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build STM32 firmware exposing a JSON-lines config/telemetry protocol over USB serial, plus a Python backend and Bootstrap web UI that reads live state and changes settings.

**Architecture:** The firmware splits into a pure-C++ `core/` (no Arduino, unit-tested natively) and a thin Arduino glue layer, communicating through `HardwareSink` and `Persistence` interfaces. A Python backend owns the device model — id correlation, timeouts, schema cache — and exposes REST + WebSocket. The web UI is static and generates its config form from the device's own schema.

**Tech Stack:** PlatformIO, Arduino STM32 core, ArduinoJson 7, Unity (native tests), Python 3, FastAPI, pyserial, pytest, Bootstrap 5.

**Spec:** `_notes/spec-configurator-core.md` — read it before starting. **Progress tracking:** `_notes/progress.md` — update at the end of every session.

## Global Constraints

- **`pio` is NOT on `PATH`.** Every PlatformIO command in this plan uses `~/.platformio/penv/bin/pio`.
- **Do NOT add udev rules, `dialout` group setup, or any permissions script.** Already handled system-wide: PlatformIO's vendor-wide `0483` rule covers VCP/ST-Link/DFU and tells ModemManager to ignore them. `/dev/ttyACM0` is already `crw-rw-rw-`.
- **Python is 3.14.4.** Verify wheels for FastAPI/uvicorn/pydantic install before writing backend code (Task 7 step 1). Fall back to 3.12 if they lag.
- **Board:** Black Pill STM32F411CE, `blackpill_f411ce`, upload via ST-Link over SWD. **Two ST-Link/V2 units are attached** — if upload picks the wrong one, set `upload_port`.
- **LED is `PC13` and ACTIVE-LOW** (LOW = on). It has **no timer channel — no hardware PWM**.
- **Button is `PA0`, `INPUT_PULLUP`.**
- **Protocol version is `1`.** Firmware reports it in `hello`; backend refuses mismatches.
- **Buffers: 256 bytes inbound, 1024 bytes outbound.** The schema response (~450 bytes) is the largest message — size ArduinoJson documents for it, not for telemetry.
- **Out-of-bounds values are rejected, never coerced.** No silent truncation or clamping.
- **`app/web/` must contain nothing Python-specific.** No Jinja, no server-side rendering. All backend access goes through the single `Api` object in `app.js`.
- **Commit after every task.** Conventional commit prefixes (`feat:`, `test:`, `chore:`).

## Decomposition note

The spec names two core units (`protocol`, `params`). This plan splits the request-handling logic into a third, `dispatch`, so that JSON encoding, parameter storage, and the decision logic tying them together can each be tested alone. `dispatch` is where `HardwareSink` lives — plus a matching `Persistence` interface, applying the spec's seam principle to flash writes so `save` is testable natively too. This is finer decomposition within the spec's architecture, not a change to it.

## File Structure

```
firmware/
  platformio.ini              native + blackpill envs
  src/core/                   pure C++, NO Arduino — all natively tested
    types.h                   ParamId, ParamType, SetResult, Value, limits
    params.h/.cpp             static registry table + value storage + validation
    protocol.h/.cpp           LineReader (framing) + parseRequest
    dispatch.h/.cpp           Request -> Response; HardwareSink, Persistence seams
  src/
    main.cpp                  Arduino glue: USB CDC, main loop, wiring
    hardware.h/.cpp           LED driver + telemetry readers (ported from test1)
    storage.h/.cpp            Persistence impl over flash-emulated EEPROM
  test/
    test_params/test_params.cpp
    test_protocol/test_protocol.cpp
    test_dispatch/test_dispatch.cpp

app/
  requirements.txt
  backend/
    protocol.py               JSON-lines codec
    link.py                   SerialLink: thread, correlation, timeout, reconnect
    device.py                 DeviceModel: schema cache, values, state machine
    main.py                   FastAPI routes + WebSocket
  web/
    index.html
    app.js                    Api module + UI rendering
    vendor/bootstrap.min.css
  tests/
    fake_serial.py            scripted fake port — the key test double
    test_protocol.py
    test_link.py
    test_device.py
    test_api.py

docs/api.md                   HTTP/WS contract (the Electron port contract)
```

---

## Task 1: Firmware scaffold and native test harness

Nothing downstream can be tested until `pio test -e native` works. This task proves the harness.

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/src/core/types.h`
- Create: `firmware/test/test_harness/test_harness.cpp`

**Interfaces:**
- Consumes: nothing
- Produces: `core::ParamId`, `core::ParamType`, `core::SetResult`, `core::kMaxStrLen`, `core::kMaxLineIn`, `core::kMaxLineOut`, `core::kProtoVersion` — used by every later firmware task.

- [ ] **Step 1: Create `firmware/platformio.ini`**

`lib_compat_mode = off` is required or PlatformIO refuses to build ArduinoJson for the native platform.

```ini
[env:blackpill_f411ce]
platform = ststm32
board = blackpill_f411ce
framework = arduino
upload_protocol = stlink
debug_tool = stlink
build_flags =
    -DUSBCON
    -DUSBD_USE_CDC
monitor_speed = 115200
lib_deps =
    bblanchon/ArduinoJson@^7.0.4

[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -I src
lib_deps =
    bblanchon/ArduinoJson@^7.0.4
lib_compat_mode = off
```

- [ ] **Step 2: Create `firmware/src/core/types.h`**

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace core {

constexpr size_t   kMaxStrLen   = 31;    // device.name max chars
constexpr size_t   kMaxLineIn   = 256;   // inbound line budget
constexpr size_t   kMaxLineOut  = 1024;  // outbound: schema is ~450 bytes
constexpr uint16_t kProtoVersion = 1;

enum ParamId : uint8_t {
  PARAM_LED_MODE = 0,
  PARAM_LED_BLINK_HZ,
  PARAM_DEVICE_NAME,
  PARAM_TLM_RATE,
  PARAM_COUNT
};

enum class ParamType { U8, Str, Enum };

enum class SetResult { Ok, NoKey, Range, BadEnum, TooLong, WrongType };

// One value slot. 36 bytes x 4 params is irrelevant on a 128KB part, and a
// flat struct is far simpler than a variant.
struct Value {
  int32_t num;
  char    str[kMaxStrLen + 1];
};

}  // namespace core
```

- [ ] **Step 3: Write the harness test**

Create `firmware/test/test_harness/test_harness.cpp`:

```cpp
#include <unity.h>
#include "core/types.h"

void test_constants_are_sane() {
  TEST_ASSERT_EQUAL(4, core::PARAM_COUNT);
  TEST_ASSERT_EQUAL(31, core::kMaxStrLen);
  TEST_ASSERT_EQUAL(256, core::kMaxLineIn);
  TEST_ASSERT_EQUAL(1024, core::kMaxLineOut);
  TEST_ASSERT_EQUAL(1, core::kProtoVersion);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_constants_are_sane);
  return UNITY_END();
}
```

- [ ] **Step 4: Run the test**

Run: `cd firmware && ~/.platformio/penv/bin/pio test -e native`
Expected: PASS, 1 test. If ArduinoJson fails to build, confirm `lib_compat_mode = off` is present.

- [ ] **Step 5: Commit**

```bash
git add firmware/
git commit -m "chore: firmware scaffold with native test harness"
```

---

## Task 2: Parameter registry (`core/params`)

The single source of truth. The static table drives validation, the schema, and the UI.

**Files:**
- Create: `firmware/src/core/params.h`, `firmware/src/core/params.cpp`
- Test: `firmware/test/test_params/test_params.cpp`

**Interfaces:**
- Consumes: `core/types.h` from Task 1.
- Produces:
  - `const ParamDef* core::defs()` — array of `PARAM_COUNT` entries
  - `bool core::findParam(const char* key, ParamId* out)`
  - `class core::Params` with `loadDefaults()`, `SetResult setNum(ParamId, int32_t)`, `SetResult setStr(ParamId, const char*)`, `int32_t num(ParamId) const`, `const char* str(ParamId) const`
  - `str()` returns the option **name** for Enum params, not the index.

- [ ] **Step 1: Write the failing tests**

Create `firmware/test/test_params/test_params.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "core/params.h"

using namespace core;

void test_defaults_match_spec() {
  Params p;
  TEST_ASSERT_EQUAL_STRING("blink", p.str(PARAM_LED_MODE));
  TEST_ASSERT_EQUAL_INT32(2, p.num(PARAM_LED_BLINK_HZ));
  TEST_ASSERT_EQUAL_STRING("app-demo", p.str(PARAM_DEVICE_NAME));
  TEST_ASSERT_EQUAL_INT32(10, p.num(PARAM_TLM_RATE));
}

void test_find_param_by_key() {
  ParamId id;
  TEST_ASSERT_TRUE(findParam("led.blink_hz", &id));
  TEST_ASSERT_EQUAL(PARAM_LED_BLINK_HZ, id);
  TEST_ASSERT_FALSE(findParam("nope.missing", &id));
}

void test_numeric_range_rejected_not_clamped() {
  Params p;
  TEST_ASSERT_EQUAL(SetResult::Range, p.setNum(PARAM_LED_BLINK_HZ, 21));
  TEST_ASSERT_EQUAL(SetResult::Range, p.setNum(PARAM_LED_BLINK_HZ, 0));
  // value must be untouched after rejection
  TEST_ASSERT_EQUAL_INT32(2, p.num(PARAM_LED_BLINK_HZ));
  TEST_ASSERT_EQUAL(SetResult::Ok, p.setNum(PARAM_LED_BLINK_HZ, 20));
  TEST_ASSERT_EQUAL_INT32(20, p.num(PARAM_LED_BLINK_HZ));
}

void test_enum_set_by_name() {
  Params p;
  TEST_ASSERT_EQUAL(SetResult::Ok, p.setStr(PARAM_LED_MODE, "off"));
  TEST_ASSERT_EQUAL_STRING("off", p.str(PARAM_LED_MODE));
  TEST_ASSERT_EQUAL_INT32(0, p.num(PARAM_LED_MODE));  // stored as index
  TEST_ASSERT_EQUAL(SetResult::BadEnum, p.setStr(PARAM_LED_MODE, "purple"));
  TEST_ASSERT_EQUAL_STRING("off", p.str(PARAM_LED_MODE));
}

void test_string_too_long_rejected_not_truncated() {
  Params p;
  char long_name[64];
  memset(long_name, 'x', sizeof(long_name));
  long_name[40] = '\0';
  TEST_ASSERT_EQUAL(SetResult::TooLong, p.setStr(PARAM_DEVICE_NAME, long_name));
  TEST_ASSERT_EQUAL_STRING("app-demo", p.str(PARAM_DEVICE_NAME));

  char exact[kMaxStrLen + 1];
  memset(exact, 'y', kMaxStrLen);
  exact[kMaxStrLen] = '\0';
  TEST_ASSERT_EQUAL(SetResult::Ok, p.setStr(PARAM_DEVICE_NAME, exact));
  TEST_ASSERT_EQUAL_STRING(exact, p.str(PARAM_DEVICE_NAME));
}

void test_wrong_type_rejected() {
  Params p;
  TEST_ASSERT_EQUAL(SetResult::WrongType, p.setNum(PARAM_DEVICE_NAME, 5));
  TEST_ASSERT_EQUAL(SetResult::WrongType, p.setStr(PARAM_TLM_RATE, "fast"));
}

void test_load_defaults_restores_after_changes() {
  Params p;
  p.setNum(PARAM_LED_BLINK_HZ, 15);
  p.setStr(PARAM_DEVICE_NAME, "changed");
  p.loadDefaults();
  TEST_ASSERT_EQUAL_INT32(2, p.num(PARAM_LED_BLINK_HZ));
  TEST_ASSERT_EQUAL_STRING("app-demo", p.str(PARAM_DEVICE_NAME));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_match_spec);
  RUN_TEST(test_find_param_by_key);
  RUN_TEST(test_numeric_range_rejected_not_clamped);
  RUN_TEST(test_enum_set_by_name);
  RUN_TEST(test_string_too_long_rejected_not_truncated);
  RUN_TEST(test_wrong_type_rejected);
  RUN_TEST(test_load_defaults_restores_after_changes);
  return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && ~/.platformio/penv/bin/pio test -e native -f test_params`
Expected: FAIL — `core/params.h: No such file or directory`

- [ ] **Step 3: Write `firmware/src/core/params.h`**

```cpp
#pragma once
#include "core/types.h"

namespace core {

struct ParamDef {
  const char* key;
  ParamType   type;
  const char* label;
  const char* unit;          // nullptr when unitless
  int32_t     minVal;        // U8 only
  int32_t     maxVal;        // U8 only
  const char* const* options;  // Enum only
  uint8_t     optionCount;    // Enum only
  size_t      maxLen;         // Str only
  int32_t     defNum;         // default for U8 / Enum index
  const char* defStr;         // default for Str, nullptr otherwise
};

const ParamDef* defs();
bool findParam(const char* key, ParamId* out);

class Params {
 public:
  Params() { loadDefaults(); }
  void loadDefaults();

  SetResult setNum(ParamId id, int32_t v);
  SetResult setStr(ParamId id, const char* s);

  int32_t     num(ParamId id) const { return v_[id].num; }
  const char* str(ParamId id) const;

  // Raw access for persistence — copies all value slots.
  const Value* raw() const { return v_; }
  Value*       rawMutable() { return v_; }

 private:
  Value v_[PARAM_COUNT];
};

}  // namespace core
```

- [ ] **Step 4: Write `firmware/src/core/params.cpp`**

```cpp
#include "core/params.h"
#include <string.h>

namespace core {

static const char* const kLedModes[] = {"off", "on", "blink"};

static const ParamDef kDefs[PARAM_COUNT] = {
  {"led.mode",     ParamType::Enum, "LED Mode",       nullptr,
   0, 0, kLedModes, 3, 0, 2, nullptr},
  {"led.blink_hz", ParamType::U8,   "Blink Rate",     "Hz",
   1, 20, nullptr, 0, 0, 2, nullptr},
  {"device.name",  ParamType::Str,  "Device Name",    nullptr,
   0, 0, nullptr, 0, kMaxStrLen, 0, "app-demo"},
  {"tlm.rate",     ParamType::U8,   "Telemetry Rate", "Hz",
   1, 50, nullptr, 0, 0, 10, nullptr},
};

const ParamDef* defs() { return kDefs; }

bool findParam(const char* key, ParamId* out) {
  for (uint8_t i = 0; i < PARAM_COUNT; ++i) {
    if (strcmp(kDefs[i].key, key) == 0) {
      *out = static_cast<ParamId>(i);
      return true;
    }
  }
  return false;
}

void Params::loadDefaults() {
  for (uint8_t i = 0; i < PARAM_COUNT; ++i) {
    v_[i].num = kDefs[i].defNum;
    v_[i].str[0] = '\0';
    if (kDefs[i].type == ParamType::Str && kDefs[i].defStr) {
      strncpy(v_[i].str, kDefs[i].defStr, kMaxStrLen);
      v_[i].str[kMaxStrLen] = '\0';
    }
  }
}

SetResult Params::setNum(ParamId id, int32_t val) {
  const ParamDef& d = kDefs[id];
  if (d.type != ParamType::U8) return SetResult::WrongType;
  if (val < d.minVal || val > d.maxVal) return SetResult::Range;
  v_[id].num = val;
  return SetResult::Ok;
}

SetResult Params::setStr(ParamId id, const char* s) {
  const ParamDef& d = kDefs[id];
  if (d.type == ParamType::Enum) {
    for (uint8_t i = 0; i < d.optionCount; ++i) {
      if (strcmp(d.options[i], s) == 0) {
        v_[id].num = i;
        return SetResult::Ok;
      }
    }
    return SetResult::BadEnum;
  }
  if (d.type != ParamType::Str) return SetResult::WrongType;
  if (strlen(s) > d.maxLen) return SetResult::TooLong;  // reject, never truncate
  strncpy(v_[id].str, s, kMaxStrLen);
  v_[id].str[kMaxStrLen] = '\0';
  return SetResult::Ok;
}

const char* Params::str(ParamId id) const {
  const ParamDef& d = kDefs[id];
  if (d.type == ParamType::Enum) {
    int32_t i = v_[id].num;
    if (i < 0 || i >= d.optionCount) return "";
    return d.options[i];
  }
  return v_[id].str;
}

}  // namespace core
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd firmware && ~/.platformio/penv/bin/pio test -e native -f test_params`
Expected: PASS, 7 tests.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/core/params.h firmware/src/core/params.cpp firmware/test/test_params/
git commit -m "feat: parameter registry with validation"
```

---

## Task 3: Line framing and request parsing (`core/protocol`)

**Files:**
- Create: `firmware/src/core/protocol.h`, `firmware/src/core/protocol.cpp`
- Test: `firmware/test/test_protocol/test_protocol.cpp`

**Interfaces:**
- Consumes: `core/types.h` (Task 1).
- Produces:
  - `class core::LineReader` with `bool feed(char c)`, `const char* line() const`, `bool overflowed() const`, `void reset()`
  - `enum class core::Op { Hello, Schema, Get, GetAll, Set, Save, Defaults, Tlm, Unknown }`
  - `struct core::Request` with fields `id`, `op`, `key[40]`, `hasNum`, `num`, `str[kMaxStrLen+1]`, `hasStr`, `tlmOn`, `ok`, `err`
  - `core::Request core::parseRequest(const char* line)`

- [ ] **Step 1: Write the failing tests**

Create `firmware/test/test_protocol/test_protocol.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "core/protocol.h"

using namespace core;

static bool feedAll(LineReader& r, const char* s) {
  bool done = false;
  for (const char* p = s; *p; ++p) done = r.feed(*p);
  return done;
}

void test_line_reader_assembles_a_line() {
  LineReader r;
  TEST_ASSERT_TRUE(feedAll(r, "{\"a\":1}\n"));
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", r.line());
  TEST_ASSERT_FALSE(r.overflowed());
}

void test_line_reader_strips_carriage_return() {
  LineReader r;
  TEST_ASSERT_TRUE(feedAll(r, "{\"a\":1}\r\n"));
  TEST_ASSERT_EQUAL_STRING("{\"a\":1}", r.line());
}

void test_oversized_line_flags_overflow_then_recovers() {
  LineReader r;
  char junk[kMaxLineIn + 50];
  memset(junk, 'x', sizeof(junk));
  junk[sizeof(junk) - 1] = '\0';
  TEST_ASSERT_FALSE(feedAll(r, junk));   // no newline yet
  TEST_ASSERT_TRUE(r.feed('\n'));        // line completes
  TEST_ASSERT_TRUE(r.overflowed());

  // must recover cleanly on the NEXT line — this is the wedge case
  TEST_ASSERT_TRUE(feedAll(r, "{\"id\":1,\"op\":\"hello\"}\n"));
  TEST_ASSERT_FALSE(r.overflowed());
  TEST_ASSERT_EQUAL_STRING("{\"id\":1,\"op\":\"hello\"}", r.line());
}

void test_parse_hello() {
  Request q = parseRequest("{\"id\":7,\"op\":\"hello\"}");
  TEST_ASSERT_TRUE(q.ok);
  TEST_ASSERT_EQUAL_UINT32(7, q.id);
  TEST_ASSERT_EQUAL(Op::Hello, q.op);
}

void test_parse_set_numeric() {
  Request q = parseRequest("{\"id\":3,\"op\":\"set\",\"key\":\"led.blink_hz\",\"val\":5}");
  TEST_ASSERT_TRUE(q.ok);
  TEST_ASSERT_EQUAL(Op::Set, q.op);
  TEST_ASSERT_EQUAL_STRING("led.blink_hz", q.key);
  TEST_ASSERT_TRUE(q.hasNum);
  TEST_ASSERT_FALSE(q.hasStr);
  TEST_ASSERT_EQUAL_INT32(5, q.num);
}

void test_parse_set_string() {
  Request q = parseRequest("{\"id\":4,\"op\":\"set\",\"key\":\"led.mode\",\"val\":\"off\"}");
  TEST_ASSERT_TRUE(q.ok);
  TEST_ASSERT_TRUE(q.hasStr);
  TEST_ASSERT_FALSE(q.hasNum);
  TEST_ASSERT_EQUAL_STRING("off", q.str);
}

void test_parse_tlm_carries_no_rate() {
  Request q = parseRequest("{\"id\":9,\"op\":\"tlm\",\"on\":true}");
  TEST_ASSERT_TRUE(q.ok);
  TEST_ASSERT_EQUAL(Op::Tlm, q.op);
  TEST_ASSERT_TRUE(q.tlmOn);
}

void test_malformed_json_rejected() {
  Request q = parseRequest("{not json at all");
  TEST_ASSERT_FALSE(q.ok);
  TEST_ASSERT_EQUAL_STRING("badjson", q.err);
}

void test_unknown_op_rejected() {
  Request q = parseRequest("{\"id\":1,\"op\":\"launch_missiles\"}");
  TEST_ASSERT_FALSE(q.ok);
  TEST_ASSERT_EQUAL_STRING("badop", q.err);
  TEST_ASSERT_EQUAL_UINT32(1, q.id);  // id preserved so we can still reply
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_line_reader_assembles_a_line);
  RUN_TEST(test_line_reader_strips_carriage_return);
  RUN_TEST(test_oversized_line_flags_overflow_then_recovers);
  RUN_TEST(test_parse_hello);
  RUN_TEST(test_parse_set_numeric);
  RUN_TEST(test_parse_set_string);
  RUN_TEST(test_parse_tlm_carries_no_rate);
  RUN_TEST(test_malformed_json_rejected);
  RUN_TEST(test_unknown_op_rejected);
  return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && ~/.platformio/penv/bin/pio test -e native -f test_protocol`
Expected: FAIL — `core/protocol.h: No such file or directory`

- [ ] **Step 3: Write `firmware/src/core/protocol.h`**

```cpp
#pragma once
#include "core/types.h"

namespace core {

enum class Op { Hello, Schema, Get, GetAll, Set, Save, Defaults, Tlm, Unknown };

struct Request {
  uint32_t id      = 0;
  Op       op      = Op::Unknown;
  char     key[40] = {0};
  bool     hasNum  = false;
  int32_t  num     = 0;
  bool     hasStr  = false;
  char     str[kMaxStrLen + 1] = {0};
  bool     tlmOn   = false;
  bool     ok      = false;
  const char* err  = nullptr;   // "badjson" | "badop" when !ok
};

// Assembles bytes into newline-terminated lines with overflow detection.
class LineReader {
 public:
  bool feed(char c);                       // true when a line completed
  const char* line() const { return buf_; }
  bool overflowed() const { return lineOverflowed_; }
  void reset();

 private:
  char   buf_[kMaxLineIn + 1] = {0};
  size_t len_ = 0;
  bool   dropping_ = false;        // current line already too long
  bool   lineOverflowed_ = false;  // the COMPLETED line was too long
};

Request parseRequest(const char* line);

}  // namespace core
```

- [ ] **Step 4: Write `firmware/src/core/protocol.cpp`**

```cpp
#include "core/protocol.h"
#include <ArduinoJson.h>
#include <string.h>

namespace core {

void LineReader::reset() {
  len_ = 0;
  buf_[0] = '\0';
  dropping_ = false;
  lineOverflowed_ = false;
}

bool LineReader::feed(char c) {
  if (c == '\r') return false;
  if (c == '\n') {
    buf_[len_] = '\0';
    lineOverflowed_ = dropping_;
    len_ = 0;
    dropping_ = false;      // always recover for the next line
    return true;
  }
  if (len_ >= kMaxLineIn) {
    dropping_ = true;       // keep consuming until newline, discard content
    return false;
  }
  buf_[len_++] = c;
  return false;
}

static Op opFromString(const char* s) {
  if (strcmp(s, "hello") == 0)    return Op::Hello;
  if (strcmp(s, "schema") == 0)   return Op::Schema;
  if (strcmp(s, "get") == 0)      return Op::Get;
  if (strcmp(s, "getall") == 0)   return Op::GetAll;
  if (strcmp(s, "set") == 0)      return Op::Set;
  if (strcmp(s, "save") == 0)     return Op::Save;
  if (strcmp(s, "defaults") == 0) return Op::Defaults;
  if (strcmp(s, "tlm") == 0)      return Op::Tlm;
  return Op::Unknown;
}

Request parseRequest(const char* line) {
  Request q;
  JsonDocument doc;
  if (deserializeJson(doc, line) != DeserializationError::Ok) {
    q.err = "badjson";
    return q;
  }
  q.id = doc["id"] | 0u;

  const char* opStr = doc["op"] | "";
  q.op = opFromString(opStr);
  if (q.op == Op::Unknown) {
    q.err = "badop";     // id already captured, so a reply is still possible
    return q;
  }

  const char* key = doc["key"] | "";
  strncpy(q.key, key, sizeof(q.key) - 1);

  JsonVariant val = doc["val"];
  if (!val.isNull()) {
    if (val.is<const char*>()) {
      q.hasStr = true;
      strncpy(q.str, val.as<const char*>(), kMaxStrLen);
      q.str[kMaxStrLen] = '\0';
    } else {
      q.hasNum = true;
      q.num = val.as<int32_t>();
    }
  }

  q.tlmOn = doc["on"] | false;
  q.ok = true;
  return q;
}

}  // namespace core
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd firmware && ~/.platformio/penv/bin/pio test -e native -f test_protocol`
Expected: PASS, 9 tests.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/core/protocol.h firmware/src/core/protocol.cpp firmware/test/test_protocol/
git commit -m "feat: line framing and request parsing"
```

---

## Task 4: Request dispatch and the hardware seam (`core/dispatch`)

This is the task the whole testing strategy exists for: config logic verified with no board attached.

**Files:**
- Create: `firmware/src/core/dispatch.h`, `firmware/src/core/dispatch.cpp`
- Test: `firmware/test/test_dispatch/test_dispatch.cpp`

**Interfaces:**
- Consumes: `core::Params` (Task 2), `core::Request`/`core::Op` (Task 3).
- Produces:
  - `struct core::HardwareSink` with `virtual void onParamChanged(ParamId, const Params&)`
  - `struct core::Persistence` with `virtual bool save(const Params&)` and `virtual bool load(Params*)`
  - `class core::Dispatcher` with `Dispatcher(Params&, HardwareSink&, Persistence&)`, `size_t handle(const Request&, char* out, size_t cap)`, `bool telemetryEnabled() const`
  - `size_t core::writeTelemetry(char* out, size_t cap, const Telemetry& t)`
  - `struct core::Telemetry { uint32_t up; uint32_t clk; float temp; int32_t vdd; int32_t ram; uint8_t btn; }`

- [ ] **Step 1: Write the failing tests**

Create `firmware/test/test_dispatch/test_dispatch.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "core/dispatch.h"
#include "core/protocol.h"

using namespace core;

// --- test doubles ----------------------------------------------------------
struct MockSink : HardwareSink {
  int calls = 0;
  ParamId lastId = PARAM_COUNT;
  int32_t lastNum = -1;
  void onParamChanged(ParamId id, const Params& p) override {
    ++calls;
    lastId = id;
    lastNum = p.num(id);
  }
};

struct MockStore : Persistence {
  int saveCalls = 0;
  bool saveOk = true;
  bool save(const Params&) override { ++saveCalls; return saveOk; }
  bool load(Params*) override { return false; }
};

static char out[kMaxLineOut];

// --- tests -----------------------------------------------------------------
void test_set_applies_to_hardware_exactly_once() {
  Params p; MockSink sink; MockStore store;
  Dispatcher d(p, sink, store);

  Request q = parseRequest("{\"id\":1,\"op\":\"set\",\"key\":\"led.blink_hz\",\"val\":5}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT(1, sink.calls);
  TEST_ASSERT_EQUAL(PARAM_LED_BLINK_HZ, sink.lastId);
  TEST_ASSERT_EQUAL_INT32(5, sink.lastNum);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":1"));
}

void test_rejected_set_does_not_touch_hardware() {
  Params p; MockSink sink; MockStore store;
  Dispatcher d(p, sink, store);

  Request q = parseRequest("{\"id\":2,\"op\":\"set\",\"key\":\"led.blink_hz\",\"val\":99}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT(0, sink.calls);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":false"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"err\":\"range\""));
}

void test_set_unknown_key_returns_nokey() {
  Params p; MockSink sink; MockStore store;
  Dispatcher d(p, sink, store);

  Request q = parseRequest("{\"id\":3,\"op\":\"set\",\"key\":\"no.such\",\"val\":1}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(0, sink.calls);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"err\":\"nokey\""));
}

void test_hello_reports_proto_version() {
  Params p; MockSink sink; MockStore store;
  Dispatcher d(p, sink, store);

  Request q = parseRequest("{\"id\":4,\"op\":\"hello\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"proto\":1"));
  TEST_ASSERT_NOT_NULL(strstr(out, "blackpill_f411ce"));
}

void test_schema_lists_all_params_and_fits_buffer() {
  Params p; MockSink sink; MockStore store;
  Dispatcher d(p, sink, store);

  Request q = parseRequest("{\"id\":5,\"op\":\"schema\"}");
  size_t n = d.handle(q, out, sizeof(out));

  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(n < kMaxLineOut);   // must not truncate
  TEST_ASSERT_NOT_NULL(strstr(out, "led.mode"));
  TEST_ASSERT_NOT_NULL(strstr(out, "led.blink_hz"));
  TEST_ASSERT_NOT_NULL(strstr(out, "device.name"));
  TEST_ASSERT_NOT_NULL(strstr(out, "tlm.rate"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"options\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"unit\":\"Hz\""));
}

void test_getall_returns_every_value() {
  Params p; MockSink sink; MockStore store;
  Dispatcher d(p, sink, store);

  Request q = parseRequest("{\"id\":6,\"op\":\"getall\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"led.mode\":\"blink\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"led.blink_hz\":2"));
}

void test_save_delegates_to_persistence() {
  Params p; MockSink sink; MockStore store;
  Dispatcher d(p, sink, store);

  Request q = parseRequest("{\"id\":7,\"op\":\"save\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(1, store.saveCalls);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));
}

void test_save_failure_reported() {
  Params p; MockSink sink; MockStore store;
  store.saveOk = false;
  Dispatcher d(p, sink, store);

  Request q = parseRequest("{\"id\":8,\"op\":\"save\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":false"));
}

void test_defaults_restores_and_notifies_every_param() {
  Params p; MockSink sink; MockStore store;
  Dispatcher d(p, sink, store);
  p.setNum(PARAM_LED_BLINK_HZ, 15);
  sink.calls = 0;

  Request q = parseRequest("{\"id\":9,\"op\":\"defaults\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT32(2, p.num(PARAM_LED_BLINK_HZ));
  TEST_ASSERT_EQUAL_INT(PARAM_COUNT, sink.calls);  // hardware resynced
}

void test_tlm_op_toggles_streaming() {
  Params p; MockSink sink; MockStore store;
  Dispatcher d(p, sink, store);
  TEST_ASSERT_TRUE(d.telemetryEnabled());   // on by default

  Request off = parseRequest("{\"id\":10,\"op\":\"tlm\",\"on\":false}");
  d.handle(off, out, sizeof(out));
  TEST_ASSERT_FALSE(d.telemetryEnabled());
}

void test_bad_request_still_gets_a_reply() {
  Params p; MockSink sink; MockStore store;
  Dispatcher d(p, sink, store);

  Request q = parseRequest("{\"id\":11,\"op\":\"bogus\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"err\":\"badop\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":11"));
}

void test_telemetry_frame_has_no_id() {
  Telemetry t{1204, 96, 41.2f, 3298, 18432, 0};
  size_t n = writeTelemetry(out, sizeof(out), t);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"tlm\""));
  TEST_ASSERT_NULL(strstr(out, "\"id\""));   // id-less: this is what makes interleaving safe
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_set_applies_to_hardware_exactly_once);
  RUN_TEST(test_rejected_set_does_not_touch_hardware);
  RUN_TEST(test_set_unknown_key_returns_nokey);
  RUN_TEST(test_hello_reports_proto_version);
  RUN_TEST(test_schema_lists_all_params_and_fits_buffer);
  RUN_TEST(test_getall_returns_every_value);
  RUN_TEST(test_save_delegates_to_persistence);
  RUN_TEST(test_save_failure_reported);
  RUN_TEST(test_defaults_restores_and_notifies_every_param);
  RUN_TEST(test_tlm_op_toggles_streaming);
  RUN_TEST(test_bad_request_still_gets_a_reply);
  RUN_TEST(test_telemetry_frame_has_no_id);
  return UNITY_END();
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd firmware && ~/.platformio/penv/bin/pio test -e native -f test_dispatch`
Expected: FAIL — `core/dispatch.h: No such file or directory`

- [ ] **Step 3: Write `firmware/src/core/dispatch.h`**

```cpp
#pragma once
#include "core/params.h"
#include "core/protocol.h"

namespace core {

// The seam: core never touches GPIO, it announces changes through this.
struct HardwareSink {
  virtual ~HardwareSink() {}
  virtual void onParamChanged(ParamId id, const Params& p) = 0;
};

// The same principle applied to flash, so `save` is testable natively.
struct Persistence {
  virtual ~Persistence() {}
  virtual bool save(const Params& p) = 0;
  virtual bool load(Params* p) = 0;
};

struct Telemetry {
  uint32_t up;    // ms
  uint32_t clk;   // MHz
  float    temp;  // degC
  int32_t  vdd;   // mV
  int32_t  ram;   // free bytes
  uint8_t  btn;   // 0|1
};

size_t writeTelemetry(char* out, size_t cap, const Telemetry& t);

class Dispatcher {
 public:
  Dispatcher(Params& p, HardwareSink& sink, Persistence& store)
      : p_(p), sink_(sink), store_(store) {}

  // Writes a response line (no trailing newline) into out. Returns length.
  size_t handle(const Request& q, char* out, size_t cap);

  bool telemetryEnabled() const { return tlmOn_; }

 private:
  Params&       p_;
  HardwareSink& sink_;
  Persistence&  store_;
  bool          tlmOn_ = true;
};

}  // namespace core
```

- [ ] **Step 4: Write `firmware/src/core/dispatch.cpp`**

```cpp
#include "core/dispatch.h"
#include <ArduinoJson.h>
#include <string.h>

namespace core {

static const char* errName(SetResult r) {
  switch (r) {
    case SetResult::Range:     return "range";
    case SetResult::BadEnum:   return "enum";
    case SetResult::TooLong:   return "toolong";
    case SetResult::WrongType: return "badtype";
    case SetResult::NoKey:     return "nokey";
    default:                   return "err";
  }
}

static void putValue(JsonObject o, const char* name, ParamId id, const Params& p) {
  if (defs()[id].type == ParamType::U8) o[name] = p.num(id);
  else                                  o[name] = p.str(id);
}

size_t writeTelemetry(char* out, size_t cap, const Telemetry& t) {
  JsonDocument doc;
  JsonObject o = doc["tlm"].to<JsonObject>();
  o["up"]   = t.up;
  o["clk"]  = t.clk;
  o["temp"] = t.temp;
  o["vdd"]  = t.vdd;
  o["ram"]  = t.ram;
  o["btn"]  = t.btn;
  return serializeJson(doc, out, cap);
}

size_t Dispatcher::handle(const Request& q, char* out, size_t cap) {
  JsonDocument doc;
  doc["id"] = q.id;

  if (!q.ok) {
    doc["ok"] = false;
    doc["err"] = q.err ? q.err : "err";
    return serializeJson(doc, out, cap);
  }

  switch (q.op) {
    case Op::Hello:
      doc["ok"] = true;
      doc["fw"] = "app-demo 0.1.0";
      doc["proto"] = kProtoVersion;
      doc["board"] = "blackpill_f411ce";
      break;

    case Op::Schema: {
      doc["ok"] = true;
      JsonArray arr = doc["params"].to<JsonArray>();
      for (uint8_t i = 0; i < PARAM_COUNT; ++i) {
        const ParamDef& d = defs()[i];
        JsonObject e = arr.add<JsonObject>();
        e["key"] = d.key;
        switch (d.type) {
          case ParamType::U8:
            e["type"] = "u8";
            e["min"] = d.minVal;
            e["max"] = d.maxVal;
            e["def"] = d.defNum;
            break;
          case ParamType::Enum: {
            e["type"] = "enum";
            JsonArray opts = e["options"].to<JsonArray>();
            for (uint8_t k = 0; k < d.optionCount; ++k) opts.add(d.options[k]);
            e["def"] = d.options[d.defNum];
            break;
          }
          case ParamType::Str:
            e["type"] = "str";
            e["maxlen"] = (uint32_t)d.maxLen;
            e["def"] = d.defStr;
            break;
        }
        e["label"] = d.label;
        if (d.unit) e["unit"] = d.unit;
      }
      break;
    }

    case Op::Get: {
      ParamId id;
      if (!findParam(q.key, &id)) { doc["ok"] = false; doc["err"] = "nokey"; break; }
      doc["ok"] = true;
      doc["key"] = q.key;
      putValue(doc.as<JsonObject>(), "val", id, p_);
      break;
    }

    case Op::GetAll: {
      doc["ok"] = true;
      JsonObject vals = doc["vals"].to<JsonObject>();
      for (uint8_t i = 0; i < PARAM_COUNT; ++i)
        putValue(vals, defs()[i].key, static_cast<ParamId>(i), p_);
      break;
    }

    case Op::Set: {
      ParamId id;
      if (!findParam(q.key, &id)) { doc["ok"] = false; doc["err"] = "nokey"; break; }
      SetResult r = q.hasStr ? p_.setStr(id, q.str)
                             : p_.setNum(id, q.num);
      if (r != SetResult::Ok) { doc["ok"] = false; doc["err"] = errName(r); break; }
      sink_.onParamChanged(id, p_);   // only ever on success
      doc["ok"] = true;
      break;
    }

    case Op::Save:
      doc["ok"] = store_.save(p_);
      if (!doc["ok"]) doc["err"] = "flash";
      break;

    case Op::Defaults:
      p_.loadDefaults();
      for (uint8_t i = 0; i < PARAM_COUNT; ++i)
        sink_.onParamChanged(static_cast<ParamId>(i), p_);
      doc["ok"] = true;
      break;

    case Op::Tlm:
      tlmOn_ = q.tlmOn;
      doc["ok"] = true;
      break;

    default:
      doc["ok"] = false;
      doc["err"] = "badop";
      break;
  }

  return serializeJson(doc, out, cap);
}

}  // namespace core
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd firmware && ~/.platformio/penv/bin/pio test -e native -f test_dispatch`
Expected: PASS, 12 tests.

- [ ] **Step 6: Run the whole native suite**

Run: `cd firmware && ~/.platformio/penv/bin/pio test -e native`
Expected: PASS, 29 tests total across 4 suites.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/core/dispatch.h firmware/src/core/dispatch.cpp firmware/test/test_dispatch/
git commit -m "feat: request dispatch with hardware and persistence seams"
```

---

## Task 5: Firmware glue — hardware and main loop

First task needing the board. Verified by hand in a serial monitor; there are no automated tests for this layer by design.

**Files:**
- Create: `firmware/src/hardware.h`, `firmware/src/hardware.cpp`
- Create: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `core::Params`, `core::Dispatcher`, `core::HardwareSink`, `core::Persistence`, `core::LineReader`, `core::parseRequest`, `core::writeTelemetry`, `core::Telemetry`.
- Produces: `hw::LedDriver` (`begin()`, `apply(mode, hz)`, `tick(nowMs)`), `hw::readTelemetry(core::Telemetry*)`, `hw::buttonPressed()`. Task 6 replaces the stub `Persistence` used here.

- [ ] **Step 1: Write `firmware/src/hardware.h`**

```cpp
#pragma once
#include "core/dispatch.h"

namespace hw {

class LedDriver {
 public:
  void begin();
  void apply(int32_t modeIdx, int32_t blinkHz);  // 0=off 1=on 2=blink
  void tick(uint32_t nowMs);

 private:
  int32_t  mode_ = 2;
  int32_t  hz_ = 2;
  uint32_t lastToggle_ = 0;
  bool     on_ = false;
  void write(bool on);
};

void begin();
void readTelemetry(core::Telemetry* t);
uint8_t buttonPressed();

}  // namespace hw
```

- [ ] **Step 2: Write `firmware/src/hardware.cpp`**

Telemetry readers ported from `test1/src/main.cpp` (`readVddaMv` :226, `readTempC` :231, `freeRamBytes` :57) along with the factory calibration constants at :31-36.

```cpp
#include <Arduino.h>
#include "hardware.h"

namespace hw {

// STM32F411 factory calibration (reference manual)
#define VREFINT_CAL  (*((uint16_t *)0x1FFF7A2AU))
#define TS_CAL1      (*((uint16_t *)0x1FFF7A2CU))
#define TS_CAL2      (*((uint16_t *)0x1FFF7A2EU))
static const int32_t CAL_VDDA_MV = 3300;
static const int32_t TS_CAL1_TEMP = 30;
static const int32_t TS_CAL2_TEMP = 110;

extern "C" char *sbrk(int incr);
static int freeRamBytes() {
  char top;
  return (int)(&top - (char *)sbrk(0));
}

static int32_t readVddaMv() {
  analogReadResolution(12);
  int32_t raw = analogRead(AVREF);
  if (raw <= 0) return CAL_VDDA_MV;
  return (CAL_VDDA_MV * (int32_t)VREFINT_CAL) / raw;
}

static float readTempC(int32_t vddaMv) {
  analogReadResolution(12);
  int32_t raw = analogRead(ATEMP);
  int32_t adj = (raw * vddaMv) / CAL_VDDA_MV;
  int32_t span = (int32_t)TS_CAL2 - (int32_t)TS_CAL1;
  if (span == 0) return 0.0f;
  return (float)(adj - (int32_t)TS_CAL1) * (TS_CAL2_TEMP - TS_CAL1_TEMP) / span
         + TS_CAL1_TEMP;
}

// --- LED: PC13 is ACTIVE-LOW (LOW = on) and has no timer channel -----------
void LedDriver::write(bool on) { digitalWrite(LED_BUILTIN, on ? LOW : HIGH); }

void LedDriver::begin() {
  pinMode(LED_BUILTIN, OUTPUT);
  write(false);
}

void LedDriver::apply(int32_t modeIdx, int32_t blinkHz) {
  mode_ = modeIdx;
  hz_ = blinkHz < 1 ? 1 : blinkHz;
  if (mode_ == 0) { on_ = false; write(false); }
  else if (mode_ == 1) { on_ = true; write(true); }
}

void LedDriver::tick(uint32_t nowMs) {
  if (mode_ != 2) return;
  uint32_t halfPeriod = 500u / (uint32_t)hz_;   // hz_ full cycles per second
  if (halfPeriod == 0) halfPeriod = 1;
  if (nowMs - lastToggle_ >= halfPeriod) {
    lastToggle_ = nowMs;
    on_ = !on_;
    write(on_);
  }
}

static bool btnIdle = true;

void begin() {
  pinMode(USER_BTN, INPUT_PULLUP);
  btnIdle = digitalRead(USER_BTN);   // assume not pressed at boot
}

uint8_t buttonPressed() {
  return (digitalRead(USER_BTN) != btnIdle) ? 1 : 0;
}

void readTelemetry(core::Telemetry* t) {
  int32_t vdd = readVddaMv();
  t->up   = millis();
  t->clk  = SystemCoreClock / 1000000UL;
  t->temp = readTempC(vdd);
  t->vdd  = vdd;
  t->ram  = freeRamBytes();
  t->btn  = buttonPressed();
}

}  // namespace hw
```

- [ ] **Step 3: Write `firmware/src/main.cpp`**

A stub `Persistence` is used here; Task 6 replaces it with real flash storage.

```cpp
#include <Arduino.h>
#include "core/dispatch.h"
#include "core/protocol.h"
#include "hardware.h"

using namespace core;

static Params      g_params;
static hw::LedDriver g_led;
static LineReader  g_reader;

struct ArduinoSink : HardwareSink {
  void onParamChanged(ParamId id, const Params& p) override {
    if (id == PARAM_LED_MODE || id == PARAM_LED_BLINK_HZ) {
      g_led.apply(p.num(PARAM_LED_MODE), p.num(PARAM_LED_BLINK_HZ));
    }
  }
};

// Replaced by FlashStore in Task 6.
struct NullStore : Persistence {
  bool save(const Params&) override { return false; }
  bool load(Params*) override { return false; }
};

static ArduinoSink g_sink;
static NullStore   g_store;
static Dispatcher  g_dispatch(g_params, g_sink, g_store);

static char g_out[kMaxLineOut];
static uint32_t g_lastTlm = 0;

void setup() {
  Serial.begin(115200);
  hw::begin();
  g_led.begin();
  g_led.apply(g_params.num(PARAM_LED_MODE), g_params.num(PARAM_LED_BLINK_HZ));
}

void loop() {
  uint32_t now = millis();

  while (Serial.available()) {
    if (g_reader.feed((char)Serial.read())) {
      if (g_reader.overflowed()) {
        Serial.println("{\"ok\":false,\"err\":\"overflow\"}");
      } else if (g_reader.line()[0] != '\0') {
        Request q = parseRequest(g_reader.line());
        size_t n = g_dispatch.handle(q, g_out, sizeof(g_out));
        if (n > 0) Serial.println(g_out);
      }
    }
  }

  g_led.tick(now);

  if (g_dispatch.telemetryEnabled()) {
    uint32_t period = 1000u / (uint32_t)g_params.num(PARAM_TLM_RATE);
    if (period == 0) period = 1;
    if (now - g_lastTlm >= period) {
      g_lastTlm = now;
      Telemetry t;
      hw::readTelemetry(&t);
      size_t n = writeTelemetry(g_out, sizeof(g_out), t);
      if (n > 0) Serial.println(g_out);
    }
  }
}
```

- [ ] **Step 4: Build and upload**

```bash
cd firmware
~/.platformio/penv/bin/pio run -e blackpill_f411ce -t upload
```

If upload picks the wrong ST-Link (two are attached), add `upload_port = <serial>` to `platformio.ini`.

- [ ] **Step 5: Verify by hand in a serial monitor**

Run: `cd firmware && ~/.platformio/penv/bin/pio device monitor -b 115200`

Telemetry lines should stream immediately. Type each of these and confirm the response:

```
{"id":1,"op":"hello"}          -> {"id":1,"ok":true,"fw":...,"proto":1,...}
{"id":2,"op":"schema"}         -> full param list, NOT truncated
{"id":3,"op":"getall"}         -> all four values
{"id":4,"op":"set","key":"led.blink_hz","val":10}   -> ok, LED VISIBLY FASTER
{"id":5,"op":"set","key":"led.blink_hz","val":99}   -> {"ok":false,"err":"range"}
{"id":6,"op":"set","key":"led.mode","val":"off"}    -> ok, LED OFF
{"id":7,"op":"set","key":"led.mode","val":"on"}     -> ok, LED SOLID ON
{"id":8,"op":"set","key":"tlm.rate","val":1}        -> ok, telemetry VISIBLY SLOWER
{"id":9,"op":"tlm","on":false}                      -> ok, telemetry STOPS
```

Confirm: schema output is complete (the 1024-byte buffer holds it), and the LED responds visibly to blink rate and mode.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/hardware.h firmware/src/hardware.cpp firmware/src/main.cpp
git commit -m "feat: firmware hardware layer and main loop"
```

---

## Task 6: Flash persistence (`save` / `defaults`)

**Files:**
- Create: `firmware/src/storage.h`, `firmware/src/storage.cpp`
- Modify: `firmware/src/main.cpp` — swap `NullStore` for `FlashStore`, load at boot

**Interfaces:**
- Consumes: `core::Persistence`, `core::Params`, `core::Value`, `core::PARAM_COUNT`.
- Produces: `class FlashStore : public core::Persistence`.

- [ ] **Step 1: Write `firmware/src/storage.h`**

```cpp
#pragma once
#include "core/dispatch.h"

class FlashStore : public core::Persistence {
 public:
  bool save(const core::Params& p) override;
  bool load(core::Params* p) override;
};
```

- [ ] **Step 2: Write `firmware/src/storage.cpp`**

Uses STM32duino's **buffered** EEPROM API deliberately: `EEPROM.write()` flushes on every byte, which would erase the sector once per byte. Fill, write all bytes, flush once.

```cpp
#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>
#include "storage.h"

using namespace core;

static const uint32_t kMagic = 0x4D444C31;  // "MDL1"
static const uint16_t kVersion = 1;

struct Header {
  uint32_t magic;
  uint16_t version;
  uint16_t crc;
};

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
  const size_t payloadLen = sizeof(Value) * PARAM_COUNT;

  Header h;
  h.magic = kMagic;
  h.version = kVersion;
  h.crc = crc16(payload, payloadLen);

  eeprom_buffer_fill();                       // pull page into RAM
  size_t off = 0;
  const uint8_t* hp = reinterpret_cast<const uint8_t*>(&h);
  for (size_t i = 0; i < sizeof(Header); ++i) eeprom_buffered_write_byte(off++, hp[i]);
  for (size_t i = 0; i < payloadLen; ++i)     eeprom_buffered_write_byte(off++, payload[i]);
  eeprom_buffer_flush();                      // ONE erase+write, stalls ~1s
  return true;
}

bool FlashStore::load(core::Params* p) {
  eeprom_buffer_fill();

  Header h;
  uint8_t* hp = reinterpret_cast<uint8_t*>(&h);
  size_t off = 0;
  for (size_t i = 0; i < sizeof(Header); ++i) hp[i] = eeprom_buffered_read_byte(off++);

  if (h.magic != kMagic || h.version != kVersion) return false;

  const size_t payloadLen = sizeof(Value) * PARAM_COUNT;
  static uint8_t buf[sizeof(Value) * PARAM_COUNT];
  for (size_t i = 0; i < payloadLen; ++i) buf[i] = eeprom_buffered_read_byte(off++);

  if (crc16(buf, payloadLen) != h.crc) return false;

  memcpy(p->rawMutable(), buf, payloadLen);
  return true;
}
```

- [ ] **Step 3: Wire it into `firmware/src/main.cpp`**

Replace the `NullStore` struct and its instance:

```cpp
#include "storage.h"
// ... delete: struct NullStore { ... };  and  static NullStore g_store;
static FlashStore g_store;
```

And in `setup()`, load saved values before applying them to hardware:

```cpp
void setup() {
  Serial.begin(115200);
  hw::begin();
  g_led.begin();
  g_store.load(&g_params);   // falls back to defaults on magic/version/CRC mismatch
  g_led.apply(g_params.num(PARAM_LED_MODE), g_params.num(PARAM_LED_BLINK_HZ));
}
```

- [ ] **Step 4: Build, upload, verify persistence by hand**

```bash
cd firmware && ~/.platformio/penv/bin/pio run -e blackpill_f411ce -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

Then:

```
{"id":1,"op":"set","key":"led.blink_hz","val":15}   -> ok
{"id":2,"op":"save"}                                -> ok (telemetry gaps ~1s — expected)
```

**Now physically unplug and replug the board.** Reconnect the monitor and run:

```
{"id":3,"op":"getall"}     -> led.blink_hz MUST still be 15
{"id":4,"op":"defaults"}   -> ok, LED returns to 2Hz blink
{"id":5,"op":"getall"}     -> led.blink_hz back to 2
```

Confirm the ~1s telemetry gap during `save` — the UI must tolerate this later (Task 10).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/storage.h firmware/src/storage.cpp firmware/src/main.cpp
git commit -m "feat: flash persistence for parameters"
```

---

## Task 7: Python environment and JSON-lines codec

**Files:**
- Create: `app/requirements.txt`, `app/backend/__init__.py`, `app/backend/protocol.py`
- Create: `app/tests/__init__.py`, `app/tests/test_protocol.py`

**Interfaces:**
- Consumes: nothing.
- Produces: `ProtocolError`, `encode(req_id: int, op: str, **fields) -> str`, `decode(line: str) -> dict`, `is_response(msg) -> bool`, `is_telemetry(msg) -> bool`, `is_log(msg) -> bool`.

- [ ] **Step 1: Create the venv and confirm wheels exist on Python 3.14**

This is the Global Constraints risk check — do it before writing any backend code.

```bash
cd app
python3 -m venv .venv
.venv/bin/pip install --upgrade pip
.venv/bin/pip install fastapi "uvicorn[standard]" pyserial pytest httpx websockets
.venv/bin/python -c "import fastapi, uvicorn, serial, pydantic; print('ok', fastapi.__version__)"
```

If any package fails to build a wheel on 3.14.4, stop and recreate the venv with `python3.12 -m venv .venv`. Record the fallback in `_notes/progress.md` under Deviations.

- [ ] **Step 2: Write `app/requirements.txt`**

```
fastapi
uvicorn[standard]
pyserial
pytest
httpx
websockets
```

- [ ] **Step 3: Write the failing tests**

Create `app/tests/__init__.py` (empty) and `app/tests/test_protocol.py`:

```python
import pytest
from backend.protocol import (
    ProtocolError, encode, decode, is_response, is_telemetry, is_log,
)


def test_encode_produces_one_terminated_line():
    line = encode(7, "set", key="led.blink_hz", val=5)
    assert line.endswith("\n")
    assert line.count("\n") == 1
    assert decode(line) == {"id": 7, "op": "set", "key": "led.blink_hz", "val": 5}


def test_encode_omits_none_fields():
    assert "key" not in decode(encode(1, "hello", key=None))


def test_decode_rejects_garbage():
    with pytest.raises(ProtocolError):
        decode("{not json")
    with pytest.raises(ProtocolError):
        decode("")
    with pytest.raises(ProtocolError):
        decode("[1,2,3]")          # valid JSON, wrong shape


def test_message_classification():
    resp = {"id": 3, "ok": True}
    tlm = {"tlm": {"up": 10}}
    log = {"log": "saved"}

    assert is_response(resp) and not is_telemetry(resp) and not is_log(resp)
    assert is_telemetry(tlm) and not is_response(tlm)
    assert is_log(log) and not is_response(log)
```

- [ ] **Step 4: Run tests to verify they fail**

Run: `cd app && .venv/bin/pytest tests/test_protocol.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'backend'`

- [ ] **Step 5: Write `app/backend/protocol.py`**

Also create an empty `app/backend/__init__.py`.

```python
"""JSON-lines codec. Mirrors firmware/src/core/protocol.cpp."""
import json


class ProtocolError(Exception):
    """A line could not be understood as a protocol message."""


def encode(req_id: int, op: str, **fields) -> str:
    """Build one newline-terminated request line. None-valued fields are dropped."""
    msg = {"id": req_id, "op": op}
    msg.update({k: v for k, v in fields.items() if v is not None})
    return json.dumps(msg, separators=(",", ":")) + "\n"


def decode(line: str) -> dict:
    """Parse one line into a dict, or raise ProtocolError."""
    line = line.strip()
    if not line:
        raise ProtocolError("empty line")
    try:
        obj = json.loads(line)
    except json.JSONDecodeError as exc:
        raise ProtocolError(f"bad json: {exc}") from exc
    if not isinstance(obj, dict):
        raise ProtocolError("message is not a JSON object")
    return obj


# An id means it answers a request. No id means the device volunteered it —
# that distinction is what lets telemetry interleave with request/response.
def is_response(msg: dict) -> bool:
    return "id" in msg


def is_telemetry(msg: dict) -> bool:
    return "id" not in msg and "tlm" in msg


def is_log(msg: dict) -> bool:
    return "id" not in msg and "log" in msg
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cd app && .venv/bin/pytest tests/test_protocol.py -v`
Expected: PASS, 4 tests.

- [ ] **Step 7: Add a pytest config so `backend` imports resolve**

Create `app/pytest.ini`:

```ini
[pytest]
pythonpath = .
testpaths = tests
```

- [ ] **Step 8: Commit**

```bash
git add app/requirements.txt app/pytest.ini app/backend/ app/tests/
git commit -m "feat: python json-lines codec"
```

---

## Task 8: Serial link with correlation and timeouts (`link.py`)

The highest-risk unit. The fake serial port is what makes it testable.

**Files:**
- Create: `app/tests/fake_serial.py`
- Create: `app/backend/link.py`
- Test: `app/tests/test_link.py`

**Interfaces:**
- Consumes: `backend.protocol` (Task 7).
- Produces:
  - `class SerialLink(open_port: Callable[[str], Any] | None = None)` with `connect(port: str)`, `disconnect()`, `request(op: str, timeout: float = 1.0, **fields) -> dict`, `subscribe(cb: Callable[[dict], None])`, property `state -> str` (`"disconnected"` | `"connected"`)
  - `class NotConnected(Exception)`, `class RequestTimeout(Exception)`
  - `list_candidate_ports() -> list[dict]`

- [ ] **Step 1: Write the fake serial port**

Create `app/tests/fake_serial.py`:

```python
"""Scripted stand-in for serial.Serial.

Real hardware cannot reliably reproduce out-of-order replies, mid-request
disconnects, or garbage bytes. This can, deterministically.
"""
import json
import queue
import threading


class FakeDisconnected(Exception):
    """Raised from readline/write once the fake port is 'unplugged'."""


class FakeSerial:
    def __init__(self, responder=None, timeout=0.05):
        # responder(request_dict, emit) -> None. emit(obj_or_str) queues a line.
        self.responder = responder
        self.timeout = timeout
        self.is_open = True
        self.written = []
        self._lines = queue.Queue()
        self._gone = False
        self._lock = threading.Lock()

    # --- test-side controls -------------------------------------------------
    def emit(self, obj):
        """Queue a line for the link to read. Accepts a dict or a raw string."""
        text = obj if isinstance(obj, str) else json.dumps(obj)
        self._lines.put(text.encode() + b"\n")

    def unplug(self):
        self._gone = True
        self._lines.put(None)   # wake a blocked readline

    # --- serial.Serial surface ---------------------------------------------
    def write(self, data):
        if self._gone:
            raise FakeDisconnected("port gone")
        with self._lock:
            self.written.append(data)
        req = json.loads(data.decode())
        if self.responder:
            self.responder(req, self.emit)
        return len(data)

    def readline(self):
        if self._gone:
            raise FakeDisconnected("port gone")
        try:
            item = self._lines.get(timeout=self.timeout)
        except queue.Empty:
            return b""          # pyserial returns empty on timeout
        if item is None:
            raise FakeDisconnected("port gone")
        return item

    def close(self):
        self.is_open = False
```

- [ ] **Step 2: Write the failing tests**

Create `app/tests/test_link.py`:

```python
import threading
import time

import pytest

from backend.link import SerialLink, NotConnected, RequestTimeout
from tests.fake_serial import FakeSerial, FakeDisconnected


def make_link(responder=None):
    fake = FakeSerial(responder=responder)
    link = SerialLink(open_port=lambda port: fake)
    return link, fake


def echo_ok(req, emit):
    emit({"id": req["id"], "ok": True, "op": req["op"]})


def test_request_resolves_with_matching_id():
    link, _ = make_link(echo_ok)
    link.connect("/dev/fake")
    try:
        resp = link.request("hello")
        assert resp["ok"] is True
        assert resp["op"] == "hello"
    finally:
        link.disconnect()


def test_out_of_order_responses_resolve_correctly():
    """id=8 answered before id=7. Each caller must get its own reply."""
    held = []

    def responder(req, emit):
        held.append((req, emit))
        if len(held) == 2:
            (r1, e1), (r2, e2) = held
            e2({"id": r2["id"], "ok": True, "who": "second"})
            e1({"id": r1["id"], "ok": True, "who": "first"})

    link, _ = make_link(responder)
    link.connect("/dev/fake")
    results = {}

    def call(name):
        results[name] = link.request("get", key=name, timeout=2.0)

    t1 = threading.Thread(target=call, args=("first",))
    t2 = threading.Thread(target=call, args=("second",))
    t1.start(); time.sleep(0.02); t2.start()
    t1.join(3); t2.join(3)
    link.disconnect()

    assert results["first"]["who"] == "first"
    assert results["second"]["who"] == "second"


def test_late_response_times_out():
    link, _ = make_link(responder=lambda req, emit: None)   # never answers
    link.connect("/dev/fake")
    try:
        with pytest.raises(RequestTimeout):
            link.request("hello", timeout=0.2)
    finally:
        link.disconnect()


def test_telemetry_interleaved_mid_request_is_published_not_matched():
    seen = []

    def responder(req, emit):
        emit({"tlm": {"up": 1}})            # arrives before the reply
        emit({"id": req["id"], "ok": True})
        emit({"tlm": {"up": 2}})            # and after

    link, _ = make_link(responder)
    link.subscribe(seen.append)
    link.connect("/dev/fake")
    try:
        assert link.request("hello")["ok"] is True
        time.sleep(0.1)
    finally:
        link.disconnect()

    tlm = [m for m in seen if "tlm" in m]
    assert len(tlm) == 2


def test_garbage_line_does_not_wedge_the_reader():
    def responder(req, emit):
        emit("}{ not json at all")          # must be discarded silently
        emit({"id": req["id"], "ok": True})

    link, _ = make_link(responder)
    link.connect("/dev/fake")
    try:
        assert link.request("hello", timeout=1.0)["ok"] is True
    finally:
        link.disconnect()


def test_port_vanishing_mid_request_disconnects_and_raises():
    holder = {}

    def unplug_on_request(req, emit):
        holder["fake"].unplug()      # board yanked before it can answer

    link, fake = make_link(unplug_on_request)
    holder["fake"] = fake

    link.connect("/dev/fake")
    with pytest.raises((RequestTimeout, NotConnected)):
        link.request("hello", timeout=1.0)
    time.sleep(0.2)
    assert link.state == "disconnected"


def test_request_without_connection_raises():
    link = SerialLink(open_port=lambda port: FakeSerial())
    with pytest.raises(NotConnected):
        link.request("hello")
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cd app && .venv/bin/pytest tests/test_link.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'backend.link'`

- [ ] **Step 4: Write `app/backend/link.py`**

```python
"""Owns the serial port in a dedicated thread.

pyserial's read is blocking. Calling it from the asyncio event loop freezes the
whole server and looks exactly like a board crash, so all port I/O lives here.
"""
import threading
import logging

import serial
from serial.tools import list_ports

from . import protocol

log = logging.getLogger(__name__)

VID = 0x0483
PID = 0x5740


class NotConnected(Exception):
    pass


class RequestTimeout(Exception):
    pass


class _Pending:
    __slots__ = ("event", "response")

    def __init__(self):
        self.event = threading.Event()
        self.response = None


def list_candidate_ports() -> list[dict]:
    out = []
    for p in list_ports.comports():
        out.append({
            "port": p.device,
            "desc": p.description,
            "vid": f"{p.vid:04x}" if p.vid else None,
            "pid": f"{p.pid:04x}" if p.pid else None,
            "match": p.vid == VID and p.pid == PID,
        })
    return out


def _default_open(port: str):
    return serial.Serial(port, baudrate=115200, timeout=0.2)


class SerialLink:
    def __init__(self, open_port=None):
        self._open_port = open_port or _default_open
        self._port = None
        self._reader = None
        self._stop = threading.Event()
        self._pending: dict[int, _Pending] = {}
        self._lock = threading.Lock()
        self._next_id = 1
        self._subs = []
        self._state = "disconnected"

    @property
    def state(self) -> str:
        return self._state

    def subscribe(self, callback):
        self._subs.append(callback)

    def connect(self, port: str):
        self.disconnect()
        self._port = self._open_port(port)
        self._stop.clear()
        self._state = "connected"
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def disconnect(self):
        self._stop.set()
        port, self._port = self._port, None
        if port is not None:
            try:
                port.close()
            except Exception:
                pass
        if self._reader and self._reader.is_alive():
            self._reader.join(timeout=1.0)
        self._reader = None
        self._state = "disconnected"
        self._fail_all_pending()

    def request(self, op: str, timeout: float = 1.0, **fields) -> dict:
        port = self._port
        if port is None or self._state != "connected":
            raise NotConnected("no serial connection")

        with self._lock:
            req_id = self._next_id
            self._next_id = self._next_id % 65535 + 1
            slot = _Pending()
            self._pending[req_id] = slot

        line = protocol.encode(req_id, op, **fields)
        try:
            port.write(line.encode())
        except Exception as exc:
            self._drop_pending(req_id)
            self._on_port_lost()
            raise NotConnected(f"write failed: {exc}") from exc

        if not slot.event.wait(timeout):
            self._drop_pending(req_id)
            raise RequestTimeout(f"no response to {op} within {timeout}s")

        self._drop_pending(req_id)
        if slot.response is None:
            raise NotConnected("connection lost while waiting")
        return slot.response

    # --- internals ----------------------------------------------------------
    def _drop_pending(self, req_id: int):
        with self._lock:
            self._pending.pop(req_id, None)

    def _fail_all_pending(self):
        with self._lock:
            waiters = list(self._pending.values())
            self._pending.clear()
        for slot in waiters:
            slot.response = None
            slot.event.set()

    def _on_port_lost(self):
        self._state = "disconnected"
        self._stop.set()
        self._fail_all_pending()
        self._publish({"state": "disconnected"})

    def _publish(self, msg: dict):
        for cb in list(self._subs):
            try:
                cb(msg)
            except Exception:
                log.exception("subscriber raised")

    def _read_loop(self):
        port = self._port
        while not self._stop.is_set():
            try:
                raw = port.readline()
            except Exception:
                self._on_port_lost()
                return
            if not raw:
                continue
            try:
                msg = protocol.decode(raw.decode(errors="replace"))
            except protocol.ProtocolError:
                log.debug("discarding unparseable line: %r", raw)
                continue      # one bad line must never wedge the reader

            if protocol.is_response(msg):
                with self._lock:
                    slot = self._pending.get(msg["id"])
                if slot is not None:
                    slot.response = msg
                    slot.event.set()
                else:
                    log.debug("response for unknown id %s", msg.get("id"))
            else:
                self._publish(msg)
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cd app && .venv/bin/pytest tests/test_link.py -v`
Expected: PASS, 7 tests.

- [ ] **Step 6: Commit**

```bash
git add app/backend/link.py app/tests/fake_serial.py app/tests/test_link.py
git commit -m "feat: serial link with id correlation and timeouts"
```

---

## Task 9: Device model (`device.py`)

**Files:**
- Create: `app/backend/device.py`
- Test: `app/tests/test_device.py`

**Interfaces:**
- Consumes: `SerialLink`, `NotConnected`, `RequestTimeout` (Task 8).
- Produces: `class DeviceModel(link: SerialLink)` with `connect(port)`, `disconnect()`, `status() -> dict`, `schema() -> list[dict]`, `values() -> dict`, `set(key, val) -> None`, `save()`, `load_defaults()`, `subscribe(cb)`; `class ProtoMismatch(Exception)`, `class DeviceError(Exception)` (carries `.code`).

- [ ] **Step 1: Write the failing tests**

Create `app/tests/test_device.py`:

```python
import pytest

from backend.link import SerialLink
from backend.device import DeviceModel, ProtoMismatch, DeviceError
from tests.fake_serial import FakeSerial

SCHEMA = [
    {"key": "led.mode", "type": "enum", "options": ["off", "on", "blink"],
     "def": "blink", "label": "LED Mode"},
    {"key": "led.blink_hz", "type": "u8", "min": 1, "max": 20, "def": 2,
     "label": "Blink Rate", "unit": "Hz"},
    {"key": "device.name", "type": "str", "maxlen": 31, "def": "app-demo",
     "label": "Device Name"},
    {"key": "tlm.rate", "type": "u8", "min": 1, "max": 50, "def": 10,
     "label": "Telemetry Rate", "unit": "Hz"},
]
VALUES = {"led.mode": "blink", "led.blink_hz": 2,
          "device.name": "app-demo", "tlm.rate": 10}


def device_responder(proto=1):
    def responder(req, emit):
        op = req["op"]
        rid = req["id"]
        if op == "hello":
            emit({"id": rid, "ok": True, "fw": "app-demo 0.1.0",
                  "proto": proto, "board": "blackpill_f411ce"})
        elif op == "schema":
            emit({"id": rid, "ok": True, "params": SCHEMA})
        elif op == "getall":
            emit({"id": rid, "ok": True, "vals": dict(VALUES)})
        elif op == "set":
            emit({"id": rid, "ok": True})
        elif op in ("save", "defaults"):
            emit({"id": rid, "ok": True})
        else:
            emit({"id": rid, "ok": False, "err": "badop"})
    return responder


def make_device(proto=1):
    fake = FakeSerial(responder=device_responder(proto))
    return DeviceModel(SerialLink(open_port=lambda p: fake)), fake


def test_connect_caches_schema_and_values():
    dev, _ = make_device()
    dev.connect("/dev/fake")
    try:
        assert dev.status()["state"] == "connected"
        assert dev.status()["proto"] == 1
        assert len(dev.schema()) == 4
        assert dev.values()["led.blink_hz"] == 2
    finally:
        dev.disconnect()


def test_proto_mismatch_refuses_connection():
    dev, _ = make_device(proto=99)
    with pytest.raises(ProtoMismatch):
        dev.connect("/dev/fake")
    assert dev.status()["state"] == "disconnected"


def test_set_validates_against_cached_schema_before_sending():
    dev, fake = make_device()
    dev.connect("/dev/fake")
    try:
        before = len(fake.written)
        with pytest.raises(DeviceError) as exc:
            dev.set("led.blink_hz", 99)          # max is 20
        assert exc.value.code == "range"
        assert len(fake.written) == before        # nothing hit the wire
    finally:
        dev.disconnect()


def test_set_rejects_unknown_enum_option():
    dev, _ = make_device()
    dev.connect("/dev/fake")
    try:
        with pytest.raises(DeviceError) as exc:
            dev.set("led.mode", "purple")
        assert exc.value.code == "enum"
    finally:
        dev.disconnect()


def test_set_rejects_overlong_string():
    dev, _ = make_device()
    dev.connect("/dev/fake")
    try:
        with pytest.raises(DeviceError) as exc:
            dev.set("device.name", "x" * 40)
        assert exc.value.code == "toolong"
    finally:
        dev.disconnect()


def test_set_unknown_key_rejected():
    dev, _ = make_device()
    dev.connect("/dev/fake")
    try:
        with pytest.raises(DeviceError) as exc:
            dev.set("no.such.key", 1)
        assert exc.value.code == "nokey"
    finally:
        dev.disconnect()


def test_successful_set_updates_the_cache():
    dev, _ = make_device()
    dev.connect("/dev/fake")
    try:
        dev.set("led.blink_hz", 15)
        assert dev.values()["led.blink_hz"] == 15
    finally:
        dev.disconnect()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd app && .venv/bin/pytest tests/test_device.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'backend.device'`

- [ ] **Step 3: Write `app/backend/device.py`**

```python
"""Device model: schema cache, value cache, connection state.

Validates against the cached schema before sending, so bad input fails fast
with a useful message. The firmware validates again regardless — it must never
trust its host.
"""
import logging

from .link import SerialLink, NotConnected, RequestTimeout

log = logging.getLogger(__name__)

PROTO_VERSION = 1


class ProtoMismatch(Exception):
    pass


class DeviceError(Exception):
    def __init__(self, code: str, message: str = ""):
        super().__init__(message or code)
        self.code = code


class DeviceModel:
    def __init__(self, link: SerialLink | None = None):
        self._link = link or SerialLink()
        self._schema: list[dict] = []
        self._by_key: dict[str, dict] = {}
        self._values: dict = {}
        self._info: dict = {}

    def subscribe(self, callback):
        self._link.subscribe(callback)

    # --- lifecycle ----------------------------------------------------------
    def connect(self, port: str):
        self._link.connect(port)
        try:
            hello = self._link.request("hello")
            if hello.get("proto") != PROTO_VERSION:
                raise ProtoMismatch(
                    f"device speaks proto {hello.get('proto')}, "
                    f"this app speaks {PROTO_VERSION}"
                )
            self._info = {
                "fw": hello.get("fw"),
                "proto": hello.get("proto"),
                "board": hello.get("board"),
            }
            self._schema = self._link.request("schema")["params"]
            self._by_key = {p["key"]: p for p in self._schema}
            self._values = self._link.request("getall")["vals"]
        except Exception:
            self._link.disconnect()
            self._schema, self._by_key, self._values, self._info = [], {}, {}, {}
            raise

    def disconnect(self):
        self._link.disconnect()

    # --- reads --------------------------------------------------------------
    def status(self) -> dict:
        return {"state": self._link.state, **self._info}

    def schema(self) -> list[dict]:
        return self._schema

    def values(self) -> dict:
        return dict(self._values)

    # --- writes -------------------------------------------------------------
    def set(self, key: str, val):
        spec = self._by_key.get(key)
        if spec is None:
            raise DeviceError("nokey", f"unknown parameter {key!r}")

        self._validate(spec, val)

        resp = self._send("set", key=key, val=val)
        if not resp.get("ok"):
            raise DeviceError(resp.get("err", "err"), f"device rejected {key}")
        self._values[key] = val

    def save(self):
        # Flash erase stalls the MCU ~1s, so this needs a longer timeout than
        # a normal request.
        resp = self._send("save", timeout=5.0)
        if not resp.get("ok"):
            raise DeviceError(resp.get("err", "err"), "save failed")

    def load_defaults(self):
        resp = self._send("defaults")
        if not resp.get("ok"):
            raise DeviceError(resp.get("err", "err"), "defaults failed")
        self._values = self._send("getall")["vals"]

    # --- internals ----------------------------------------------------------
    @staticmethod
    def _validate(spec: dict, val):
        kind = spec["type"]
        if kind == "u8":
            if not isinstance(val, int) or isinstance(val, bool):
                raise DeviceError("badtype", "expected an integer")
            if val < spec["min"] or val > spec["max"]:
                raise DeviceError(
                    "range", f"must be {spec['min']}..{spec['max']}")
        elif kind == "enum":
            if val not in spec["options"]:
                raise DeviceError(
                    "enum", f"must be one of {', '.join(spec['options'])}")
        elif kind == "str":
            if not isinstance(val, str):
                raise DeviceError("badtype", "expected a string")
            if len(val) > spec["maxlen"]:
                raise DeviceError(
                    "toolong", f"max {spec['maxlen']} characters")

    def _send(self, op: str, timeout: float = 1.0, **fields) -> dict:
        try:
            return self._link.request(op, timeout=timeout, **fields)
        except RequestTimeout as exc:
            raise DeviceError("timeout", str(exc)) from exc
        except NotConnected as exc:
            raise DeviceError("disconnected", str(exc)) from exc
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd app && .venv/bin/pytest tests/test_device.py -v`
Expected: PASS, 7 tests.

- [ ] **Step 5: Commit**

```bash
git add app/backend/device.py app/tests/test_device.py
git commit -m "feat: device model with schema cache and validation"
```

---

## Task 10: HTTP API and WebSocket (`main.py`)

**Files:**
- Create: `app/backend/main.py`
- Test: `app/tests/test_api.py`

**Interfaces:**
- Consumes: `DeviceModel`, `DeviceError`, `ProtoMismatch` (Task 9), `list_candidate_ports` (Task 8).
- Produces: `create_app(device: DeviceModel | None = None) -> FastAPI`, module-level `app` for uvicorn.

- [ ] **Step 1: Write the failing tests**

Create `app/tests/test_api.py`:

```python
import pytest
from fastapi.testclient import TestClient

from backend.link import SerialLink
from backend.device import DeviceModel
from backend.main import create_app
from tests.fake_serial import FakeSerial
from tests.test_device import device_responder


@pytest.fixture
def client():
    fake = FakeSerial(responder=device_responder())
    device = DeviceModel(SerialLink(open_port=lambda p: fake))
    app = create_app(device)
    with TestClient(app) as c:
        yield c
    device.disconnect()


def test_status_starts_disconnected(client):
    assert client.get("/api/status").json()["state"] == "disconnected"


def test_ports_listing_is_a_list(client):
    assert isinstance(client.get("/api/ports").json(), list)


def test_connect_then_schema_and_params(client):
    assert client.post("/api/connect", json={"port": "/dev/fake"}).status_code == 200
    assert client.get("/api/status").json()["state"] == "connected"

    schema = client.get("/api/schema").json()
    assert len(schema) == 4
    assert {p["key"] for p in schema} == {
        "led.mode", "led.blink_hz", "device.name", "tlm.rate"}

    assert client.get("/api/params").json()["led.blink_hz"] == 2


def test_valid_set_returns_200_and_updates(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    assert client.put("/api/params/led.blink_hz", json={"val": 15}).status_code == 200
    assert client.get("/api/params").json()["led.blink_hz"] == 15


def test_out_of_range_set_returns_400_with_code(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    r = client.put("/api/params/led.blink_hz", json={"val": 99})
    assert r.status_code == 400
    assert r.json()["err"] == "range"


def test_unknown_key_returns_400(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    r = client.put("/api/params/no.such", json={"val": 1})
    assert r.status_code == 400
    assert r.json()["err"] == "nokey"


def test_set_while_disconnected_returns_409(client):
    assert client.put("/api/params/led.blink_hz", json={"val": 5}).status_code == 409


def test_save_and_defaults(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    assert client.post("/api/params/save").status_code == 200
    assert client.post("/api/params/defaults").status_code == 200


def test_websocket_receives_telemetry(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    with client.websocket_connect("/ws") as ws:
        # the fake device only speaks when spoken to; nudge it
        client.get("/api/params")
        msg = ws.receive_json()
        assert msg["type"] in ("state", "tlm", "log")
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd app && .venv/bin/pytest tests/test_api.py -v`
Expected: FAIL — `ModuleNotFoundError: No module named 'backend.main'`

- [ ] **Step 3: Write `app/backend/main.py`**

The broadcaster bridge matters: the serial reader is a plain thread and cannot touch the asyncio loop directly. `run_coroutine_threadsafe` is what makes that crossing safe.

```python
"""FastAPI surface. This is the contract an Electron port must reimplement."""
import asyncio
import logging
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from .device import DeviceModel, DeviceError, ProtoMismatch
from .link import list_candidate_ports

log = logging.getLogger(__name__)

WEB_DIR = Path(__file__).resolve().parent.parent / "web"


class ConnectBody(BaseModel):
    port: str


class ValueBody(BaseModel):
    val: int | str


class Broadcaster:
    """Fans messages from the serial reader thread out to WebSocket clients."""

    def __init__(self):
        self._clients: set[WebSocket] = set()
        self._loop: asyncio.AbstractEventLoop | None = None

    def bind(self, loop):
        self._loop = loop

    def add(self, ws):
        self._clients.add(ws)

    def remove(self, ws):
        self._clients.discard(ws)

    def publish_threadsafe(self, msg: dict):
        if self._loop is None:
            return
        if "tlm" in msg:
            payload = {"type": "tlm", "data": msg["tlm"]}
        elif "log" in msg:
            payload = {"type": "log", "data": msg["log"]}
        elif "state" in msg:
            payload = {"type": "state", "data": msg["state"]}
        else:
            payload = {"type": "raw", "data": msg}
        asyncio.run_coroutine_threadsafe(self._fanout(payload), self._loop)

    async def _fanout(self, payload: dict):
        for ws in list(self._clients):
            try:
                await ws.send_json(payload)
            except Exception:
                self._clients.discard(ws)


def create_app(device: DeviceModel | None = None) -> FastAPI:
    device = device or DeviceModel()
    bus = Broadcaster()
    device.subscribe(bus.publish_threadsafe)

    # lifespan, not the deprecated @app.on_event("startup")
    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        bus.bind(asyncio.get_running_loop())
        yield
        device.disconnect()

    app = FastAPI(title="app-demo configurator", lifespan=lifespan)

    @app.exception_handler(DeviceError)
    async def _device_error(_request, exc: DeviceError):
        status = 409 if exc.code == "disconnected" else 400
        if exc.code == "timeout":
            status = 504
        return JSONResponse(status_code=status,
                            content={"err": exc.code, "detail": str(exc)})

    @app.get("/api/ports")
    def ports():
        return list_candidate_ports()

    @app.post("/api/connect")
    def connect(body: ConnectBody):
        try:
            device.connect(body.port)
        except ProtoMismatch as exc:
            raise HTTPException(status_code=502, detail=str(exc)) from exc
        except Exception as exc:
            raise HTTPException(status_code=502, detail=str(exc)) from exc
        return device.status()

    @app.post("/api/disconnect")
    def disconnect():
        device.disconnect()
        return device.status()

    @app.get("/api/status")
    def status():
        return device.status()

    @app.get("/api/schema")
    def schema():
        return device.schema()

    @app.get("/api/params")
    def params():
        return device.values()

    @app.put("/api/params/{key}")
    def set_param(key: str, body: ValueBody):
        device.set(key, body.val)
        return {"ok": True, "key": key, "val": body.val}

    @app.post("/api/params/save")
    def save():
        device.save()
        return {"ok": True}

    @app.post("/api/params/defaults")
    def defaults():
        device.load_defaults()
        return {"ok": True, "vals": device.values()}

    @app.websocket("/ws")
    async def ws_endpoint(ws: WebSocket):
        await ws.accept()
        bus.add(ws)
        await ws.send_json({"type": "state", "data": device.status()})
        try:
            while True:
                await ws.receive_text()   # clients send nothing; this detects close
        except WebSocketDisconnect:
            pass
        finally:
            bus.remove(ws)

    if WEB_DIR.is_dir():
        app.mount("/", StaticFiles(directory=str(WEB_DIR), html=True), name="web")

    return app


app = create_app()
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd app && .venv/bin/pytest tests/test_api.py -v`
Expected: PASS, 9 tests.

- [ ] **Step 5: Run the full Python suite**

Run: `cd app && .venv/bin/pytest -v`
Expected: PASS, 27 tests.

- [ ] **Step 6: Smoke-test against the real board**

With the board plugged in:

```bash
cd app && .venv/bin/uvicorn backend.main:app --port 8080
```

In another terminal:

```bash
curl -s localhost:8080/api/ports
curl -s -X POST localhost:8080/api/connect -H 'Content-Type: application/json' \
     -d '{"port":"/dev/ttyACM0"}'
curl -s localhost:8080/api/schema
curl -s -X PUT localhost:8080/api/params/led.blink_hz \
     -H 'Content-Type: application/json' -d '{"val":10}'   # LED speeds up
curl -s -X PUT localhost:8080/api/params/led.blink_hz \
     -H 'Content-Type: application/json' -d '{"val":99}'   # 400 {"err":"range"}
```

- [ ] **Step 7: Commit**

```bash
git add app/backend/main.py app/tests/test_api.py
git commit -m "feat: http api and websocket telemetry"
```

---

## Task 11: Web UI

Verified by hand — there are no automated tests for the UI by design.

**Files:**
- Create: `app/web/index.html`, `app/web/app.js`, `app/web/vendor/bootstrap.min.css`

**Interfaces:**
- Consumes: the HTTP/WS API from Task 10.
- Produces: nothing consumed by later tasks.

- [ ] **Step 1: Vendor Bootstrap locally**

CDN is not used: the page must work offline and port cleanly into Electron later.

```bash
mkdir -p app/web/vendor
curl -L -o app/web/vendor/bootstrap.min.css \
  https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css
```

- [ ] **Step 2: Write `app/web/index.html`**

```html
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>app-demo configurator</title>
  <link rel="stylesheet" href="vendor/bootstrap.min.css">
</head>
<body class="bg-body-tertiary">
  <nav class="navbar bg-dark border-bottom" data-bs-theme="dark">
    <div class="container-fluid gap-2">
      <span class="navbar-brand mb-0 h1">app-demo</span>
      <select id="port" class="form-select form-select-sm w-auto"></select>
      <button id="connect" class="btn btn-sm btn-primary">Connect</button>
      <span id="state" class="badge text-bg-secondary">disconnected</span>
      <span id="fw" class="text-secondary small ms-auto"></span>
    </div>
  </nav>

  <div class="container py-3">
    <div id="alert" class="alert alert-danger d-none" role="alert"></div>

    <ul class="nav nav-tabs" role="tablist">
      <li class="nav-item"><button class="nav-link active" data-tab="config">Configuration</button></li>
      <li class="nav-item"><button class="nav-link" data-tab="telemetry">Telemetry</button></li>
    </ul>

    <div id="tab-config" class="tab-pane py-3">
      <form id="form" class="row g-3"></form>
      <div class="mt-3 d-flex gap-2 align-items-center">
        <button id="save" class="btn btn-success btn-sm">Save to flash</button>
        <button id="defaults" class="btn btn-outline-secondary btn-sm">Load defaults</button>
        <span id="dirty" class="text-warning small d-none">unsaved changes</span>
      </div>
    </div>

    <div id="tab-telemetry" class="tab-pane py-3 d-none">
      <div id="tlm" class="row g-3"></div>
    </div>
  </div>

  <script src="app.js"></script>
</body>
</html>
```

- [ ] **Step 3: Write `app/web/app.js`**

**Every network call goes through `Api`. That object is the entire Electron porting surface** — keep it that way.

```javascript
'use strict';

const Api = {
  async get(path) {
    const r = await fetch(path);
    if (!r.ok) throw await Api._err(r);
    return r.json();
  },
  async send(method, path, body) {
    const r = await fetch(path, {
      method,
      headers: { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    if (!r.ok) throw await Api._err(r);
    return r.json();
  },
  async _err(r) {
    let detail = r.statusText;
    try { const j = await r.json(); detail = j.detail || j.err || detail; } catch {}
    return new Error(detail);
  },
  ports:     ()          => Api.get('/api/ports'),
  status:    ()          => Api.get('/api/status'),
  schema:    ()          => Api.get('/api/schema'),
  params:    ()          => Api.get('/api/params'),
  connect:   (port)      => Api.send('POST', '/api/connect', { port }),
  disconnect:()          => Api.send('POST', '/api/disconnect'),
  setParam:  (key, val)  => Api.send('PUT', `/api/params/${encodeURIComponent(key)}`, { val }),
  save:      ()          => Api.send('POST', '/api/params/save'),
  defaults:  ()          => Api.send('POST', '/api/params/defaults'),
  socket:    ()          => new WebSocket(`ws://${location.host}/ws`),
};

const el = (id) => document.getElementById(id);
let connected = false;

function showError(msg) {
  const a = el('alert');
  a.textContent = msg;
  a.classList.remove('d-none');
  setTimeout(() => a.classList.add('d-none'), 5000);
}

function setState(state, info) {
  connected = state === 'connected';
  const badge = el('state');
  badge.textContent = state;
  badge.className = 'badge ' + (connected ? 'text-bg-success' : 'text-bg-secondary');
  el('connect').textContent = connected ? 'Disconnect' : 'Connect';
  el('fw').textContent = connected && info && info.fw ? `${info.fw} · proto ${info.proto}` : '';
  el('form').querySelectorAll('input,select').forEach((i) => { i.disabled = !connected; });
}

// --- schema-driven form ----------------------------------------------------
function buildForm(schema, values) {
  const form = el('form');
  form.innerHTML = '';
  for (const p of schema) {
    const col = document.createElement('div');
    col.className = 'col-md-6';

    const label = document.createElement('label');
    label.className = 'form-label';
    label.textContent = p.unit ? `${p.label} (${p.unit})` : p.label;
    label.htmlFor = `f-${p.key}`;

    let input;
    if (p.type === 'enum') {
      input = document.createElement('select');
      input.className = 'form-select';
      for (const opt of p.options) {
        const o = document.createElement('option');
        o.value = o.textContent = opt;
        input.appendChild(o);
      }
    } else if (p.type === 'str') {
      input = document.createElement('input');
      input.type = 'text';
      input.className = 'form-control';
      input.maxLength = p.maxlen;
    } else {
      input = document.createElement('input');
      input.type = 'number';
      input.className = 'form-control';
      input.min = p.min;
      input.max = p.max;
    }

    input.id = `f-${p.key}`;
    input.value = values[p.key];
    input.addEventListener('change', () => onFieldChange(p, input));

    const help = document.createElement('div');
    help.className = 'form-text';
    help.id = `h-${p.key}`;
    help.textContent = p.type === 'u8' ? `${p.min}–${p.max}`
                     : p.type === 'str' ? `max ${p.maxlen} chars` : '';

    col.append(label, input, help);
    form.appendChild(col);
  }
}

async function onFieldChange(spec, input) {
  const raw = input.value;
  const val = spec.type === 'u8' ? Number(raw) : raw;
  try {
    await Api.setParam(spec.key, val);
    input.classList.remove('is-invalid');
    el('dirty').classList.remove('d-none');
  } catch (e) {
    input.classList.add('is-invalid');
    showError(`${spec.label}: ${e.message}`);
  }
}

// --- telemetry -------------------------------------------------------------
const TLM_FIELDS = {
  up: 'Uptime (ms)', clk: 'Clock (MHz)', temp: 'Temp (°C)',
  vdd: 'VDD (mV)', ram: 'Free RAM (B)', btn: 'Button',
};

function renderTelemetry(data) {
  const box = el('tlm');
  if (!box.children.length) {
    for (const [k, label] of Object.entries(TLM_FIELDS)) {
      const col = document.createElement('div');
      col.className = 'col-6 col-md-4';
      col.innerHTML =
        `<div class="card"><div class="card-body py-2">
           <div class="text-secondary small">${label}</div>
           <div class="fs-4" id="t-${k}">–</div>
         </div></div>`;
      box.appendChild(col);
    }
  }
  for (const k of Object.keys(TLM_FIELDS)) {
    if (data[k] !== undefined) {
      const v = typeof data[k] === 'number' ? Math.round(data[k] * 10) / 10 : data[k];
      el(`t-${k}`).textContent = v;
    }
  }
}

// --- wiring ----------------------------------------------------------------
async function refreshPorts() {
  const ports = await Api.ports();
  const sel = el('port');
  sel.innerHTML = '';
  for (const p of ports) {
    const o = document.createElement('option');
    o.value = p.port;
    o.textContent = p.match ? `${p.port} (STM32)` : p.port;
    if (p.match) o.selected = true;
    sel.appendChild(o);
  }
}

async function loadDevice() {
  const [schema, values] = await Promise.all([Api.schema(), Api.params()]);
  buildForm(schema, values);
}

el('connect').addEventListener('click', async () => {
  try {
    if (connected) {
      setState((await Api.disconnect()).state);
    } else {
      const st = await Api.connect(el('port').value);
      setState(st.state, st);
      await loadDevice();
      setState(st.state, st);
    }
  } catch (e) { showError(e.message); }
});

el('save').addEventListener('click', async () => {
  try {
    // Flash erase stalls the board ~1s; telemetry will gap. That is expected.
    await Api.save();
    el('dirty').classList.add('d-none');
  } catch (e) { showError(e.message); }
});

el('defaults').addEventListener('click', async () => {
  try {
    await Api.defaults();
    await loadDevice();
    setState('connected');
  } catch (e) { showError(e.message); }
});

document.querySelectorAll('[data-tab]').forEach((btn) => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('[data-tab]').forEach((b) => b.classList.remove('active'));
    btn.classList.add('active');
    el('tab-config').classList.toggle('d-none', btn.dataset.tab !== 'config');
    el('tab-telemetry').classList.toggle('d-none', btn.dataset.tab !== 'telemetry');
  });
});

function openSocket() {
  const ws = Api.socket();
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.type === 'tlm') renderTelemetry(msg.data);
    else if (msg.type === 'state') {
      const d = msg.data;
      setState(typeof d === 'string' ? d : d.state, typeof d === 'object' ? d : null);
    }
  };
  ws.onclose = () => setTimeout(openSocket, 1000);   // survive backend restarts
}

(async function init() {
  await refreshPorts();
  const st = await Api.status();
  setState(st.state, st);
  if (st.state === 'connected') await loadDevice();
  openSocket();
})();
```

- [ ] **Step 4: Add the disconnect watchdog and port rescan**

The spec's disconnect rule lives here, in the UI. Append to `app/web/app.js`:

```javascript
// --- disconnect watchdog ---------------------------------------------------
// Spec rule: declare a disconnect only after THREE missed telemetry intervals.
// Deliberately slack — a flash save stalls the MCU ~1s and telemetry will gap.
// A tighter threshold would report a false disconnect on every save.
let lastTlmAt = 0;
let tlmPeriodMs = 100;

function noteTelemetry() {
  lastTlmAt = Date.now();
}

function setTelemetryPeriodFrom(values) {
  const hz = Number(values['tlm.rate']) || 10;
  tlmPeriodMs = 1000 / hz;
}

function startWatchdog() {
  setInterval(async () => {
    if (!connected) {
      // While disconnected, keep rescanning so a replugged board reappears.
      try { await refreshPorts(); } catch { /* ignore */ }
      return;
    }
    if (!lastTlmAt || Date.now() - lastTlmAt <= tlmPeriodMs * 3) return;
    try {
      const st = await Api.status();
      setState(st.state, st);
    } catch {
      setState('disconnected');
    }
  }, 1000);
}
```

Then make three small edits to the existing code in the same file:

1. In `renderTelemetry(data)`, add `noteTelemetry();` as the first line.
2. In `loadDevice()`, replace the body with:

```javascript
async function loadDevice() {
  const [schema, values] = await Promise.all([Api.schema(), Api.params()]);
  buildForm(schema, values);
  setTelemetryPeriodFrom(values);
}
```

3. In `onFieldChange`, after a successful `Api.setParam`, keep the telemetry
   period in step when the user changes the rate:

```javascript
    if (spec.key === 'tlm.rate') tlmPeriodMs = 1000 / Number(val);
```

4. In the `init()` IIFE, add `startWatchdog();` immediately after `openSocket();`.

- [ ] **Step 5: Verify by hand in a browser**

```bash
cd app && .venv/bin/uvicorn backend.main:app --port 8080
```

Open `http://localhost:8080` with the board plugged in and confirm:

- Port dropdown pre-selects the STM32 port; Connect populates the form from the schema
- `led.mode` renders as a **select**, `led.blink_hz` as a **number with min/max**, `device.name` as **text**, `tlm.rate` as a **number** — proving the generator handles all four types
- Changing `led.blink_hz` visibly changes the LED
- Entering `99` shows an inline invalid state and a red alert, and the LED does not change
- Telemetry tab updates live; raising `tlm.rate` visibly speeds it up
- **Save does not trip a disconnect** despite the ~1s stall — this is what the 3-interval
  threshold exists for; if the badge flickers here, the watchdog is too tight
- Unplugging the board flips the badge to disconnected and disables the form
- Replugging the board makes it reappear in the port dropdown without a page reload

- [ ] **Step 6: Commit**

```bash
git add app/web/
git commit -m "feat: schema-driven bootstrap web ui"
```

---

## Task 12: API documentation and final verification

**Files:**
- Create: `docs/api.md`
- Modify: `_notes/progress.md`

- [ ] **Step 1: Write `docs/api.md`**

```markdown
# HTTP / WebSocket API

The contract between `app/web/` and the backend. **An Electron port reimplements
exactly this surface in Node; `app/web/` moves across untouched.**

## REST

| Method | Path | Body | Returns |
|---|---|---|---|
| GET | `/api/ports` | — | `[{port, desc, vid, pid, match}]` |
| POST | `/api/connect` | `{"port": "/dev/ttyACM0"}` | status object |
| POST | `/api/disconnect` | — | status object |
| GET | `/api/status` | — | `{state, fw, proto, board}` |
| GET | `/api/schema` | — | array of param descriptors |
| GET | `/api/params` | — | `{key: value}` |
| PUT | `/api/params/{key}` | `{"val": V}` | `{ok, key, val}` |
| POST | `/api/params/save` | — | `{ok}` |
| POST | `/api/params/defaults` | — | `{ok, vals}` |

### Error responses

| Status | Meaning | Body |
|---|---|---|
| 400 | Value rejected | `{"err": "range"\|"enum"\|"toolong"\|"nokey"\|"badtype", "detail": "..."}` |
| 409 | Not connected | `{"err": "disconnected", ...}` |
| 502 | Connect failed / protocol mismatch | `{"detail": "..."}` |
| 504 | Device did not answer in time | `{"err": "timeout", ...}` |

## WebSocket `/ws`

Server pushes only; clients send nothing. Every frame is
`{"type": "tlm"|"state"|"log"|"raw", "data": ...}`.

- `tlm` — `{up, clk, temp, vdd, ram, btn}`
- `state` — status object, or the string `"disconnected"`
- `log` — device log string

A `save` stalls the board ~1s and telemetry will gap. **That is not a
disconnect** — do not treat it as one.
```

- [ ] **Step 2: Run every test in the project**

```bash
cd firmware && ~/.platformio/penv/bin/pio test -e native
cd ../app && .venv/bin/pytest -v
```

Expected: 29 native C++ tests pass, 27 Python tests pass. **Do not proceed if anything fails.**

- [ ] **Step 3: Work through the manual checklist in `_notes/progress.md`**

With the board plugged in and the app running, verify each item and tick it off:

- LED blink rate visibly changes when `led.blink_hz` is set
- `led.mode` off / on / blink all behave (LED is active-low on PC13)
- `save` → power-cycle → values persist
- `defaults` restores values and the UI reflects it
- Telemetry visibly speeds up when `tlm.rate` increases
- Unplugging mid-session shows a disconnect state, not a hang
- A `save` does **not** trigger a false disconnect

- [ ] **Step 4: Update `_notes/progress.md`**

Tick all ten build-status rows, tick the manual checklist, add a session-log entry describing what was built, and record anything that had to deviate from the spec under Deviations.

- [ ] **Step 5: Commit**

```bash
git add docs/api.md _notes/progress.md
git commit -m "docs: api contract and project 1 completion"
```

---

## Definition of done

Project 1 is complete when:

1. `pio test -e native` passes all 29 firmware core tests.
2. `pytest` passes all 27 backend tests.
3. Every manual checklist item in `_notes/progress.md` is ticked.
4. The browser UI connects to the real board, generates its form from the device schema, changes parameters with visible LED feedback, streams telemetry, and persists across a power cycle.

**Then, and only then**, start Project 2 (in-app DFU) with its own brainstorming → spec → plan cycle.
