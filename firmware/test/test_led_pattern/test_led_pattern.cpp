#include <unity.h>
#include "core/led_pattern.h"

using namespace core;

static const uint16_t kHealthy[] = {950, 50};
static const uint16_t kFault[]   = {100, 100};
static const uint16_t kTwoBlink[] = {150, 150, 150, 600};

void test_starts_on() {
  Pattern p{kHealthy, 2};
  TEST_ASSERT_TRUE(patternState(p, 0));
}

void test_stays_on_through_the_first_step() {
  Pattern p{kHealthy, 2};
  TEST_ASSERT_TRUE(patternState(p, 949));
}

void test_turns_off_for_the_second_step() {
  Pattern p{kHealthy, 2};
  TEST_ASSERT_FALSE(patternState(p, 950));
  TEST_ASSERT_FALSE(patternState(p, 999));
}

void test_wraps_after_the_full_cycle() {
  Pattern p{kHealthy, 2};
  TEST_ASSERT_TRUE(patternState(p, 1000));
  TEST_ASSERT_FALSE(patternState(p, 1950));
}

void test_wraps_many_cycles_later() {
  Pattern p{kHealthy, 2};
  TEST_ASSERT_TRUE(patternState(p, 1000u * 1000u));
  TEST_ASSERT_FALSE(patternState(p, 1000u * 1000u + 950u));
}

void test_even_duty_pattern_alternates() {
  Pattern p{kFault, 2};
  TEST_ASSERT_TRUE(patternState(p, 0));
  TEST_ASSERT_FALSE(patternState(p, 100));
  TEST_ASSERT_TRUE(patternState(p, 200));
}

void test_four_step_pattern_alternates_on_off_on_off() {
  Pattern p{kTwoBlink, 4};
  TEST_ASSERT_TRUE(patternState(p, 0));      // step 0, on
  TEST_ASSERT_FALSE(patternState(p, 150));   // step 1, off
  TEST_ASSERT_TRUE(patternState(p, 300));    // step 2, on
  TEST_ASSERT_FALSE(patternState(p, 450));   // step 3, off (the 600ms pause)
  TEST_ASSERT_TRUE(patternState(p, 1050));   // wrapped back to step 0
}

void test_empty_pattern_is_off() {
  Pattern p{kHealthy, 0};
  TEST_ASSERT_FALSE(patternState(p, 0));
}

void test_null_pattern_is_off() {
  Pattern p{nullptr, 2};
  TEST_ASSERT_FALSE(patternState(p, 0));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_on);
  RUN_TEST(test_stays_on_through_the_first_step);
  RUN_TEST(test_turns_off_for_the_second_step);
  RUN_TEST(test_wraps_after_the_full_cycle);
  RUN_TEST(test_wraps_many_cycles_later);
  RUN_TEST(test_even_duty_pattern_alternates);
  RUN_TEST(test_four_step_pattern_alternates_on_off_on_off);
  RUN_TEST(test_empty_pattern_is_off);
  RUN_TEST(test_null_pattern_is_off);
  return UNITY_END();
}
