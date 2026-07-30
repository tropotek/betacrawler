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
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_ARMED, true, false, 100, 0, 2000, true));
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_ARMING, true, false, 100, 0, 2000, true));
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_OFF, true, false, 100, 0, 2000, true));
}

void test_arm_entering_from_off_starts_arming() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_OFF, false, true, 5000, 5000, 2000, true));
}

void test_arm_entering_from_off_never_skips_straight_to_armed() {
  // Even if the caller passes a stale armT0Ms that would already satisfy the
  // hold, the transition call itself must still land on ARMING -- this is
  // the property that guarantees the very first pulse written is always
  // minUs, never a stale commanded value.
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_OFF, false, true, 100000, 0, 2000, true));
}

void test_arm_before_hold_elapsed_stays_arming() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_ARMING, false, false, 1000, 0, 2000, true));
}

void test_arm_hold_boundary_is_inclusive() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMING, false, false, 2000, 0, 2000, true));
}

void test_arm_after_hold_elapsed_becomes_armed() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMING, false, false, 5000, 0, 2000, true));
}

void test_arm_switching_mode_while_armed_does_not_rearm() {
  // Large, stale-looking elapsed time deliberately: an already-ARMED state
  // must never fall back into a hold just because time has passed. Only
  // ARM_ARMING is subject to the elapsed-time check.
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMED, false, false, 999999, 0, 2000, true));
}

// --- nextPulseUs --------------------------------------------------------------

void test_pulse_during_arming_is_always_min_regardless_of_mode() {
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_ARMING, MODE_ARMED, 1000, 2000, 1800, 0, false));
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_ARMING, MODE_INPUT, 1000, 2000, 1000, 1800, false));
}

void test_pulse_off_arm_state_defaults_to_min() {
  // Defensive: tick()/apply() never call this with ARM_OFF in practice (they
  // return early on MODE_OFF first), but the function's own contract must
  // still hold -- anything other than ARM_ARMED is the safe min pulse.
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_OFF, MODE_ARMED, 1000, 2000, 1800, 0, false));
}

void test_pulse_armed_mode_clamps_throttle() {
  TEST_ASSERT_EQUAL_UINT16(1500, nextPulseUs(ARM_ARMED, MODE_ARMED, 1000, 2000, 1500, 0, false));
  TEST_ASSERT_EQUAL_UINT16(2000, nextPulseUs(ARM_ARMED, MODE_ARMED, 1000, 2000, 2500, 0, false));
}

void test_pulse_input_mode_clamps_bus_value() {
  TEST_ASSERT_EQUAL_UINT16(1800, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 1800, false));
}

void test_pulse_input_mode_holds_last_on_no_data() {
  TEST_ASSERT_EQUAL_UINT16(0, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 0, false));
}

void test_pulse_input_mode_holds_last_on_negative() {
  TEST_ASSERT_EQUAL_UINT16(0, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, -5, false));
}

// --- nextArmState: the low-throttle arm precondition ---------------------

void test_arm_hold_elapsed_but_not_low_stays_arming() {
  // Time alone is not enough once commandedIsLow can be false: the hold has
  // fully elapsed here (5000ms >= 2000ms), but the operator has not brought
  // the throttle down, so promotion must not happen.
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_ARMING, false, false, 5000, 0, 2000, false));
}

void test_arm_promotes_only_when_both_elapsed_and_low() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMING, false, false, 2000, 0, 2000, true));
}

void test_arm_already_armed_ignores_commanded_low() {
  // The precondition gates the INITIAL promotion only -- once ARMED, the
  // full commanded range is available, unchanged from before this
  // amendment. commandedIsLow=false here must not demote anything.
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMED, false, false, 999999, 0, 2000, false));
}

// --- isCommandedLow ------------------------------------------------------

void test_commanded_low_armed_mode_checks_throttle_against_margin() {
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1000, 0, 1000, 50));   // exactly min_us
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1040, 0, 1000, 50));   // within margin
  TEST_ASSERT_FALSE(isCommandedLow(MODE_ARMED, 1060, 0, 1000, 50));  // outside margin
}

void test_commanded_low_input_mode_requires_confirmed_reading() {
  TEST_ASSERT_TRUE(isCommandedLow(MODE_INPUT, 0, 1020, 1000, 50));
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 1800, 1000, 50));
  // inputUs <= 0 is "no data", never "confirmed low" -- must not read as
  // low enough to arm just because it is numerically small.
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 0, 1000, 50));
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, -5, 1000, 50));
}

// --- nextInputWatch --------------------------------------------------------

void test_input_watch_first_sample_is_a_change() {
  InputWatch w = nextInputWatch(InputWatch{0, 0}, 1500, 1000);
  TEST_ASSERT_EQUAL_INT16(1500, w.lastUs);
  TEST_ASSERT_EQUAL_UINT32(1000, w.lastChangeMs);
}

void test_input_watch_unchanged_value_keeps_old_timestamp() {
  InputWatch w = nextInputWatch(InputWatch{1500, 1000}, 1500, 5000);
  TEST_ASSERT_EQUAL_UINT32(1000, w.lastChangeMs);
}

void test_input_watch_changed_value_resets_timestamp() {
  InputWatch w = nextInputWatch(InputWatch{1500, 1000}, 1510, 5000);
  TEST_ASSERT_EQUAL_INT16(1510, w.lastUs);
  TEST_ASSERT_EQUAL_UINT32(5000, w.lastChangeMs);
}

// --- nextPulseUs: stale input forces min_us -------------------------------

void test_pulse_stale_input_forces_min_even_with_a_plausible_value() {
  // 1800 looks like a perfectly valid throttle reading -- inputStale is the
  // only thing distinguishing "live signal, happens to read 1800" from "the
  // link died with 1800 as the last frame". Must force minUs regardless.
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 1800, true));
}

void test_pulse_stale_check_precedes_no_data_check() {
  // inputUs<=0 alone means "hold last pulse" (returns 0), but inputStale
  // must take priority and force an active minUs write instead.
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 0, true));
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
  RUN_TEST(test_arm_hold_elapsed_but_not_low_stays_arming);
  RUN_TEST(test_arm_promotes_only_when_both_elapsed_and_low);
  RUN_TEST(test_arm_already_armed_ignores_commanded_low);
  RUN_TEST(test_commanded_low_armed_mode_checks_throttle_against_margin);
  RUN_TEST(test_commanded_low_input_mode_requires_confirmed_reading);
  RUN_TEST(test_input_watch_first_sample_is_a_change);
  RUN_TEST(test_input_watch_unchanged_value_keeps_old_timestamp);
  RUN_TEST(test_input_watch_changed_value_resets_timestamp);
  RUN_TEST(test_pulse_stale_input_forces_min_even_with_a_plausible_value);
  RUN_TEST(test_pulse_stale_check_precedes_no_data_check);
  return UNITY_END();
}
