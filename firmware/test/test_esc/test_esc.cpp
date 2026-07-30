#include <unity.h>
#include "hardware/esc/esc_params.h"

using namespace esc;

// --- clampUs -----------------------------------------------------------------

void test_clamp_within_range_passes_through() {
  TEST_ASSERT_EQUAL_UINT16(1500, clampUs(1500, 1000, 2000));
}

void test_clamp_below_min_clamps_to_min() {
  TEST_ASSERT_EQUAL_UINT16(1000, clampUs(0, 1000, 2000));
  TEST_ASSERT_EQUAL_UINT16(1000, clampUs(50, 1000, 2000));
}

void test_clamp_above_max_clamps_to_max() {
  TEST_ASSERT_EQUAL_UINT16(2000, clampUs(3000, 1000, 2000));
}

void test_clamp_degenerate_span_holds_one_pulse() {
  TEST_ASSERT_EQUAL_UINT16(1500, clampUs(0, 1500, 1500));
  TEST_ASSERT_EQUAL_UINT16(1500, clampUs(3000, 1500, 1500));
}

// --- nextArmState --------------------------------------------------------------

void test_arm_off_mode_forces_off_from_any_state() {
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_ARMED, true, false, 100, 0, 2000));
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_ARMING, true, false, 100, 0, 2000));
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_OFF, true, false, 100, 0, 2000));
}

void test_arm_entering_from_off_starts_arming() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_OFF, false, true, 5000, 5000, 2000));
}

void test_arm_entering_from_off_never_skips_straight_to_armed() {
  // Even if the caller passes a stale armT0Ms that would already satisfy the
  // hold, the transition call itself must still land on ARMING -- this is
  // the property that guarantees the very first pulse written is always
  // minUs, never a stale commanded value.
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_OFF, false, true, 100000, 0, 2000));
}

void test_arm_before_hold_elapsed_stays_arming() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_ARMING, false, false, 1000, 0, 2000));
}

void test_arm_hold_boundary_is_inclusive() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMING, false, false, 2000, 0, 2000));
}

void test_arm_after_hold_elapsed_becomes_armed() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMING, false, false, 5000, 0, 2000));
}

void test_arm_switching_mode_while_armed_does_not_rearm() {
  // Large, stale-looking elapsed time deliberately: an already-ARMED state
  // must never fall back into a hold just because time has passed. Only
  // ARM_ARMING is subject to the elapsed-time check.
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMED, false, false, 999999, 0, 2000));
}

// --- nextPulseUs --------------------------------------------------------------

void test_pulse_during_arming_is_always_min_regardless_of_mode() {
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_ARMING, MODE_ARMED, 1000, 2000, 1800, 0));
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_ARMING, MODE_INPUT, 1000, 2000, 1000, 1800));
}

void test_pulse_off_arm_state_defaults_to_min() {
  // Defensive: tick()/apply() never call this with ARM_OFF in practice (they
  // return early on MODE_OFF first), but the function's own contract must
  // still hold -- anything other than ARM_ARMED is the safe min pulse.
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_OFF, MODE_ARMED, 1000, 2000, 1800, 0));
}

void test_pulse_armed_mode_clamps_throttle() {
  TEST_ASSERT_EQUAL_UINT16(1500, nextPulseUs(ARM_ARMED, MODE_ARMED, 1000, 2000, 1500, 0));
  TEST_ASSERT_EQUAL_UINT16(2000, nextPulseUs(ARM_ARMED, MODE_ARMED, 1000, 2000, 2500, 0));
}

void test_pulse_input_mode_clamps_bus_value() {
  TEST_ASSERT_EQUAL_UINT16(1800, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 1800));
}

void test_pulse_input_mode_holds_last_on_no_data() {
  TEST_ASSERT_EQUAL_UINT16(0, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 0));
}

void test_pulse_input_mode_holds_last_on_negative() {
  TEST_ASSERT_EQUAL_UINT16(0, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, -5));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_clamp_within_range_passes_through);
  RUN_TEST(test_clamp_below_min_clamps_to_min);
  RUN_TEST(test_clamp_above_max_clamps_to_max);
  RUN_TEST(test_clamp_degenerate_span_holds_one_pulse);
  RUN_TEST(test_arm_off_mode_forces_off_from_any_state);
  RUN_TEST(test_arm_entering_from_off_starts_arming);
  RUN_TEST(test_arm_entering_from_off_never_skips_straight_to_armed);
  RUN_TEST(test_arm_before_hold_elapsed_stays_arming);
  RUN_TEST(test_arm_hold_boundary_is_inclusive);
  RUN_TEST(test_arm_after_hold_elapsed_becomes_armed);
  RUN_TEST(test_arm_switching_mode_while_armed_does_not_rearm);
  RUN_TEST(test_pulse_during_arming_is_always_min_regardless_of_mode);
  RUN_TEST(test_pulse_off_arm_state_defaults_to_min);
  RUN_TEST(test_pulse_armed_mode_clamps_throttle);
  RUN_TEST(test_pulse_input_mode_clamps_bus_value);
  RUN_TEST(test_pulse_input_mode_holds_last_on_no_data);
  RUN_TEST(test_pulse_input_mode_holds_last_on_negative);
  return UNITY_END();
}
