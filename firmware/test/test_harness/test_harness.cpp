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
