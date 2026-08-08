// Two registries on purpose:
//
//   fakeReg  -- TWO synthetic modules, each with a driver attached. Protocol
//               behaviour (set/get/save/defaults) is identical for any module
//               set, and a fake is the only way to assert "the hardware was
//               called exactly once" natively, since real drivers are
//               Arduino-only. Two rather than one because a single module's
//               paramBase is 0, which makes global and module-local indices
//               numerically identical and therefore makes the single invariant
//               CLAUDE.md cares most about untestable.
//
//   realReg  -- built by registerModules(), i.e. the actual board config from
//               include/boards/blackpill_f411ce.h. Used for the things that
//               MUST reflect the shipping device: the schema, the golden
//               fixture the Python tests load, and hello's module list.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include "core/dispatch.h"
#include "core/protocol.h"
#include "core/registry.h"

using namespace core;

// --- test doubles ----------------------------------------------------------
static const char* const kFakeModes[] = {"off", "on", "blink", "fade"};

enum : uint8_t { P_RATE = 0, P_MODE = 1 };

static const ParamDef kFakeParams[] = {
  {"fake.rate", ParamType::U8,   "Rate", "Hz",    1, 20, nullptr,    0, 0, 2, nullptr, nullptr},
  {"fake.mode", ParamType::Enum, "Mode", nullptr, 0, 0,  kFakeModes, 4, 0, 2, nullptr, nullptr},
};
static const TlmDef kFakeTlm[] = {
  {"f.up",  "Uptime", "ms",    TlmType::U32, 0, 0, nullptr, nullptr, 0, 0},
  // lo/hi declare a range for a renderer that draws a proportion. Nothing in
  // core/ knows what is being proportioned -- it carries the numbers only.
  {"f.pos", "Pos",    "\xC2\xB5s", TlmType::U32, 0, 0, "bar", nullptr, 988, 2012},
};
static const ModuleDesc kFakeDesc = {"fake", "Fake", kFakeParams, 2, kFakeTlm, 2};

// A SECOND module, registered after the first, so its paramBase is 2 rather
// than 0. That offset is the whole point of it: with only kFakeDesc present,
// Registry::notify() could hand drivers the GLOBAL index instead of the
// module-local one and every assertion in this file would still pass -- while
// on the real five-module board a revert would drive the LED module with
// index 3 and the display with index 0, misapplying every parameter.
//
// Deliberately no telemetry (tlm = nullptr, tlmCount = 0): the telemetry
// assertions here are about kFakeDesc's single field, and a second module
// contributing frame entries would only make them read as being about
// something they are not.
enum : uint8_t { P2_LEVEL = 0, P2_NAME = 1 };

static const ParamDef kFake2Params[] = {
  // fake2.level carries a showIf so the serializer has something to emit.
  // The condition names a param in the OTHER fake module on purpose: showIf
  // is resolved by the browser against the whole form, not within a module.
  // The condition is "fade" and fake.mode DEFAULTS to index 2, "blink" -- so
  // fake2.level starts hidden. That is deliberate: the settable-while-hidden
  // test below is only meaningful if the param is actually hidden.
  {"fake2.level", ParamType::U8,  "Level", nullptr, 0, 9, nullptr, 0, 0, 4, nullptr, nullptr,
   "fake.mode", "fade"},
  {"fake2.name",  ParamType::Str, "Name",  nullptr, 0, 0, nullptr, 0, 8, 0, "two",   nullptr,
   nullptr, nullptr},
};
static const ModuleDesc kFake2Desc = {"fake2", "Fake2", kFake2Params, 2, nullptr, 0};

struct MockDriver : Module {
  int     calls = 0;
  uint8_t lastLocal = 0xFF;
  int32_t lastNum = -1;
  // Every index this driver was handed, in order. `calls` alone proves a
  // module was resynced; only these prove it was resynced with its OWN
  // indices.
  uint8_t locals[FW_MAX_PARAMS] = {};
  uint8_t localCount = 0;

  void onParamChanged(uint8_t local, const Params& p) override {
    ++calls;
    lastLocal = local;
    lastNum = p.num(globalParam(local));
    if (localCount < FW_MAX_PARAMS) locals[localCount++] = local;
  }
  void readTelemetry(TlmValue* out) override { out[0].u = 1204; out[1].u = 1500; }

  void reset() { calls = 0; lastLocal = 0xFF; lastNum = -1; localCount = 0; }
};

