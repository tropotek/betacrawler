#include <unity.h>
#include "hardware/servo/servo_params.h"

using namespace servo;

// --- angleToUs ---------------------------------------------------------------

void test_angle_zero_is_min_us() {
  TEST_ASSERT_EQUAL_UINT16(1000, angleToUs(0, 1000, 2000));
}

void test_angle_centre_is_midpoint() {
  TEST_ASSERT_EQUAL_UINT16(1500, angleToUs(90, 1000, 2000));
}

void test_angle_max_is_max_us() {
  TEST_ASSERT_EQUAL_UINT16(2000, angleToUs(180, 1000, 2000));
}

void test_angle_above_range_is_clamped() {
  // Params range-checks first, so this only fires if a caller bypasses it --
  // but the arithmetic must not run off the end of the pulse range if it does.
  TEST_ASSERT_EQUAL_UINT16(2000, angleToUs(255, 1000, 2000));
}

void test_asymmetric_calibration_maps_proportionally() {
  // 800 + (2200-800) * 90/180 = 1500
  TEST_ASSERT_EQUAL_UINT16(1500, angleToUs(90, 800, 2200));
  TEST_ASSERT_EQUAL_UINT16(800,  angleToUs(0, 800, 2200));
  TEST_ASSERT_EQUAL_UINT16(2200, angleToUs(180, 800, 2200));
}

void test_degenerate_span_holds_one_pulse() {
  // min_us tops out at 1500 and max_us starts at 1500, so the bounds cannot
  // cross -- but they CAN meet. That must be a fixed pulse, not a divide by
  // zero: the division is by 180, never by the span.
  TEST_ASSERT_EQUAL_UINT16(1500, angleToUs(0, 1500, 1500));
  TEST_ASSERT_EQUAL_UINT16(1500, angleToUs(90, 1500, 1500));
  TEST_ASSERT_EQUAL_UINT16(1500, angleToUs(180, 1500, 1500));
}

// --- sweepAngle --------------------------------------------------------------

void test_sweep_starts_at_zero() {
  TEST_ASSERT_EQUAL_UINT8(0, sweepAngle(0, 4000));
}

void test_sweep_turns_around_at_half_period() {
  TEST_ASSERT_EQUAL_UINT8(180, sweepAngle(2000, 4000));
}

void test_sweep_quarter_points_are_centre() {
  TEST_ASSERT_EQUAL_UINT8(90, sweepAngle(1000, 4000));
  TEST_ASSERT_EQUAL_UINT8(90, sweepAngle(3000, 4000));
}

void test_sweep_phase_wraps_around_period() {
  TEST_ASSERT_EQUAL_UINT8(sweepAngle(1000, 4000), sweepAngle(5000, 4000));
}

void test_sweep_degenerate_period_is_safe() {
  // breathingDuty returns 0 when the period leaves no room for a ramp.
  TEST_ASSERT_EQUAL_UINT8(0, sweepAngle(0, 1));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_angle_zero_is_min_us);
  RUN_TEST(test_angle_centre_is_midpoint);
  RUN_TEST(test_angle_max_is_max_us);
  RUN_TEST(test_angle_above_range_is_clamped);
  RUN_TEST(test_asymmetric_calibration_maps_proportionally);
  RUN_TEST(test_degenerate_span_holds_one_pulse);
  RUN_TEST(test_sweep_starts_at_zero);
  RUN_TEST(test_sweep_turns_around_at_half_period);
  RUN_TEST(test_sweep_quarter_points_are_centre);
  RUN_TEST(test_sweep_phase_wraps_around_period);
  RUN_TEST(test_sweep_degenerate_period_is_safe);
  return UNITY_END();
}
