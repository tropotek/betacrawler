#include <unity.h>
#include "core/types.h"

void test_constants_are_sane() {
  TEST_ASSERT_EQUAL(31, core::kMaxStrLen);
  TEST_ASSERT_EQUAL(256, core::kMaxLineIn);
  TEST_ASSERT_EQUAL(7168, core::kMaxLineOut);
  TEST_ASSERT_EQUAL(1, core::kProtoVersion);
}

// The parameter count is no longer a compile-time constant -- modules
// contribute it at boot -- but the static ceilings it must fit inside still
// are, and a build whose caps were shrunk below the registry's needs would
// fail at add() time on the board rather than here. Pin them so that change
// is at least deliberate.
void test_capacity_limits_are_sane() {
  TEST_ASSERT_TRUE(FW_MAX_MODULES >= 4);
  TEST_ASSERT_TRUE(FW_MAX_PARAMS >= 8);
  TEST_ASSERT_TRUE(FW_MAX_TLM >= 8);
  // ParamId is a uint8_t index and kNoParam is its sentinel, so the table can
  // never grow to where a valid index would collide with "not found".
  TEST_ASSERT_TRUE(FW_MAX_PARAMS < core::kNoParam);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_constants_are_sane);
  RUN_TEST(test_capacity_limits_are_sane);
  return UNITY_END();
}
