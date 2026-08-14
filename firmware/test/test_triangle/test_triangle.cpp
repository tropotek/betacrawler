#include <unity.h>
#include "core/triangle.h"

using namespace core;

void test_phase_zero_is_off() {
  TEST_ASSERT_EQUAL(0, trianglePercent(0, 1000));
}

void test_phase_quarter_is_half_duty() {
  TEST_ASSERT_EQUAL(50, trianglePercent(250, 1000));
}

void test_phase_half_is_peak() {
  TEST_ASSERT_EQUAL(100, trianglePercent(500, 1000));
}

void test_phase_three_quarter_is_half_duty() {
  TEST_ASSERT_EQUAL(50, trianglePercent(750, 1000));
}

void test_phase_wraps_around_period() {
  TEST_ASSERT_EQUAL(trianglePercent(200, 1000), trianglePercent(1200, 1000));
}

void test_degenerate_period_returns_off() {
  // periodMs/2 == 0: no room for a ramp -- must not divide by zero.
  TEST_ASSERT_EQUAL(0, trianglePercent(0, 1));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_phase_zero_is_off);
  RUN_TEST(test_phase_quarter_is_half_duty);
  RUN_TEST(test_phase_half_is_peak);
  RUN_TEST(test_phase_three_quarter_is_half_duty);
  RUN_TEST(test_phase_wraps_around_period);
  RUN_TEST(test_degenerate_period_returns_off);
  return UNITY_END();
}
