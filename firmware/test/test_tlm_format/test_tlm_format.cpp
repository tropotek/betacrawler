#include <unity.h>
#include <string.h>
#include "core/tlm_format.h"

using namespace core;

// The rules under test are app.js's, deliberately: formatTelemetryValue() does
// `div ? value / div : value` then `toFixed(dec)`. A second renderer that
// rounded differently would show a different number from the browser for the
// same frame, which is exactly the drift this unit exists to prevent.

static TlmDef def(const char* unit, TlmType t, uint16_t div, uint8_t dec) {
  return TlmDef{"k", "L", unit, t, div, dec, nullptr, nullptr};
}

// --- formatTlm --------------------------------------------------------------

void test_u32_plain_appends_unit() {
  char buf[32];
  TlmValue v; v.u = 96;
  formatTlm(def("MHz", TlmType::U32, 0, 0), v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("96 MHz", buf);
}

void test_i32_divided_and_rounded_like_the_browser() {
  // vdd: the wire carries millivolts, the display divides. 3298/1000 to 2dp
  // rounds up, so this pins rounding as well as scaling.
  char buf[32];
  TlmValue v; v.i = 3298;
  formatTlm(def("V", TlmType::I32, 1000, 2), v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("3.30 V", buf);
}

// The panel renders `ram` through this path, so the kB descriptor has to come
// out the same here as it does in the browser: 64896/1024 = 63.375 -> "63.4".
void test_free_ram_renders_as_kilobytes_to_one_place() {
  char buf[32];
  TlmValue v; v.i = 64896;
  formatTlm(def("kB", TlmType::I32, 1024, 1), v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("63.4 kB", buf);
}

void test_f32_rounds_half_away_from_zero() {
  char buf[32];
  TlmValue v; v.f = 34.25f;   // exact in binary; *10 = 342.5 -> 343
  formatTlm(def("C", TlmType::F32, 0, 1), v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("34.3 C", buf);
}

void test_div_of_zero_means_no_division() {
  // app.js writes `def.div ? ... : value`, so 0 and 1 must behave alike.
  char buf[32];
  TlmValue v; v.u = 42;
  formatTlm(def(nullptr, TlmType::U32, 0, 0), v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("42", buf);
}

void test_div_of_one_matches_div_of_zero() {
  char a[32], b[32];
  TlmValue v; v.u = 42;
  formatTlm(def(nullptr, TlmType::U32, 0, 0), v, a, sizeof(a));
  formatTlm(def(nullptr, TlmType::U32, 1, 0), v, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING(a, b);
}

void test_null_unit_leaves_no_trailing_space() {
  char buf[32];
  TlmValue v; v.u = 7;
  size_t n = formatTlm(def(nullptr, TlmType::U32, 0, 0), v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("7", buf);
  TEST_ASSERT_EQUAL(1, n);
}

void test_negative_value_keeps_sign_through_scaling() {
  char buf[32];
  TlmValue v; v.i = -1500;
  formatTlm(def(nullptr, TlmType::I32, 1000, 2), v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("-1.50", buf);
}

void test_negative_fraction_only_keeps_sign() {
  // -50/1000 = -0.05: the integer part is zero, so the sign can only come
  // from the sign flag, not from printing the quotient.
  char buf[32];
  TlmValue v; v.i = -50;
  formatTlm(def(nullptr, TlmType::I32, 1000, 2), v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("-0.05", buf);
}

void test_full_u32_range_survives() {
  // `up` is a raw millisecond counter with no div: the whole u32 range has to
  // print, which rules out doing the arithmetic in int32.
  char buf[32];
  TlmValue v; v.u = 4294967295u;
  formatTlm(def("ms", TlmType::U32, 0, 0), v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("4294967295 ms", buf);
}

void test_truncation_never_overflows_and_stays_terminated() {
  char buf[8];
  memset(buf, 'X', sizeof(buf));
  TlmValue v; v.u = 4294967295u;
  size_t n = formatTlm(def("ms", TlmType::U32, 0, 0), v, buf, sizeof(buf));
  TEST_ASSERT_EQUAL('\0', buf[sizeof(buf) - 1]);
  TEST_ASSERT_TRUE(n < sizeof(buf));
  TEST_ASSERT_EQUAL(strlen(buf), n);
}

void test_zero_length_buffer_is_refused() {
  char buf[4] = {'k', 0, 0, 0};
  TlmValue v; v.u = 1;
  TEST_ASSERT_EQUAL(0, formatTlm(def(nullptr, TlmType::U32, 0, 0), v, buf, 0));
  TEST_ASSERT_EQUAL('k', buf[0]);   // untouched
}

// --- formatUptime -----------------------------------------------------------

void test_uptime_zero() {
  char buf[16];
  formatUptime(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("00:00:00", buf);
}

void test_uptime_sub_minute_pads() {
  char buf[16];
  formatUptime(9000, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("00:00:09", buf);
}

void test_uptime_hours_minutes_seconds() {
  char buf[16];
  formatUptime(3661000, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("01:01:01", buf);
}

void test_uptime_hours_are_not_clamped_to_two_digits() {
  // millis() wraps at ~49.7 days; the display should show the real figure
  // rather than a truncated one right before the wrap.
  char buf[16];
  formatUptime(4294967295u, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1193:02:47", buf);
}

// --- formatIp ---------------------------------------------------------------

void test_format_ip_renders_dotted_decimal() {
  char buf[32];
  size_t n = formatIp(0xC0A80001u, buf, sizeof(buf));   // 192.168.0.1
  TEST_ASSERT_EQUAL_STRING_LEN("192.168.0.1", buf, n);
}

void test_format_ip_zero_renders_all_zeroes() {
  char buf[32];
  size_t n = formatIp(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING_LEN("0.0.0.0", buf, n);
}

void test_formatTlm_dispatches_to_ip_renderer() {
  TlmDef def{"wifi.ip", "IP", nullptr, TlmType::U32, 0, 0, "ip", nullptr, 0, 0};
  TlmValue v; v.u = 0xC0A80001u;
  char buf[48];
  size_t n = formatTlm(def, v, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strstr(buf, "192.168.0.1") != nullptr);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_u32_plain_appends_unit);
  RUN_TEST(test_i32_divided_and_rounded_like_the_browser);
  RUN_TEST(test_free_ram_renders_as_kilobytes_to_one_place);
  RUN_TEST(test_f32_rounds_half_away_from_zero);
  RUN_TEST(test_div_of_zero_means_no_division);
  RUN_TEST(test_div_of_one_matches_div_of_zero);
  RUN_TEST(test_null_unit_leaves_no_trailing_space);
  RUN_TEST(test_negative_value_keeps_sign_through_scaling);
  RUN_TEST(test_negative_fraction_only_keeps_sign);
  RUN_TEST(test_full_u32_range_survives);
  RUN_TEST(test_truncation_never_overflows_and_stays_terminated);
  RUN_TEST(test_zero_length_buffer_is_refused);
  RUN_TEST(test_uptime_zero);
  RUN_TEST(test_uptime_sub_minute_pads);
  RUN_TEST(test_uptime_hours_minutes_seconds);
  RUN_TEST(test_uptime_hours_are_not_clamped_to_two_digits);
  RUN_TEST(test_format_ip_renders_dotted_decimal);
  RUN_TEST(test_format_ip_zero_renders_all_zeroes);
  RUN_TEST(test_formatTlm_dispatches_to_ip_renderer);
  return UNITY_END();
}
