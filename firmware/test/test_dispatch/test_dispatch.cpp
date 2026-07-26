// Two registries on purpose:
//
//   fakeReg  -- a synthetic module with a driver attached. Protocol behaviour
//               (set/get/save/defaults) is identical for any module set, and a
//               fake is the only way to assert "the hardware was called
//               exactly once" natively, since real drivers are Arduino-only.
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
  {"f.up", "Uptime", "ms", TlmType::U32, 0, 0, nullptr},
};
static const ModuleDesc kFakeDesc = {"fake", "Fake", kFakeParams, 2, kFakeTlm, 1};

struct MockDriver : Module {
  int     calls = 0;
  uint8_t lastLocal = 0xFF;
  int32_t lastNum = -1;
  void onParamChanged(uint8_t local, const Params& p) override {
    ++calls;
    lastLocal = local;
    lastNum = p.num(globalParam(local));
  }
  void readTelemetry(TlmValue* out) override { out[0].u = 1204; }
};

struct MockStore : Persistence {
  int  saveCalls = 0;
  bool saveOk = true;
  bool save(const Params&) override { ++saveCalls; return saveOk; }
  bool load(Params*) override { return false; }
};

static Registry fakeReg;
static Registry realReg;
static MockDriver driver;

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
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ok\":true"));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":1"));
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
  driver.calls = 0;

  Request q = parseRequest("{\"id\":9,\"op\":\"defaults\"}");
  d.handle(q, out, sizeof(out));

  TEST_ASSERT_EQUAL_INT32(2, p.num(P_RATE));
  TEST_ASSERT_EQUAL_INT(fakeReg.paramCount(), driver.calls);  // hardware resynced
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

  TEST_ASSERT_NOT_NULL(strstr(out, "\"name\":\"app-demo\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"ver\":\"1.0.0\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"board\":\"blackpill_f411ce\""));
  // `fw` must survive as a display string -- app.js and docs/api.md read it.
  TEST_ASSERT_NOT_NULL(strstr(out, "\"fw\":\"app-demo 1.0.0\""));
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
  TEST_ASSERT_NOT_NULL(strstr(out, "\"mods\":[\"device\",\"system\",\"button\",\"led\"]"));
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

void setUp() { driver.calls = 0; }
void tearDown() {}

int main() {
  fakeReg.add(kFakeDesc, &driver);
  registerModules(realReg);

  UNITY_BEGIN();
  RUN_TEST(test_set_applies_to_hardware_exactly_once);
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
  RUN_TEST(test_hello_reports_proto_version);
  RUN_TEST(test_hello_reports_build_identity_from_config);
  RUN_TEST(test_hello_lists_the_enabled_modules);
  RUN_TEST(test_schema_lists_all_params_and_fits_buffer);
  RUN_TEST(test_schema_carries_groups_and_the_telemetry_descriptor);
  RUN_TEST(test_schema_golden_fixture_matches_firmware);
  return UNITY_END();
}
