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
