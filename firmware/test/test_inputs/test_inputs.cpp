#include <unity.h>
#include "core/inputs.h"

using namespace core;

void test_unset_slot_reads_zero() {
  Inputs in;
  TEST_ASSERT_EQUAL_INT16(0, in.get(0));
  TEST_ASSERT_EQUAL_INT16(0, in.get(15));
}

void test_set_then_get_round_trips() {
  Inputs in;
  in.set(3, 1500);
  TEST_ASSERT_EQUAL_INT16(1500, in.get(3));
  // Untouched slots stay zero -- proves set() writes exactly one slot.
  TEST_ASSERT_EQUAL_INT16(0, in.get(2));
  TEST_ASSERT_EQUAL_INT16(0, in.get(4));
}

void test_out_of_range_get_returns_zero_not_garbage() {
  Inputs in;
  in.set(0, 999);
  TEST_ASSERT_EQUAL_INT16(0, in.get(16));
  TEST_ASSERT_EQUAL_INT16(0, in.get(255));
}

void test_out_of_range_set_is_a_no_op() {
  Inputs in;
  in.set(16, 42);     // must not corrupt slot 0 or anything else
  TEST_ASSERT_EQUAL_INT16(0, in.get(0));
}

void test_fresh_ms_starts_at_zero() {
  Inputs in;
  TEST_ASSERT_EQUAL_UINT32(0, in.lastFreshMs());
}

void test_mark_fresh_records_the_timestamp() {
  Inputs in;
  in.markFresh(1234);
  TEST_ASSERT_EQUAL_UINT32(1234, in.lastFreshMs());
}

void test_mark_fresh_overwrites_the_previous_timestamp() {
  Inputs in;
  in.markFresh(1000);
  in.markFresh(5000);
  TEST_ASSERT_EQUAL_UINT32(5000, in.lastFreshMs());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_unset_slot_reads_zero);
  RUN_TEST(test_set_then_get_round_trips);
  RUN_TEST(test_out_of_range_get_returns_zero_not_garbage);
  RUN_TEST(test_out_of_range_set_is_a_no_op);
  RUN_TEST(test_fresh_ms_starts_at_zero);
  RUN_TEST(test_mark_fresh_records_the_timestamp);
  RUN_TEST(test_mark_fresh_overwrites_the_previous_timestamp);
  return UNITY_END();
}
