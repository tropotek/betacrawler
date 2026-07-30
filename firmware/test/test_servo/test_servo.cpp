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

// --- clampUs -----------------------------------------------------------------
// Unlike angleToUs's `angle`, this input is NOT already known to be in
// range -- it comes from another module over core::Inputs, so a stale,
// zeroed or out-of-calibration channel must not command the servo past
// min_us/max_us.

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

// --- rephase -----------------------------------------------------------------
// Regression: found on hardware, not by the suite. Changing servo.sweep_s from
// 4 to 10 mid-sweep moved the commanded pulse 1916 -> 1050us in a single step,
// roughly 156 degrees of travel, because position is (elapsed % period) and
// changing the modulus lands somewhere unrelated.

void test_rephase_preserves_quarter_point() {
  // 1000/4000 is a quarter through; a quarter of 10000 is 2500.
  TEST_ASSERT_EQUAL_UINT32(2500, rephase(1000, 4000, 10000));
}

void test_rephase_preserves_half_point() {
  TEST_ASSERT_EQUAL_UINT32(5000, rephase(2000, 4000, 10000));
}

void test_rephase_start_stays_at_start() {
  TEST_ASSERT_EQUAL_UINT32(0, rephase(0, 4000, 10000));
}

void test_rephase_wraps_elapsed_past_one_cycle() {
  // 5000 into a 4000 cycle is phase 1000, i.e. the same quarter point.
  TEST_ASSERT_EQUAL_UINT32(rephase(1000, 4000, 10000), rephase(5000, 4000, 10000));
}

void test_rephase_shortening_the_period_also_works() {
  // Three quarters through 10000 -> three quarters of 4000.
  TEST_ASSERT_EQUAL_UINT32(3000, rephase(7500, 10000, 4000));
}

void test_rephase_same_period_is_identity_within_a_cycle() {
  TEST_ASSERT_EQUAL_UINT32(1234, rephase(1234, 4000, 4000));
}

void test_rephase_degenerate_old_period_is_safe() {
  // Must not divide by zero.
  TEST_ASSERT_EQUAL_UINT32(0, rephase(1000, 0, 10000));
}

void test_rephase_keeps_the_commanded_angle_continuous() {
  // The property that actually matters: the angle either side of a period
  // change is the same, which is what stops the servo jumping.
  const uint32_t elapsed = 1000, oldP = 4000, newP = 10000;
  TEST_ASSERT_EQUAL_UINT8(sweepAngle(elapsed, oldP),
                          sweepAngle(rephase(elapsed, oldP, newP), newP));
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
  RUN_TEST(test_clamp_within_range_passes_through);
  RUN_TEST(test_clamp_below_min_clamps_to_min);
  RUN_TEST(test_clamp_above_max_clamps_to_max);
  RUN_TEST(test_clamp_degenerate_span_holds_one_pulse);
  RUN_TEST(test_sweep_starts_at_zero);
  RUN_TEST(test_sweep_turns_around_at_half_period);
  RUN_TEST(test_sweep_quarter_points_are_centre);
  RUN_TEST(test_sweep_phase_wraps_around_period);
  RUN_TEST(test_sweep_degenerate_period_is_safe);
  RUN_TEST(test_rephase_preserves_quarter_point);
  RUN_TEST(test_rephase_preserves_half_point);
  RUN_TEST(test_rephase_start_stays_at_start);
  RUN_TEST(test_rephase_wraps_elapsed_past_one_cycle);
  RUN_TEST(test_rephase_shortening_the_period_also_works);
  RUN_TEST(test_rephase_same_period_is_identity_within_a_cycle);
  RUN_TEST(test_rephase_degenerate_old_period_is_safe);
  RUN_TEST(test_rephase_keeps_the_commanded_angle_continuous);
  return UNITY_END();
}
