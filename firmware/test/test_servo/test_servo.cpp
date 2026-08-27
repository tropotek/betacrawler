#include <unity.h>
#include "hardware/servo/servo_params.h"

using namespace servo;

// --- clampUs -----------------------------------------------------------------
// The input is NOT already known to be in range -- it comes from another
// module over core::Inputs, so a stale, zeroed or out-of-calibration channel
// must not command the servo past min_us/max_us.

void test_clamp_within_range_passes_through() {
  TEST_ASSERT_EQUAL_UINT16(1500, clampUs(1500, 1000, 2000));
}

void test_clamp_below_min_clamps_to_min() {
  // 0 is what an unset/never-written core::Inputs slot reads as -- well
  // below any real CRSF value (988-2012us).
  TEST_ASSERT_EQUAL_UINT16(1000, clampUs(0, 1000, 2000));
  TEST_ASSERT_EQUAL_UINT16(1000, clampUs(50, 1000, 2000));
}

void test_clamp_above_max_clamps_to_max() {
  TEST_ASSERT_EQUAL_UINT16(2000, clampUs(3000, 1000, 2000));
}

void test_clamp_degenerate_span_holds_one_pulse() {
  // min_us/max_us can meet (see servo.min_us/max_us's own comment on why they
  // cannot cross); this must not divide by zero or misbehave when they do.
  TEST_ASSERT_EQUAL_UINT16(1500, clampUs(0, 1500, 1500));
  TEST_ASSERT_EQUAL_UINT16(1500, clampUs(3000, 1500, 1500));
}

void setUp() {}
void tearDown() {}


// --- applyInvert -------------------------------------------------------------

void test_invert_off_passes_through() {
  TEST_ASSERT_EQUAL_UINT16(1700, applyInvert(1700, 1000, 2000, false));
}

void test_invert_mirrors_about_the_span_midpoint() {
  TEST_ASSERT_EQUAL_UINT16(1300, applyInvert(1700, 1000, 2000, true));
  TEST_ASSERT_EQUAL_UINT16(2000, applyInvert(1000, 1000, 2000, true));
  TEST_ASSERT_EQUAL_UINT16(1500, applyInvert(1500, 1000, 2000, true));
}

void test_invert_handles_an_asymmetric_span() {
  // Midpoint of 1000..2500 is 1750, so 1500 mirrors to 2000.
  TEST_ASSERT_EQUAL_UINT16(2000, applyInvert(1500, 1000, 2500, true));
}

void test_invert_degenerate_span_is_that_one_value() {
  TEST_ASSERT_EQUAL_UINT16(1500, applyInvert(1500, 1500, 1500, true));
}

void test_invert_is_its_own_inverse() {
  TEST_ASSERT_EQUAL_UINT16(1700, applyInvert(applyInvert(1700, 1000, 2000, true), 1000, 2000, true));
}

// --- applyTrim ---------------------------------------------------------------

void test_trim_zero_passes_through() {
  TEST_ASSERT_EQUAL_UINT16(1500, applyTrim(1500, 0, 1000, 2000));
}

void test_trim_offsets_in_both_directions() {
  TEST_ASSERT_EQUAL_UINT16(1560, applyTrim(1500, 60, 1000, 2000));
  TEST_ASSERT_EQUAL_UINT16(1440, applyTrim(1500, -60, 1000, 2000));
}

void test_trim_clamps_into_the_calibrated_range() {
  TEST_ASSERT_EQUAL_UINT16(2000, applyTrim(1950, 200, 1000, 2000));
  TEST_ASSERT_EQUAL_UINT16(1000, applyTrim(1050, -200, 1000, 2000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_clamp_within_range_passes_through);
  RUN_TEST(test_clamp_below_min_clamps_to_min);
  RUN_TEST(test_clamp_above_max_clamps_to_max);
  RUN_TEST(test_clamp_degenerate_span_holds_one_pulse);
  RUN_TEST(test_invert_off_passes_through);
  RUN_TEST(test_invert_mirrors_about_the_span_midpoint);
  RUN_TEST(test_invert_handles_an_asymmetric_span);
  RUN_TEST(test_invert_degenerate_span_is_that_one_value);
  RUN_TEST(test_invert_is_its_own_inverse);
  RUN_TEST(test_trim_zero_passes_through);
  RUN_TEST(test_trim_offsets_in_both_directions);
  RUN_TEST(test_trim_clamps_into_the_calibrated_range);
  return UNITY_END();
}
