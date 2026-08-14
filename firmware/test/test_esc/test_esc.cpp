#include <unity.h>
#include "hardware/esc/esc_math.h"

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
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_ARMED, true, false, false, 100, 0, 2000, true));
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_ARMING, true, false, false, 100, 0, 2000, true));
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_OFF, true, false, false, 100, 0, 2000, true));
}

void test_arm_switch_inactive_forces_off_from_any_state() {
  // Symmetric with test_arm_off_mode_forces_off_from_any_state above --
  // armSwitchInactive gets the exact same hard-reset treatment as modeIsOff.
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_ARMED, false, true, false, 100, 0, 2000, true));
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_ARMING, false, true, false, 100, 0, 2000, true));
  TEST_ASSERT_EQUAL_UINT32(ARM_OFF, nextArmState(ARM_OFF, false, true, false, 100, 0, 2000, true));
}

void test_arm_switch_active_alone_does_not_promote_to_armed() {
  // armSwitchInactive=false ("switch says armed") is not, by itself, enough
  // to promote ARMING to ARMED -- commandedIsLow still gates that, unchanged.
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_ARMING, false, false, false, 5000, 0, 2000, false));
}

void test_arm_entering_from_off_starts_arming() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_OFF, false, false, true, 5000, 5000, 2000, true));
}

void test_arm_entering_from_off_never_skips_straight_to_armed() {
  // Even if the caller passes a stale armT0Ms that would already satisfy the
  // hold, the transition call itself must still land on ARMING -- this is
  // the property that guarantees the very first pulse written is always
  // minUs, never a stale commanded value.
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_OFF, false, false, true, 100000, 0, 2000, true));
}

void test_arm_before_hold_elapsed_stays_arming() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_ARMING, false, false, false, 1000, 0, 2000, true));
}

void test_arm_hold_boundary_is_inclusive() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMING, false, false, false, 2000, 0, 2000, true));
}

void test_arm_after_hold_elapsed_becomes_armed() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMING, false, false, false, 5000, 0, 2000, true));
}

void test_arm_switching_mode_while_armed_does_not_rearm() {
  // Large, stale-looking elapsed time deliberately: an already-ARMED state
  // must never fall back into a hold just because time has passed. Only
  // ARM_ARMING is subject to the elapsed-time check.
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMED, false, false, false, 999999, 0, 2000, true));
}

// --- nextPulseUs --------------------------------------------------------------

void test_pulse_during_arming_is_always_min_regardless_of_mode() {
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_ARMING, MODE_ARMED, 1000, 2000, 1800, 0, false, 1000));
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_ARMING, MODE_INPUT, 1000, 2000, 1000, 1800, false, 1000));
}

void test_pulse_off_arm_state_defaults_to_min() {
  // Defensive: tick()/apply() never call this with ARM_OFF in practice (they
  // return early on MODE_OFF first), but the function's own contract must
  // still hold -- anything other than ARM_ARMED is the safe min pulse.
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_OFF, MODE_ARMED, 1000, 2000, 1800, 0, false, 1000));
}

void test_pulse_armed_mode_clamps_throttle() {
  TEST_ASSERT_EQUAL_UINT16(1500, nextPulseUs(ARM_ARMED, MODE_ARMED, 1000, 2000, 1500, 0, false, 1000));
  TEST_ASSERT_EQUAL_UINT16(2000, nextPulseUs(ARM_ARMED, MODE_ARMED, 1000, 2000, 2500, 0, false, 1000));
}

void test_pulse_input_mode_clamps_bus_value() {
  TEST_ASSERT_EQUAL_UINT16(1800, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 1800, false, 1000));
}

void test_pulse_input_mode_holds_last_on_no_data() {
  TEST_ASSERT_EQUAL_UINT16(0, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 0, false, 1000));
}

void test_pulse_input_mode_holds_last_on_negative() {
  TEST_ASSERT_EQUAL_UINT16(0, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, -5, false, 1000));
}

// --- nextArmState: the low-throttle arm precondition ---------------------