struct MockStore : Persistence {
  int  saveCalls = 0;
  bool saveOk = true;
  // `load` used to hardcode false. It now replays whatever save() captured,
  // so a revert test can prove the values came back from the STORE rather
  // than from loadDefaults() coincidentally producing the same numbers.
  int   loadCalls = 0;
  bool  hasStored = false;
  Value stored[FW_MAX_PARAMS] = {};

  bool save(const Params& p) override {
    ++saveCalls;
    if (!saveOk) return false;
    memcpy(stored, p.raw(), sizeof(stored));
    hasStored = true;
    return true;
  }
  bool load(Params* p) override {
    ++loadCalls;
    if (!hasStored) return false;      // nothing valid stored, as on a fresh board
    memcpy(p->rawMutable(), stored, sizeof(stored));
    return true;
  }
};

// The reboot-to-bootloader seam. Counting calls is the point: a `dfu` op must
// arm the reboot exactly once, and a refused one must not touch it at all.
struct MockBootloader : Bootloader {
  bool available = true;
  int  enterCalls = 0;
  bool supported() const override { return available; }
  bool enterDfu() override { ++enterCalls; return available; }
};

// The SSID-scan seam. Counting calls is the point, same reasoning as
// MockBootloader: an armed scan must call startScan() exactly once, and a
// refused one (already scanning, or unsupported) must not re-arm it.
struct MockWifiScanner : WifiScanner {
  bool available = true;    // false models "already scanning" here --
                             // there is no separate "unsupported" flag
                             // because an absent seam (nullptr) already
                             // covers that case, see the no-scanner test.
  int  startCalls = 0;
  bool startScan() override { ++startCalls; return available; }
};

static Registry fakeReg;
static Registry realReg;
static MockDriver driver;    // owns globals 0..1
static MockDriver driver2;   // owns globals 2..3, i.e. paramBase 2

static char out[kMaxLineOut];

// --- protocol behaviour (fake module) ---------------------------------------

void test_set_applies_to_hardware_exactly_once() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":1,\"op\":\"set\",\"key\":\"fake.rate\",\"val\":5}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT(1, driver.calls);
  TEST_ASSERT_EQUAL_UINT8(P_RATE, driver.lastLocal);
  TEST_ASSERT_EQUAL_INT32(5, driver.lastNum);
  TEST_ASSERT_EQUAL_INT(0, driver2.calls);   // and only the owning module
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":1"));
}

// The mirror of the above, on the module whose paramBase is NOT 0. Setting the
// second of fake2's parameters is global index 3 and local index 1, and the
// driver must see 1.
void test_set_on_a_later_module_uses_that_modules_local_index() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":30,\"op\":\"set\",\"key\":\"fake2.name\",\"val\":\"hi\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT(0, driver.calls);
  TEST_ASSERT_EQUAL_INT(1, driver2.calls);
  TEST_ASSERT_EQUAL_UINT8(P2_NAME, driver2.lastLocal);
  TEST_ASSERT_EQUAL_STRING("hi", p.str(3));   // global 3 == fake2's local 1
}

void test_rejected_set_does_not_touch_hardware() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":2,\"op\":\"set\",\"key\":\"fake.rate\",\"val\":99}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT(0, driver.calls);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":false"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"err\":\"range\""));
}

void test_set_enum_by_name_forwards_index_to_hardware() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":10,\"op\":\"set\",\"key\":\"fake.mode\",\"val\":\"fade\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT(1, driver.calls);
  TEST_ASSERT_EQUAL_UINT8(P_MODE, driver.lastLocal);
  TEST_ASSERT_EQUAL_INT32(3, driver.lastNum);   // hardware sees the index, not the name
}

void test_set_unknown_key_returns_nokey() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":3,\"op\":\"set\",\"key\":\"no.such\",\"val\":1}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(0, driver.calls);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"err\":\"nokey\""));
}

void test_getall_returns_every_value() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":6,\"op\":\"getall\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fake.mode\":\"blink\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fake.rate\":2"));
}

void test_save_delegates_to_persistence() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":7,\"op\":\"save\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_EQUAL_INT(1, store.saveCalls);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));
}

void test_save_failure_reported() {
  Params p(fakeReg); MockStore store;
  store.saveOk = false;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":8,\"op\":\"save\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":false"));
}

