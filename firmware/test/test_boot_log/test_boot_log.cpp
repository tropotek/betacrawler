#include <unity.h>
#include <string.h>
#include "core/boot_log.h"

using namespace core;

void test_records_and_returns_lines_in_order() {
  BootLog b;
  b.add("first");
  b.add("second");
  TEST_ASSERT_EQUAL_UINT8(2, b.count());
  TEST_ASSERT_EQUAL_STRING("first", b.line(0));
  TEST_ASSERT_EQUAL_STRING("second", b.line(1));
}

void test_starts_empty() {
  BootLog b;
  TEST_ASSERT_EQUAL_UINT8(0, b.count());
  TEST_ASSERT_FALSE(b.dropped());
}

void test_truncates_an_overlong_line_rather_than_overflowing() {
  BootLog b;
  char big[BootLog::kMaxLen * 2];
  memset(big, 'x', sizeof(big));
  big[sizeof(big) - 1] = '\0';
  b.add(big);
  TEST_ASSERT_EQUAL_UINT8(1, b.count());
  TEST_ASSERT_EQUAL_UINT(BootLog::kMaxLen - 1, strlen(b.line(0)));
}

void test_drops_past_capacity_and_says_so() {
  // Silently losing a boot diagnostic would be worse than admitting the
  // buffer filled -- the whole point is that this is the only record.
  BootLog b;
  for (int i = 0; i < BootLog::kMaxLines + 3; ++i) b.add("line");
  TEST_ASSERT_EQUAL_UINT8(BootLog::kMaxLines, b.count());
  TEST_ASSERT_TRUE(b.dropped());
}

void test_out_of_range_line_is_empty_not_garbage() {
  BootLog b;
  b.add("only");
  TEST_ASSERT_EQUAL_STRING("", b.line(1));
  TEST_ASSERT_EQUAL_STRING("", b.line(200));
}

void test_null_message_is_ignored() {
  BootLog b;
  b.add(nullptr);
  TEST_ASSERT_EQUAL_UINT8(0, b.count());
}

void test_shared_instance_is_the_same_object() {
  bootLog().clear();
  bootLog().add("via accessor");
  TEST_ASSERT_EQUAL_UINT8(1, bootLog().count());
  TEST_ASSERT_EQUAL_STRING("via accessor", bootLog().line(0));
  bootLog().clear();
  TEST_ASSERT_EQUAL_UINT8(0, bootLog().count());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_empty);
  RUN_TEST(test_records_and_returns_lines_in_order);
  RUN_TEST(test_truncates_an_overlong_line_rather_than_overflowing);
  RUN_TEST(test_drops_past_capacity_and_says_so);
  RUN_TEST(test_out_of_range_line_is_empty_not_garbage);
  RUN_TEST(test_null_message_is_ignored);
  RUN_TEST(test_shared_instance_is_the_same_object);
  return UNITY_END();
}