void test_arm_hold_elapsed_but_not_low_stays_arming() {
  // Time alone is not enough once commandedIsLow can be false: the hold has
  // fully elapsed here (5000ms >= 2000ms), but the operator has not brought
  // the throttle down, so promotion must not happen.
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMING, nextArmState(ARM_ARMING, false, false, false, 5000, 0, 2000, false));
}

void test_arm_promotes_only_when_both_elapsed_and_low() {
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMING, false, false, false, 2000, 0, 2000, true));
}

void test_arm_already_armed_ignores_commanded_low() {
  // The precondition gates the INITIAL promotion only -- once ARMED, the
  // full commanded range is available, unchanged from before this
  // amendment. commandedIsLow=false here must not demote anything.
  TEST_ASSERT_EQUAL_UINT32(ARM_ARMED, nextArmState(ARM_ARMED, false, false, false, 999999, 0, 2000, false));
}

// --- isCommandedLow ------------------------------------------------------

void test_commanded_low_armed_mode_checks_throttle_against_margin() {
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1000, 0, true, 1000, 50, false));   // exactly min_us
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1040, 0, true, 1000, 50, false));   // within margin
  TEST_ASSERT_FALSE(isCommandedLow(MODE_ARMED, 1060, 0, true, 1000, 50, false));  // outside margin
}

void test_commanded_low_input_mode_requires_confirmed_reading() {
  TEST_ASSERT_TRUE(isCommandedLow(MODE_INPUT, 0, 1020, true, 1000, 50, false));
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 1800, true, 1000, 50, false));
  // inputUs <= 0 is "no data", never "confirmed low" -- must not read as
  // low enough to arm just because it is numerically small.
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 0, true, 1000, 50, false));
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, -5, true, 1000, 50, false));
}

// --- isCommandedLow: freshness gating (MODE_INPUT only) ---------------------

void test_commanded_low_input_mode_requires_freshness_too() {
  // A confirmed-low reading (200 <= 1050) that is NOT fresh must still fail
  // -- arming must never complete against a link already known dead.
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 1020, false, 1000, 50, false));
}

void test_commanded_low_armed_mode_ignores_freshness() {
  // MODE_ARMED has no bus input at all -- inputFresh must have no effect.
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1000, 0, false, 1000, 50, false));
}

// --- isLinkFresh -------------------------------------------------------------

void test_link_fresh_within_window() {
  TEST_ASSERT_TRUE(isLinkFresh(1000, 1400, 500));
}

void test_link_stale_at_boundary() {
  // Exactly at the window edge counts as stale -- matches nextArmState's
  // own >= convention for its elapsed-time check.
  TEST_ASSERT_FALSE(isLinkFresh(1000, 1500, 500));
}

void test_link_stale_well_past_window() {
  TEST_ASSERT_FALSE(isLinkFresh(1000, 999999, 500));
}

void test_link_fresh_never_marked_is_stale_from_the_start() {
  // lastFreshMs=0 (core::Inputs' own default, never written) at any real
  // nowMs must read as stale -- "never proven alive" is not "fresh".
  TEST_ASSERT_FALSE(isLinkFresh(0, 1000, 500));
}

void test_link_fresh_zero_is_stale_even_within_the_window() {
  // Without the dedicated zero-check, (100 - 0) = 100 < 500 would wrongly
  // read as fresh -- this is the case the existing boundary test cannot
  // distinguish from ordinary elapsed-time math.
  TEST_ASSERT_FALSE(isLinkFresh(0, 100, 500));
}

// --- inputLossDemotesArmed ---------------------------------------------------

void test_stale_link_demotes_an_armed_input_session() {
  TEST_ASSERT_TRUE(inputLossDemotesArmed(ARM_ARMED, MODE_INPUT, false));
}

void test_fresh_link_does_not_demote_an_armed_input_session() {
  TEST_ASSERT_FALSE(inputLossDemotesArmed(ARM_ARMED, MODE_INPUT, true));
}

void test_stale_link_does_not_demote_armed_mode() {
  // MODE_ARMED has no bus input -- staleness (however computed by a caller
  // that shouldn't even be checking it here) must never demote it.
  TEST_ASSERT_FALSE(inputLossDemotesArmed(ARM_ARMED, MODE_ARMED, false));
}