void test_defaults_restores_and_notifies_every_param() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);
  p.setNum(P_RATE, 15);
  driver.reset(); driver2.reset();

  Request q = parseRequest("{\"id\":9,\"op\":\"defaults\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT32(2, p.num(P_RATE));
  // Every parameter resynced, each on its own module -- the two counts add up
  // to fakeReg.paramCount(), which a single-module registry could not tell
  // apart from "one driver got them all".
  TEST_ASSERT_EQUAL_INT(kFakeDesc.paramCount,  driver.calls);
  TEST_ASSERT_EQUAL_INT(kFake2Desc.paramCount, driver2.calls);
  TEST_ASSERT_EQUAL_INT(fakeReg.paramCount(), driver.calls + driver2.calls);
}

void test_tlm_op_toggles_streaming() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);
  TEST_ASSERT_TRUE(d.telemetryEnabled());   // on by default

  Request off = parseRequest("{\"id\":10,\"op\":\"tlm\",\"on\":false}");
  d.handle(off, out, sizeof(out));
  TEST_ASSERT_FALSE(d.telemetryEnabled());
}

void test_bad_request_still_gets_a_reply() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":11,\"op\":\"bogus\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"err\":\"badop\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":11"));
}

void test_telemetry_frame_is_built_from_the_registry() {
  TlmValue vals[FW_MAX_TLM];
  fakeReg.collectTelemetry(vals);
  size_t n = writeTelemetry(out, sizeof(out), fakeReg, vals);

  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"tlm\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"f.up\":1204"));
  TEST_ASSERT_NULL(strstr(out, "\"id\""));   // id-less: this is what makes interleaving safe
}

void test_log_line_is_unsolicited_and_well_formed() {
  size_t n = writeLog(out, sizeof(out), "display: ST7789 240x240 init=138ms");

  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"log\":\"display: ST7789 240x240 init=138ms\"}", out);
  // id-less, exactly like telemetry -- that is what makes the backend treat
  // it as unsolicited (app/backend/protocol.py's is_log).
  TEST_ASSERT_NULL(strstr(out, "\"id\""));
}

void test_log_escapes_characters_that_would_break_the_line() {
  // A driver builds these with snprintf from board macros; one stray quote
  // must not be able to desync the host's line parser.
  size_t n = writeLog(out, sizeof(out), "say \"hi\"\\done");

  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING("{\"log\":\"say \\\"hi\\\"\\\\done\"}", out);
}

void test_log_refuses_to_emit_a_truncated_line() {
  // Truncating JSON would produce a line the host cannot parse, which is
  // worse than dropping the message: emit nothing instead.
  char small[16];
  size_t n = writeLog(small, sizeof(small),
                      "a message far longer than the buffer allows");

  TEST_ASSERT_EQUAL(0, n);
  TEST_ASSERT_EQUAL_STRING("", small);
}

// --- the real board config ---------------------------------------------------

void test_hello_reports_proto_version() {
  Params p(realReg); MockStore store;
  Dispatcher d(realReg, p, store);

  Request q = parseRequest("{\"id\":4,\"op\":\"hello\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"proto\":1"));
  TEST_ASSERT_NOT_NULL(strstr(out, "blackpill_f411ce"));
}

// The identity fields all come from include/config.h + the board header, so
// this is really a check that BOARD_HEADER was wired through the build --
// hardcoded values here would have kept passing after the config.h refactor.
void test_hello_reports_build_identity_from_config() {
  Params p(realReg); MockStore store;
  Dispatcher d(realReg, p, store);

  Request q = parseRequest("{\"id\":12,\"op\":\"hello\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"betacrawler\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ver\":\"1.0.0\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"board\":\"blackpill_f411ce\""));
  // `fw` must survive as a display string -- app.js and docs/api.md read it.
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fw\":\"betacrawler 1.0.0\""));
  // Exact build timestamp is unassertable; that it is present and non-empty
  // is the part that can actually regress.
  TEST_ASSERT_NOT_NULL(strstr(out, "\"built\":\""));
  TEST_ASSERT_NULL(strstr(out, "\"built\":\"\""));
}

void test_hello_lists_the_enabled_modules() {
  Params p(realReg); MockStore store;
  Dispatcher d(realReg, p, store);

  Request q = parseRequest("{\"id\":13,\"op\":\"hello\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(
      strstr(out, "\"mods\":[\"device\",\"system\",\"button\",\"led\"]"));
}

