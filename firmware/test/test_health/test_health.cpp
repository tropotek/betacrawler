#include <unity.h>
#include <string.h>
#include "core/health.h"
#include "core/boot_log.h"

using namespace core;

void test_starts_healthy() {
  Health h;
  TEST_ASSERT_TRUE(h.ok());
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Fault::None, (uint8_t)h.fault());
}

void test_records_a_fault() {
  Health h;
  h.fail(Fault::Registry);
  TEST_ASSERT_FALSE(h.ok());
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Fault::Registry, (uint8_t)h.fault());
}

void test_first_fault_wins() {
  Health h;
  h.fail(Fault::Registry);
  h.fail(Fault::Panic);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Fault::Registry, (uint8_t)h.fault());
}

void test_failing_with_none_is_ignored() {
  Health h;
  h.fail(Fault::None);
  TEST_ASSERT_TRUE(h.ok());
}

void test_reset_clears_the_fault() {
  Health h;
  h.fail(Fault::Registry);
  h.reset();
  TEST_ASSERT_TRUE(h.ok());
}

void test_fault_writes_one_boot_log_line_naming_it() {
  bootLog().clear();
  health().reset();
  health().fail(Fault::Registry);
  TEST_ASSERT_EQUAL_UINT8(1, bootLog().count());
  TEST_ASSERT_NOT_NULL(strstr(bootLog().line(0), "fault"));
  TEST_ASSERT_NOT_NULL(strstr(bootLog().line(0), "registry"));
  bootLog().clear();
  health().reset();
}

void test_repeated_failure_logs_once() {
  bootLog().clear();
  health().reset();
  health().fail(Fault::Registry);
  health().fail(Fault::Registry);
  TEST_ASSERT_EQUAL_UINT8(1, bootLog().count());
  bootLog().clear();
  health().reset();
}

void test_shared_instance_is_the_same_object() {
  health().reset();
  health().fail(Fault::Panic);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)Fault::Panic, (uint8_t)health().fault());
  health().reset();
  bootLog().clear();
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_healthy);
  RUN_TEST(test_records_a_fault);
  RUN_TEST(test_first_fault_wins);
  RUN_TEST(test_failing_with_none_is_ignored);
  RUN_TEST(test_reset_clears_the_fault);
  RUN_TEST(test_fault_writes_one_boot_log_line_naming_it);
  RUN_TEST(test_repeated_failure_logs_once);
  RUN_TEST(test_shared_instance_is_the_same_object);
  return UNITY_END();
}
