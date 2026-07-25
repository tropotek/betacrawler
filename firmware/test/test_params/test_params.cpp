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