void test_schema_lists_all_params_and_fits_buffer() {
  Params p(realReg); MockStore store;
  Dispatcher d(realReg, p, store);

  Request q = parseRequest("{\"id\":5,\"op\":\"schema\"}");
  size_t n = d.handle(q, out, sizeof(out));

  TEST_ASSERT_TRUE(n > 0);
  // serializeJson() writes at most cap-1 bytes and returns that count even
  // on overflow/truncation, so `n < kMaxLineOut` alone would still pass for
  // a silently truncated result. Tighten to cap-2 so a real truncation
  // (which lands exactly on cap-1) actually fails this test.
  TEST_ASSERT_TRUE(n < kMaxLineOut - 1);   // must not truncate
  TEST_ASSERT_NOT_NULL(strstr(out, "led.mode"));
  TEST_ASSERT_NOT_NULL(strstr(out, "led.blink_hz"));
  TEST_ASSERT_NOT_NULL(strstr(out, "device.name"));
  TEST_ASSERT_NOT_NULL(strstr(out, "tlm.rate"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"options\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"unit\":\"Hz\""));
}

// Every parameter and telemetry field carries a group, so the UI never has to
// invent a heading -- and tlm.rate proves the per-item override reaches the
// wire, not just the registry.
void test_schema_carries_groups_and_the_telemetry_descriptor() {
  Params p(realReg); MockStore store;
  Dispatcher d(realReg, p, store);

  Request q = parseRequest("{\"id\":14,\"op\":\"schema\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_NOT_NULL(strstr(out, "\"group\":\"LED\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"group\":\"Device\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"group\":\"Telemetry\""));   // tlm.rate's override
  TEST_ASSERT_NOT_NULL(strstr(out, "\"group\":\"System\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"group\":\"Button\""));
  // vdd is the field that proves display hints survive: millivolts on the
  // wire, volts in the browser.
  TEST_ASSERT_NOT_NULL(strstr(out, "\"key\":\"vdd\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"div\":1000"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"dec\":2"));
}

// `fmt` is the third display hint, alongside div/dec: a named renderer the UI
// looks up, for the values a divisor and a decimal count cannot express. The
// wire still carries raw milliseconds -- only the rendering changes.
void test_schema_uptime_carries_the_hms_format_hint() {
  Params p(realReg); MockStore store;
  Dispatcher d(realReg, p, store);

  Request q = parseRequest("{\"id\":15,\"op\":\"schema\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_NOT_NULL(strstr(out, "\"key\":\"up\",\"label\":\"Uptime\",\"fmt\":\"hms\""));
}

// A bar needs a range, and the range has to come from the descriptor. If it
// came from app.js instead, that file would have to know what an RC channel
// is -- exactly the feature knowledge the schema-driven design exists to keep
// out of it.
void test_schema_carries_a_declared_range_when_one_is_set() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":21,\"op\":\"schema\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_NOT_NULL(strstr(out, "\"fmt\":\"bar\",\"lo\":988,\"hi\":2012"));
}

// Omitted when unset, so adding the field costs no bytes on the eleven
// existing descriptors and their golden JSON stays byte-identical.
void test_schema_omits_the_range_when_lo_equals_hi() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":22,\"op\":\"schema\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_NULL(strstr(out, "\"key\":\"f.up\",\"label\":\"Uptime\",\"unit\":\"ms\",\"lo\""));
}

// Free RAM is a heap figure in the tens of kilobytes; bytes is more precision
// than a human reading a dashboard can use. Same wire value, declared for
// display as kB with one decimal place.
void test_schema_declares_free_ram_in_kilobytes() {
  Params p(realReg); MockStore store;
  Dispatcher d(realReg, p, store);

  Request q = parseRequest("{\"id\":16,\"op\":\"schema\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_NOT_NULL(
      strstr(out, "\"key\":\"ram\",\"label\":\"Free RAM\",\"unit\":\"kB\",\"div\":1024,\"dec\":1"));
}

void test_schema_carries_show_if_when_a_param_declares_one() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);
  Request q = parseRequest("{\"id\":1,\"op\":\"schema\"}");
  size_t n = d.handle(q, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(
      strstr(out, "\"showIf\":{\"key\":\"fake.mode\",\"val\":\"fade\"}"));
}

void test_schema_omits_show_if_for_unconditional_params() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);
  Request q = parseRequest("{\"id\":1,\"op\":\"schema\"}");
  size_t n = d.handle(q, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  // Exactly one param declares one, so exactly one object may appear. An
  // unconditional param emitting "showIf":{"key":null,...} would cost bytes
  // in the response that is already the largest thing this firmware sends.
  const char* first = strstr(out, "\"showIf\"");
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NULL(strstr(first + 1, "\"showIf\""));
}