void test_stale_link_does_not_affect_an_already_arming_session() {
  // Demotion only applies to an ALREADY-ARMED session -- ARMING has its own
  // elapsed/commandedIsLow gate already and does not need a second path in.
  TEST_ASSERT_FALSE(inputLossDemotesArmed(ARM_ARMING, MODE_INPUT, false));
}

// --- srcChangeDemotesArmed ----------------------------------------------------

void test_src_change_demotes_an_armed_input_session() {
  TEST_ASSERT_TRUE(srcChangeDemotesArmed(ARM_ARMED, MODE_INPUT, true));
}

void test_unchanged_src_does_not_demote() {
  TEST_ASSERT_FALSE(srcChangeDemotesArmed(ARM_ARMED, MODE_INPUT, false));
}

void test_src_change_does_not_demote_armed_mode() {
  // MODE_ARMED never reads srcIdx_ -- a src change there is meaningless.
  TEST_ASSERT_FALSE(srcChangeDemotesArmed(ARM_ARMED, MODE_ARMED, true));
}

void test_src_change_does_not_affect_an_arming_session() {
  TEST_ASSERT_FALSE(srcChangeDemotesArmed(ARM_ARMING, MODE_INPUT, true));
}

// --- nextPulseUs: stale input forces min_us -------------------------------

void test_pulse_stale_input_forces_min_even_with_a_plausible_value() {
  // 1800 looks like a perfectly valid throttle reading -- inputStale is the
  // only thing distinguishing "live signal, happens to read 1800" from "the
  // link died with 1800 as the last frame". Must force minUs regardless.
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 1800, true, 1000));
}

void test_pulse_stale_check_precedes_no_data_check() {
  // inputUs<=0 alone means "hold last pulse" (returns 0), but inputStale
  // must take priority and force an active minUs write instead.
  TEST_ASSERT_EQUAL_UINT16(1000, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 0, true, 1000));
}

// --- neutralUs -----------------------------------------------------------

void test_neutral_unidirectional_is_min() {
  TEST_ASSERT_EQUAL_UINT16(1000, neutralUs(1000, 2000, false));
}

void test_neutral_bidirectional_is_center() {
  TEST_ASSERT_EQUAL_UINT16(1500, neutralUs(1000, 2000, true));
}

void test_neutral_bidirectional_odd_span_rounds_down() {
  // (1000 + 2001) / 2 = 1500.5 -> integer division floors to 1500.
  TEST_ASSERT_EQUAL_UINT16(1500, neutralUs(1000, 2001, true));
}

void test_neutral_bidirectional_degenerate_span() {
  TEST_ASSERT_EQUAL_UINT16(1500, neutralUs(1500, 1500, true));
}

// --- isCommandedLow: bidirectional shape ----------------------------------

void test_commanded_low_bidirectional_near_center_from_below() {
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1470, 0, true, 1500, 50, true));
}

void test_commanded_low_bidirectional_near_center_from_above() {
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1530, 0, true, 1500, 50, true));
}

void test_commanded_low_bidirectional_full_reverse_is_not_low() {
  // 1000 (near min_us) would have satisfied the OLD unidirectional check --
  // this is the exact case that must now fail for a bidirectional ESC: full
  // reverse is not a safe arming position.
  TEST_ASSERT_FALSE(isCommandedLow(MODE_ARMED, 1000, 0, true, 1500, 50, true));
}

void test_commanded_low_bidirectional_full_forward_is_not_low() {
  TEST_ASSERT_FALSE(isCommandedLow(MODE_ARMED, 2000, 0, true, 1500, 50, true));
}

void test_commanded_low_bidirectional_input_mode_at_center_is_low() {
  TEST_ASSERT_TRUE(isCommandedLow(MODE_INPUT, 0, 1500, true, 1500, 50, true));
}

void test_commanded_low_bidirectional_input_mode_full_reverse_is_not_low() {
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 1000, true, 1500, 50, true));
}

void test_commanded_low_bidirectional_input_mode_stale_centered_value_is_not_low() {
  // A dead-centre reading is meaningless if the link isn't confirmed fresh --
  // freshness is checked before the band, same guard as the unidirectional path.
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 1500, false, 1500, 50, true));
}

