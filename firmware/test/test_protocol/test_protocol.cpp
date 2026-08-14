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
  Request q = parseRequest("{\"id\":3,\"op\":\"set\",\"key\":\"rx.deadband_us\",\"val\":5}");
  TEST_ASSERT_TRUE(q.ok);
  TEST_ASSERT_EQUAL(Op::Set, q.op);
  TEST_ASSERT_EQUAL_STRING("rx.deadband_us", q.key);
  TEST_ASSERT_TRUE(q.hasNum);
  TEST_ASSERT_FALSE(q.hasStr);
  TEST_ASSERT_EQUAL_INT32(5, q.num);
}

void test_parse_set_string() {
  Request q = parseRequest("{\"id\":4,\"op\":\"set\",\"key\":\"rx.protocol\",\"val\":\"elrs\"}");
  TEST_ASSERT_TRUE(q.ok);
  TEST_ASSERT_TRUE(q.hasStr);
  TEST_ASSERT_FALSE(q.hasNum);
  TEST_ASSERT_EQUAL_STRING("elrs", q.str);
}

void test_parse_set_string_too_long_rejected() {
  // kMaxStrLen is 31, so a 40-character string should be rejected
  Request q = parseRequest("{\"id\":5,\"op\":\"set\",\"key\":\"device.name\",\"val\":\"1234567890123456789012345678901234567890\"}");
  TEST_ASSERT_FALSE(q.ok);
  TEST_ASSERT_EQUAL_STRING("toolong", q.err);
  TEST_ASSERT_FALSE(q.hasStr);  // nothing was written into q.str
  TEST_ASSERT_EQUAL_UINT32(5, q.id);  // id preserved so we can still reply
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

void test_parses_wifiscan_op() {
  Request q = parseRequest("{\"id\":1,\"op\":\"wifiscan\"}");
  TEST_ASSERT_TRUE(q.ok);
  TEST_ASSERT_TRUE(Op::WifiScan == q.op);
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
  RUN_TEST(test_parse_set_string_too_long_rejected);
  RUN_TEST(test_parse_tlm_carries_no_rate);
  RUN_TEST(test_malformed_json_rejected);
  RUN_TEST(test_unknown_op_rejected);
  RUN_TEST(test_parses_wifiscan_op);
  return UNITY_END();
}