void test_a_show_if_hidden_param_is_still_settable() {
  // showIf is a DISPLAY hint. fake.mode defaults to "blink" and the condition
  // asks for "fade", so fake2.level is NOT currently drawn -- and must still
  // be settable, because Terminal `set` and an INI restore both go through
  // this path and neither knows what the browser is rendering. This test is
  // the thing that stops showIf drifting into an access rule.
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);
  Request q = parseRequest("{\"id\":2,\"op\":\"set\",\"key\":\"fake2.level\",\"val\":7}");
  size_t n = d.handle(q, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));
  ParamId level;
  TEST_ASSERT_TRUE(fakeReg.findParam("fake2.level", &level));
  TEST_ASSERT_EQUAL_INT32(7, p.num(level));
}

// A tiny, throwaway registry for exactly the two tests below -- kept
// separate from fakeReg/kFakeParams on purpose, see this task's own note:
// touching the shared fixture would shift driver2's paramBase and ripple
// through every index-sensitive test elsewhere in this file.
static const ParamDef kSecretParams[] = {
  {"x.rate", ParamType::U8,  "Rate", "Hz",    1, 20, nullptr, 0, 0, 2, nullptr, nullptr},
  {"x.pass", ParamType::Str, "Pass", nullptr, 0, 0,  nullptr, 0, 8, 0, "",      nullptr,
   nullptr, nullptr, true},
};
static Registry secretReg;   // populated in main() before tests run

void test_schema_marks_a_secret_param() {
  Params p(secretReg);
  MockStore store;
  Dispatcher d(secretReg, p, store);

  Request q = parseRequest("{\"id\":30,\"op\":\"schema\"}");
  size_t n = d.handle(q, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);

  // Search from x.pass's own object, not the whole line, so this can't
  // false-pass by matching a `"secret"` key belonging to a different param.
  const char* pass = strstr(out, "\"x.pass\"");
  TEST_ASSERT_NOT_NULL(pass);
  TEST_ASSERT_NOT_NULL(strstr(pass, "\"secret\":true"));
}

void test_schema_omits_secret_key_when_unset() {
  Params p(secretReg);
  MockStore store;
  Dispatcher d(secretReg, p, store);

  Request q = parseRequest("{\"id\":31,\"op\":\"schema\"}");
  size_t n = d.handle(q, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);

  const char* rate = strstr(out, "\"x.rate\"");
  TEST_ASSERT_NOT_NULL(rate);
  const char* nextField = strstr(rate, "\"x.pass\"");
  TEST_ASSERT_NOT_NULL(nextField);
  // "secret" must not appear anywhere between x.rate's object and the next
  // param's -- it is a non-secret U8, so the key is omitted entirely (same
  // "emitted only when declared" rule showIf already follows).
  std::string slice(rate, nextField - rate);
  TEST_ASSERT_NULL(strstr(slice.c_str(), "\"secret\""));
}

// Golden fixture: app/tests/test_device.py loads this exact file instead of
// hand-typing a Python SCHEMA literal, so a firmware schema change (e.g. a
// bumped `max`) that isn't reflected here becomes a visible Python failure
// instead of silently validating against a stale bound at runtime.
//
// It is built from `realReg` -- the board header's actual module set -- which
// is the whole reason the native environment compiles against a real board
// header and every module ships its descriptor in a pure, Arduino-free
// translation unit.
//
// Mechanics: this test (re)writes the fixture from a *real* Dispatcher::handle
// call every run, then -- if a fixture was already checked in -- asserts the
// freshly generated bytes are byte-identical to what was on disk. That gives
// two properties for free: (1) no separate generator script or comparison
// tool to keep in sync with the serialization logic, and (2) a firmware
// change that alters the schema JSON without the fixture being regenerated
// and re-committed fails THIS test (not just leaves stale content sitting
// there for someone to notice via `git diff`). The tradeoff is that a
// forgotten `git add` after a legitimate schema change still requires a
// second `pio test -e native` run (which rewrites the file to match) before
// it goes green again -- acceptable since both suites are run at the end of
// every task per project convention anyway.
void test_schema_golden_fixture_matches_firmware() {
  Params p(realReg); MockStore store;
  Dispatcher d(realReg, p, store);

  Request q = parseRequest("{\"id\":20,\"op\":\"schema\"}");
  size_t n = d.handle(q, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);

  const char* path = "test/golden/schema.json";

  std::string prev;
  bool hadPrev = false;
  if (FILE* rf = fopen(path, "rb")) {
    hadPrev = true;
    char rbuf[kMaxLineOut];
    size_t rn = fread(rbuf, 1, sizeof(rbuf), rf);
    prev.assign(rbuf, rn);
    fclose(rf);
  }

  FILE* wf = fopen(path, "wb");
  TEST_ASSERT_NOT_NULL(wf);
  fwrite(out, 1, n, wf);
  fclose(wf);

  if (hadPrev) {
    TEST_ASSERT_EQUAL_STRING_LEN(out, prev.c_str(), n);
    TEST_ASSERT_EQUAL_INT(n, prev.size());
  }
}