void test_commanded_low_bidirectional_input_mode_no_data_is_not_low() {
  // 0 is numerically 1500us below neutral, so the band alone would already
  // reject it -- but this pins that the inputUs<=0 sentinel guard is what's
  // doing the rejecting, a deliberate check, not an accident of arithmetic.
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 0, true, 1500, 50, true));
}

void test_commanded_low_unidirectional_far_below_raised_min_is_still_low() {
  // The scenario the header doc comment names: min_us raised well above
  // throttle_us's own 1000us range floor. A symmetric distance check would
  // wrongly reject this (|1000-1400|=400 > 50); the one-sided check
  // correctly accepts it, since clampUs makes "further below min_us" just
  // as safe as being at it.
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1000, 0, true, 1400, 50, false));
}

// --- nextPulseUs: neutral, not min, is the arm-hold/failsafe pulse --------

void test_pulse_not_armed_returns_neutral_when_bidirectional() {
  // The regression this whole amendment exists to fix: previously this
  // returned minUs (1000) unconditionally. Must now return the passed
  // neutralUs (1500, center) when bidirectional.
  TEST_ASSERT_EQUAL_UINT16(1500, nextPulseUs(ARM_ARMING, MODE_ARMED, 1000, 2000, 1800, 0, false, 1500));
}

void test_pulse_stale_input_forces_neutral_when_bidirectional() {
  TEST_ASSERT_EQUAL_UINT16(1500, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 1800, true, 1500));
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
  RUN_TEST(test_arm_switch_inactive_forces_off_from_any_state);
  RUN_TEST(test_arm_switch_active_alone_does_not_promote_to_armed);
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
  RUN_TEST(test_commanded_low_input_mode_requires_freshness_too);
  RUN_TEST(test_commanded_low_armed_mode_ignores_freshness);
  RUN_TEST(test_link_fresh_within_window);
  RUN_TEST(test_link_stale_at_boundary);
  RUN_TEST(test_link_stale_well_past_window);
  RUN_TEST(test_link_fresh_never_marked_is_stale_from_the_start);
  RUN_TEST(test_link_fresh_zero_is_stale_even_within_the_window);
  RUN_TEST(test_stale_link_demotes_an_armed_input_session);
  RUN_TEST(test_fresh_link_does_not_demote_an_armed_input_session);
  RUN_TEST(test_stale_link_does_not_demote_armed_mode);
  RUN_TEST(test_stale_link_does_not_affect_an_already_arming_session);
  RUN_TEST(test_src_change_demotes_an_armed_input_session);
  RUN_TEST(test_unchanged_src_does_not_demote);
  RUN_TEST(test_src_change_does_not_demote_armed_mode);
  RUN_TEST(test_src_change_does_not_affect_an_arming_session);
  RUN_TEST(test_pulse_stale_input_forces_min_even_with_a_plausible_value);
  RUN_TEST(test_pulse_stale_check_precedes_no_data_check);
  RUN_TEST(test_neutral_unidirectional_is_min);
  RUN_TEST(test_neutral_bidirectional_is_center);
  RUN_TEST(test_neutral_bidirectional_odd_span_rounds_down);
  RUN_TEST(test_neutral_bidirectional_degenerate_span);
  RUN_TEST(test_commanded_low_bidirectional_near_center_from_below);
  RUN_TEST(test_commanded_low_bidirectional_near_center_from_above);
  RUN_TEST(test_commanded_low_bidirectional_full_reverse_is_not_low);
  RUN_TEST(test_commanded_low_bidirectional_full_forward_is_not_low);
  RUN_TEST(test_commanded_low_bidirectional_input_mode_at_center_is_low);
  RUN_TEST(test_commanded_low_bidirectional_input_mode_full_reverse_is_not_low);
  RUN_TEST(test_commanded_low_bidirectional_input_mode_stale_centered_value_is_not_low);
  RUN_TEST(test_commanded_low_bidirectional_input_mode_no_data_is_not_low);
  RUN_TEST(test_commanded_low_unidirectional_far_below_raised_min_is_still_low);
  RUN_TEST(test_pulse_not_armed_returns_neutral_when_bidirectional);
  RUN_TEST(test_pulse_stale_input_forces_neutral_when_bidirectional);
  return UNITY_END();
}