// --- reboot to DFU -----------------------------------------------------------

void test_dfu_op_arms_the_bootloader_exactly_once() {
  Params p(fakeReg); MockStore store; MockBootloader boot;
  Dispatcher d(fakeReg, p, store, &boot);

  Request q = parseRequest("{\"id\":20,\"op\":\"dfu\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT(1, boot.enterCalls);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));
}

// The response has to be produced BEFORE the MCU resets, or the host cannot
// tell a successful reboot from a board that died. The seam only arms; main.cpp
// resets afterwards. This asserts the op still answers with a complete,
// well-formed line.
void test_dfu_op_answers_before_any_reset() {
  Params p(fakeReg); MockStore store; MockBootloader boot;
  Dispatcher d(fakeReg, p, store, &boot);

  Request q = parseRequest("{\"id\":21,\"op\":\"dfu\"}");
  size_t n = d.handle(q, out, sizeof(out));

  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":21"));
  TEST_ASSERT_EQUAL_CHAR('}', out[n - 1]);
}

void test_dfu_op_on_a_board_without_support_reports_nodfu() {
  Params p(fakeReg); MockStore store; MockBootloader boot;
  boot.available = false;
  Dispatcher d(fakeReg, p, store, &boot);

  Request q = parseRequest("{\"id\":22,\"op\":\"dfu\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":false"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"err\":\"nodfu\""));
}

// FEATURE_DFU off wires no Bootloader at all. The op must still be answered
// rather than crash on a null seam.
void test_dfu_op_with_no_bootloader_wired_reports_nodfu() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);          // no bootloader argument

  Request q = parseRequest("{\"id\":23,\"op\":\"dfu\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_NOT_NULL(strstr(out, "\"err\":\"nodfu\""));
}

void test_hello_advertises_dfu_in_caps_when_supported() {
  Params p(realReg); MockStore store; MockBootloader boot;
  Dispatcher d(realReg, p, store, &boot);

  Request q = parseRequest("{\"id\":24,\"op\":\"hello\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"caps\":[\"dfu\"]"));
}

void test_hello_caps_is_empty_without_dfu_support() {
  Params p(realReg); MockStore store; MockBootloader boot;
  boot.available = false;
  Dispatcher d(realReg, p, store, &boot);

  Request q = parseRequest("{\"id\":25,\"op\":\"hello\"}");
  d.handle(q, out, sizeof(out));
  // Present but empty, not absent: the app reads a missing `caps` the same
  // way, but an empty array is the honest answer for firmware that HAS the
  // field and simply cannot do it.
  TEST_ASSERT_NOT_NULL(strstr(out, "\"caps\":[]"));
  TEST_ASSERT_NULL(strstr(out, "\"dfu\""));
}

// hello's existing shape is a contract (app.js, docs/api.md, the Python
// tests). `caps` is additive and must not have disturbed it.
void test_hello_keeps_its_existing_fields_alongside_caps() {
  Params p(realReg); MockStore store; MockBootloader boot;
  Dispatcher d(realReg, p, store, &boot);

  Request q = parseRequest("{\"id\":26,\"op\":\"hello\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fw\":\"betacrawler 1.0.0\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"proto\":1"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"mods\":["));
}

// A `dfu` request must not be mistaken for anything else, and a near-miss
// must still not resolve to Dfu.
void test_dfu_is_parsed_as_its_own_op() {
  Request q = parseRequest("{\"id\":27,\"op\":\"dfu\"}");
  TEST_ASSERT_TRUE(q.ok);
  TEST_ASSERT_EQUAL_INT((int)Op::Dfu, (int)q.op);

  Request bad = parseRequest("{\"id\":28,\"op\":\"dfuu\"}");
  TEST_ASSERT_FALSE(bad.ok);
  TEST_ASSERT_EQUAL_STRING("badop", bad.err);
}

// --- revert -----------------------------------------------------------------
// The last stored point lives in flash and the device owns it. These assert
// the two outcomes callers must be able to tell apart, and that the hardware
// is resynced either way.

void test_revert_restores_the_values_last_saved() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  p.setNum(P_RATE, 9);
  Request save = parseRequest("{\"id\":1,\"op\":\"save\"}");
  d.handle(save, out, sizeof(out));

  p.setNum(P_RATE, 15);              // an unsaved edit, to be discarded

  Request q = parseRequest("{\"id\":2,\"op\":\"revert\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT32(9, p.num(P_RATE));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"src\":\"flash\""));
}

void test_revert_notifies_every_param_so_hardware_resyncs() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request save = parseRequest("{\"id\":3,\"op\":\"save\"}");
  d.handle(save, out, sizeof(out));
  p.setNum(P_RATE, 15);
  driver.reset(); driver2.reset();

  Request q = parseRequest("{\"id\":4,\"op\":\"revert\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT(kFakeDesc.paramCount,  driver.calls);
  TEST_ASSERT_EQUAL_INT(kFake2Desc.paramCount, driver2.calls);
  TEST_ASSERT_EQUAL_INT(fakeReg.paramCount(), driver.calls + driver2.calls);
}

// The other half of that invariant, and the half a one-module registry cannot
// express: each driver is handed ITS OWN local indices, not global ones.
// fake2's paramBase is 2, so a Registry::notify() that forwarded the global
// index would hand driver2 {2, 3} here instead of {0, 1}.
void test_revert_hands_each_module_its_own_local_indices() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request save = parseRequest("{\"id\":9,\"op\":\"save\"}");
  d.handle(save, out, sizeof(out));
  driver.reset(); driver2.reset();

  Request q = parseRequest("{\"id\":10,\"op\":\"revert\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_UINT8(kFakeDesc.paramCount,  driver.localCount);
  TEST_ASSERT_EQUAL_UINT8(kFake2Desc.paramCount, driver2.localCount);
  for (uint8_t i = 0; i < kFakeDesc.paramCount; ++i)
    TEST_ASSERT_EQUAL_UINT8(i, driver.locals[i]);
  for (uint8_t i = 0; i < kFake2Desc.paramCount; ++i)
    TEST_ASSERT_EQUAL_UINT8(i, driver2.locals[i]);
}

void test_revert_with_nothing_stored_falls_back_to_defaults() {
  Params p(fakeReg); MockStore store;    // never saved
  Dispatcher d(fakeReg, p, store);
  p.setNum(P_RATE, 15);
  driver.reset(); driver2.reset();

  Request q = parseRequest("{\"id\":5,\"op\":\"revert\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT32(2, p.num(P_RATE));            // the ParamDef default
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));     // never an error
  TEST_ASSERT_NOT_NULL(strstr(out, "\"src\":\"defaults\""));
  TEST_ASSERT_EQUAL_INT(fakeReg.paramCount(), driver.calls + driver2.calls);
}

void test_revert_consults_the_store_exactly_once() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);

  Request q = parseRequest("{\"id\":6,\"op\":\"revert\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT(1, store.loadCalls);
}

void test_revert_is_parsed_as_its_own_op() {
  Request q = parseRequest("{\"id\":7,\"op\":\"revert\"}");
  TEST_ASSERT_TRUE(q.ok);
  TEST_ASSERT_TRUE(q.op == Op::Revert);

  Request bad = parseRequest("{\"id\":8,\"op\":\"revertx\"}");
  TEST_ASSERT_FALSE(bad.ok);
}

// --- SSID scan ----------------------------------------------------------

void test_wifiscan_op_arms_the_scanner_exactly_once() {
  Params p(fakeReg); MockStore store; MockWifiScanner scanner;
  Dispatcher d(fakeReg, p, store);
  d.setWifiScanner(&scanner);

  Request q = parseRequest("{\"id\":40,\"op\":\"wifiscan\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT(1, scanner.startCalls);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));
}

void test_wifiscan_op_reports_busy_when_scanner_refuses() {
  Params p(fakeReg); MockStore store; MockWifiScanner scanner;
  scanner.available = false;
  Dispatcher d(fakeReg, p, store);
  d.setWifiScanner(&scanner);

  Request q = parseRequest("{\"id\":41,\"op\":\"wifiscan\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":false"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"err\":\"busy\""));
}

// FEATURE_WIFI off wires no scanner at all -- the op must still answer
// rather than dereference a null seam, exactly the dfu precedent.
void test_wifiscan_op_with_no_scanner_wired_reports_nowifi() {
  Params p(fakeReg); MockStore store;
  Dispatcher d(fakeReg, p, store);          // setWifiScanner() never called

  Request q = parseRequest("{\"id\":42,\"op\":\"wifiscan\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_NOT_NULL(strstr(out, "\"err\":\"nowifi\""));
}

void test_hello_advertises_wifiscan_in_caps_when_wired() {
  Params p(realReg); MockStore store; MockWifiScanner scanner;
  Dispatcher d(realReg, p, store);
  d.setWifiScanner(&scanner);

  Request q = parseRequest("{\"id\":43,\"op\":\"hello\"}");
  d.handle(q, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"wifiscan\""));
}

void setUp() { driver.reset(); driver2.reset(); }
void tearDown() {}

int main() {
  // Order matters: kFake2Desc must be added SECOND so its paramBase is 2.
  fakeReg.add(kFakeDesc, &driver);
  fakeReg.add(kFake2Desc, &driver2);
  registerModules(realReg);
  // Initialize secretReg for the secret hint tests.
  secretReg.add(ModuleDesc{"x", "X", kSecretParams, 2, nullptr, 0});

  UNITY_BEGIN();
  RUN_TEST(test_set_applies_to_hardware_exactly_once);
  RUN_TEST(test_set_on_a_later_module_uses_that_modules_local_index);
  RUN_TEST(test_rejected_set_does_not_touch_hardware);
  RUN_TEST(test_set_enum_by_name_forwards_index_to_hardware);
  RUN_TEST(test_set_unknown_key_returns_nokey);
  RUN_TEST(test_getall_returns_every_value);
  RUN_TEST(test_save_delegates_to_persistence);
  RUN_TEST(test_save_failure_reported);
  RUN_TEST(test_defaults_restores_and_notifies_every_param);
  RUN_TEST(test_tlm_op_toggles_streaming);
  RUN_TEST(test_bad_request_still_gets_a_reply);
  RUN_TEST(test_telemetry_frame_is_built_from_the_registry);
  RUN_TEST(test_log_line_is_unsolicited_and_well_formed);
  RUN_TEST(test_log_escapes_characters_that_would_break_the_line);
  RUN_TEST(test_log_refuses_to_emit_a_truncated_line);
  RUN_TEST(test_hello_reports_proto_version);
  RUN_TEST(test_hello_reports_build_identity_from_config);
  RUN_TEST(test_hello_lists_the_enabled_modules);
  RUN_TEST(test_schema_lists_all_params_and_fits_buffer);
  RUN_TEST(test_schema_carries_groups_and_the_telemetry_descriptor);
  RUN_TEST(test_schema_uptime_carries_the_hms_format_hint);
  RUN_TEST(test_schema_carries_a_declared_range_when_one_is_set);
  RUN_TEST(test_schema_omits_the_range_when_lo_equals_hi);
  RUN_TEST(test_schema_declares_free_ram_in_kilobytes);
  RUN_TEST(test_schema_carries_show_if_when_a_param_declares_one);
  RUN_TEST(test_schema_omits_show_if_for_unconditional_params);
  RUN_TEST(test_a_show_if_hidden_param_is_still_settable);
  RUN_TEST(test_schema_marks_a_secret_param);
  RUN_TEST(test_schema_omits_secret_key_when_unset);
  RUN_TEST(test_schema_golden_fixture_matches_firmware);
  RUN_TEST(test_dfu_op_arms_the_bootloader_exactly_once);
  RUN_TEST(test_dfu_op_answers_before_any_reset);
  RUN_TEST(test_dfu_op_on_a_board_without_support_reports_nodfu);
  RUN_TEST(test_dfu_op_with_no_bootloader_wired_reports_nodfu);
  RUN_TEST(test_hello_advertises_dfu_in_caps_when_supported);
  RUN_TEST(test_hello_caps_is_empty_without_dfu_support);
  RUN_TEST(test_hello_keeps_its_existing_fields_alongside_caps);
  RUN_TEST(test_dfu_is_parsed_as_its_own_op);
  RUN_TEST(test_revert_restores_the_values_last_saved);
  RUN_TEST(test_revert_notifies_every_param_so_hardware_resyncs);
  RUN_TEST(test_revert_hands_each_module_its_own_local_indices);
  RUN_TEST(test_revert_with_nothing_stored_falls_back_to_defaults);
  RUN_TEST(test_revert_consults_the_store_exactly_once);
  RUN_TEST(test_revert_is_parsed_as_its_own_op);
  RUN_TEST(test_wifiscan_op_arms_the_scanner_exactly_once);
  RUN_TEST(test_wifiscan_op_reports_busy_when_scanner_refuses);
  RUN_TEST(test_wifiscan_op_with_no_scanner_wired_reports_nowifi);
  RUN_TEST(test_hello_advertises_wifiscan_in_caps_when_wired);
  return UNITY_END();
}
