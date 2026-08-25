#include <unity.h>
#include "hardware/motor/motor_math.h"

using namespace motor;

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
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1500, 0, true, 1500, 50));   // exactly neutral
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1540, 0, true, 1500, 50));   // within margin
  TEST_ASSERT_FALSE(isCommandedLow(MODE_ARMED, 1560, 0, true, 1500, 50));  // outside margin
}

void test_commanded_low_input_mode_requires_confirmed_reading() {
  TEST_ASSERT_TRUE(isCommandedLow(MODE_INPUT, 0, 1520, true, 1500, 50));
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 1800, true, 1500, 50));
  // inputUs <= 0 is "no data", never "confirmed low" -- must not read as
  // low enough to arm just because it is numerically small.
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 0, true, 1500, 50));
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, -5, true, 1500, 50));
}

void test_commanded_low_rejects_any_other_mode() {
  TEST_ASSERT_FALSE(isCommandedLow(MODE_OFF, 1500, 1500, true, 1500, 50));
}

// --- isCommandedLow: freshness gating (MODE_INPUT only) ---------------------

void test_commanded_low_input_mode_requires_freshness_too() {
  // A reading inside the band that is NOT fresh must still fail -- arming
  // must never complete against a link already known dead.
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 1520, false, 1500, 50));
}

void test_commanded_low_armed_mode_ignores_freshness() {
  // MODE_ARMED has no bus input at all -- inputFresh must have no effect.
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1500, 0, false, 1500, 50));
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

void test_neutral_is_the_midpoint() {
  TEST_ASSERT_EQUAL_UINT16(1500, neutralUs(1000, 2000));
}

void test_neutral_odd_span_rounds_down() {
  // (1000 + 2001) / 2 = 1500.5 -> integer division floors to 1500.
  TEST_ASSERT_EQUAL_UINT16(1500, neutralUs(1000, 2001));
}

void test_neutral_degenerate_span() {
  TEST_ASSERT_EQUAL_UINT16(1500, neutralUs(1500, 1500));
}

void test_neutral_ignores_a_raised_min() {
  // Narrowing the calibration moves the midpoint with it -- neutral is
  // always the centre of the span, never an endpoint.
  TEST_ASSERT_EQUAL_UINT16(1600, neutralUs(1200, 2000));
}

// --- isCommandedLow: the band around neutral ------------------------------

void test_commanded_low_near_center_from_below() {
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1470, 0, true, 1500, 50));
}

void test_commanded_low_near_center_from_above() {
  TEST_ASSERT_TRUE(isCommandedLow(MODE_ARMED, 1530, 0, true, 1500, 50));
}

void test_commanded_low_full_reverse_is_not_low() {
  // Full reverse is not a safe arming position, however close to min_us it
  // sits -- the band is two-sided precisely so this fails.
  TEST_ASSERT_FALSE(isCommandedLow(MODE_ARMED, 1000, 0, true, 1500, 50));
}

void test_commanded_low_full_forward_is_not_low() {
  TEST_ASSERT_FALSE(isCommandedLow(MODE_ARMED, 2000, 0, true, 1500, 50));
}

void test_commanded_low_input_mode_at_center_is_low() {
  TEST_ASSERT_TRUE(isCommandedLow(MODE_INPUT, 0, 1500, true, 1500, 50));
}

void test_commanded_low_input_mode_full_reverse_is_not_low() {
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 1000, true, 1500, 50));
}

void test_commanded_low_input_mode_stale_centered_value_is_not_low() {
  // A dead-centre reading is meaningless if the link isn't confirmed fresh --
  // freshness is checked before the band.
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 1500, false, 1500, 50));
}

void test_commanded_low_input_mode_no_data_is_not_low() {
  // 0 is numerically 1500us below neutral, so the band alone would already
  // reject it -- but this pins that the inputUs<=0 sentinel guard is what's
  // doing the rejecting, a deliberate check, not an accident of arithmetic.
  TEST_ASSERT_FALSE(isCommandedLow(MODE_INPUT, 0, 0, true, 1500, 50));
}

// --- nextPulseUs: neutral is the arm-hold/failsafe pulse ------------------

void test_pulse_not_armed_returns_neutral() {
  // Anything other than ARM_ARMED answers the passed neutralUs (1500,
  // centre), never an endpoint.
  TEST_ASSERT_EQUAL_UINT16(1500, nextPulseUs(ARM_ARMING, MODE_ARMED, 1000, 2000, 1800, 0, false, 1500));
}

void test_pulse_stale_input_forces_neutral() {
  TEST_ASSERT_EQUAL_UINT16(1500, nextPulseUs(ARM_ARMED, MODE_INPUT, 1000, 2000, 1000, 1800, true, 1500));
}

// --- frameUsForRate ----------------------------------------------------------

void test_frame_us_for_each_rate() {
  TEST_ASSERT_EQUAL_UINT32(20000, frameUsForRate(RATE_50));
  TEST_ASSERT_EQUAL_UINT32(10000, frameUsForRate(RATE_100));
  TEST_ASSERT_EQUAL_UINT32(5000,  frameUsForRate(RATE_200));
  TEST_ASSERT_EQUAL_UINT32(2500,  frameUsForRate(RATE_400));
}

// Out of range falls back to the slowest, universally auto-detected frame
// rather than the fastest -- an unknown index must never speed the output up.
void test_frame_us_out_of_range_falls_back_to_50hz() {
  TEST_ASSERT_EQUAL_UINT32(20000, frameUsForRate(4));
  TEST_ASSERT_EQUAL_UINT32(20000, frameUsForRate(255));
}

// --- effectiveMaxUs ----------------------------------------------------------

void test_effective_max_unchanged_when_the_frame_has_room() {
  TEST_ASSERT_EQUAL_UINT16(2000, effectiveMaxUs(2000, 20000));
  TEST_ASSERT_EQUAL_UINT16(2000, effectiveMaxUs(2000, 2500));
}

// max_us is settable to 2500, which is the whole 400Hz frame -- writing it
// would leave no low period at all and the ESC would see a permanently high
// line instead of a pulse train.
void test_effective_max_reserves_a_low_period_at_400hz() {
  TEST_ASSERT_EQUAL_UINT16(2375, effectiveMaxUs(2500, 2500));
}

void test_effective_max_at_the_exact_boundary() {
  TEST_ASSERT_EQUAL_UINT16(2375, effectiveMaxUs(2375, 2500));
  TEST_ASSERT_EQUAL_UINT16(2374, effectiveMaxUs(2374, 2500));
}

// Defensive: no unsigned underflow if a frame somehow shorter than the
// reserved low period is ever passed in.
void test_effective_max_degenerate_frame_does_not_underflow() {
  TEST_ASSERT_EQUAL_UINT16(0, effectiveMaxUs(2000, 100));
  TEST_ASSERT_EQUAL_UINT16(0, effectiveMaxUs(2000, kMinLowUs));
}

// --- rateChangeDemotesArmed --------------------------------------------------

// BLHeli_S detects its input frame rate when it arms. Changing the rate under
// an already-armed ESC must force a fresh low-throttle hold so it re-syncs,
// exactly as a src change or a stale link already does.
void test_rate_change_demotes_an_armed_session() {
  TEST_ASSERT_TRUE(rateChangeDemotesArmed(ARM_ARMED, true));
}

void test_unchanged_rate_does_not_demote() {
  TEST_ASSERT_FALSE(rateChangeDemotesArmed(ARM_ARMED, false));
}

void test_rate_change_does_not_affect_an_arming_session() {
  TEST_ASSERT_FALSE(rateChangeDemotesArmed(ARM_ARMING, true));
}

void test_rate_change_does_not_affect_an_off_session() {
  TEST_ASSERT_FALSE(rateChangeDemotesArmed(ARM_OFF, true));
}

// Unlike srcChangeDemotesArmed, this is NOT restricted to MODE_INPUT: the
// bench-throttle mode drives the same pin at the same new frame rate, so it
// needs the same re-sync. There is no mode parameter to get wrong.
void test_rate_change_demotes_regardless_of_mode() {
  TEST_ASSERT_TRUE(rateChangeDemotesArmed(ARM_ARMED, true));
}

// --- type = none, and the output-disabled interlock ---------------------------

void test_type_none_is_the_first_type_value() {
  // The enum values ARE the motor<N>.type option indices, so "none" must sit
  // at 0 or a stored value maps to the wrong electronics.
  TEST_ASSERT_EQUAL_INT32(0, TYPE_NONE);
  TEST_ASSERT_EQUAL_INT32(1, TYPE_BRUSHLESS);
  TEST_ASSERT_EQUAL_INT32(2, TYPE_BRUSHED);
}

void test_output_disabled_when_type_is_none() {
  // An unconfigured board: the mode says drive, but nothing is known to be on
  // the end of the wire, so the pin must stay detached.
  TEST_ASSERT_TRUE(outputDisabled(MODE_INPUT, TYPE_NONE));
  TEST_ASSERT_TRUE(outputDisabled(MODE_ARMED, TYPE_NONE));
}

void test_output_disabled_when_mode_is_off() {
  TEST_ASSERT_TRUE(outputDisabled(MODE_OFF, TYPE_BRUSHED));
  TEST_ASSERT_TRUE(outputDisabled(MODE_OFF, TYPE_BRUSHLESS));
}

void test_output_enabled_once_a_type_is_chosen_and_the_mode_drives() {
  TEST_ASSERT_FALSE(outputDisabled(MODE_INPUT, TYPE_BRUSHED));
  TEST_ASSERT_FALSE(outputDisabled(MODE_INPUT, TYPE_BRUSHLESS));
  TEST_ASSERT_FALSE(outputDisabled(MODE_ARMED, TYPE_BRUSHED));
}

void test_choosing_a_type_counts_as_entering_from_off() {
  // The whole point of the none default: selecting a type is what starts the
  // arm hold, so the first movement still costs the neutral wait.
  TEST_ASSERT_TRUE(enteringEnabled(MODE_INPUT, TYPE_NONE, MODE_INPUT, TYPE_BRUSHED));
}

void test_leaving_mode_off_still_counts_as_entering_from_off() {
  TEST_ASSERT_TRUE(enteringEnabled(MODE_OFF, TYPE_BRUSHED, MODE_INPUT, TYPE_BRUSHED));
}

void test_swapping_type_while_already_live_is_not_entering_from_off() {
  // Still demoted, but by the type-changed path, not this one.
  TEST_ASSERT_FALSE(enteringEnabled(MODE_INPUT, TYPE_BRUSHLESS, MODE_INPUT, TYPE_BRUSHED));
}

void test_still_disabled_is_not_entering_from_off() {
  TEST_ASSERT_FALSE(enteringEnabled(MODE_OFF, TYPE_NONE, MODE_INPUT, TYPE_NONE));
  TEST_ASSERT_FALSE(enteringEnabled(MODE_INPUT, TYPE_NONE, MODE_OFF, TYPE_BRUSHED));
}

void setUp() {}
void tearDown() {}


// --- armSwitchGates ---------------------------------------------------------

void test_arm_switch_gates_when_the_slot_reads_inactive() {
  TEST_ASSERT_TRUE(armSwitchGates(true, 0));
}

void test_arm_switch_does_not_gate_when_the_slot_reads_armed() {
  TEST_ASSERT_FALSE(armSwitchGates(true, 1));
}

void test_arm_switch_never_gates_without_a_drive_module() {
  // No drive module means no shared switch to obey -- motor<N> is then driven
  // by its own mode alone.
  TEST_ASSERT_FALSE(armSwitchGates(false, 0));
  TEST_ASSERT_FALSE(armSwitchGates(false, 1));
}

void test_arm_switch_gates_before_any_receiver_frame_arrives() {
  // The slot is 0 both because the drive module computed "not armed" and
  // because nothing has been received yet. Either way the answer is gate --
  // this is the case a freshness-based check used to get wrong.
  TEST_ASSERT_TRUE(armSwitchGates(true, 0));
}


// --- mirrorAboutSpan --------------------------------------------------------

void test_mirror_swaps_forward_and_reverse() {
  TEST_ASSERT_EQUAL_UINT16(1300, mirrorAboutSpan(1700, 1000, 2000));
  TEST_ASSERT_EQUAL_UINT16(1700, mirrorAboutSpan(1300, 1000, 2000));
}

void test_mirror_maps_the_endpoints_onto_each_other() {
  TEST_ASSERT_EQUAL_UINT16(2000, mirrorAboutSpan(1000, 1000, 2000));
  TEST_ASSERT_EQUAL_UINT16(1000, mirrorAboutSpan(2000, 1000, 2000));
}

void test_mirror_leaves_neutral_alone() {
  // The property the safe states depend on: the arm-hold pulse, the failsafe
  // value and the arm-switch clamp must all survive being mirrored.
  const uint16_t n = neutralUs(1000, 2000);
  TEST_ASSERT_EQUAL_UINT16(n, mirrorAboutSpan(n, 1000, 2000));
}

void test_mirror_is_its_own_inverse() {
  TEST_ASSERT_EQUAL_UINT16(1700, mirrorAboutSpan(mirrorAboutSpan(1700, 1000, 2000), 1000, 2000));
}

void test_mirror_respects_a_narrowed_calibration() {
  TEST_ASSERT_EQUAL_UINT16(1400, mirrorAboutSpan(1800, 1200, 2000));
}

void test_mirror_degenerate_span_is_that_one_value() {
  TEST_ASSERT_EQUAL_UINT16(1500, mirrorAboutSpan(1500, 1500, 1500));
}

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
  RUN_TEST(test_commanded_low_input_mode_requires_freshness_too);
  RUN_TEST(test_commanded_low_armed_mode_ignores_freshness);
  RUN_TEST(test_commanded_low_rejects_any_other_mode);
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
  RUN_TEST(test_neutral_is_the_midpoint);
  RUN_TEST(test_neutral_odd_span_rounds_down);
  RUN_TEST(test_neutral_degenerate_span);
  RUN_TEST(test_neutral_ignores_a_raised_min);
  RUN_TEST(test_commanded_low_near_center_from_below);
  RUN_TEST(test_commanded_low_near_center_from_above);
  RUN_TEST(test_commanded_low_full_reverse_is_not_low);
  RUN_TEST(test_commanded_low_full_forward_is_not_low);
  RUN_TEST(test_commanded_low_input_mode_at_center_is_low);
  RUN_TEST(test_commanded_low_input_mode_full_reverse_is_not_low);
  RUN_TEST(test_commanded_low_input_mode_stale_centered_value_is_not_low);
  RUN_TEST(test_commanded_low_input_mode_no_data_is_not_low);
  RUN_TEST(test_pulse_not_armed_returns_neutral);
  RUN_TEST(test_pulse_stale_input_forces_neutral);
  RUN_TEST(test_frame_us_for_each_rate);
  RUN_TEST(test_frame_us_out_of_range_falls_back_to_50hz);
  RUN_TEST(test_effective_max_unchanged_when_the_frame_has_room);
  RUN_TEST(test_effective_max_reserves_a_low_period_at_400hz);
  RUN_TEST(test_effective_max_at_the_exact_boundary);
  RUN_TEST(test_effective_max_degenerate_frame_does_not_underflow);
  RUN_TEST(test_rate_change_demotes_an_armed_session);
  RUN_TEST(test_unchanged_rate_does_not_demote);
  RUN_TEST(test_rate_change_does_not_affect_an_arming_session);
  RUN_TEST(test_rate_change_does_not_affect_an_off_session);
  RUN_TEST(test_rate_change_demotes_regardless_of_mode);
  RUN_TEST(test_arm_switch_gates_when_the_slot_reads_inactive);
  RUN_TEST(test_arm_switch_does_not_gate_when_the_slot_reads_armed);
  RUN_TEST(test_arm_switch_never_gates_without_a_drive_module);
  RUN_TEST(test_arm_switch_gates_before_any_receiver_frame_arrives);
  RUN_TEST(test_mirror_swaps_forward_and_reverse);
  RUN_TEST(test_mirror_maps_the_endpoints_onto_each_other);
  RUN_TEST(test_mirror_leaves_neutral_alone);
  RUN_TEST(test_mirror_is_its_own_inverse);
  RUN_TEST(test_mirror_respects_a_narrowed_calibration);
  RUN_TEST(test_mirror_degenerate_span_is_that_one_value);
  RUN_TEST(test_type_none_is_the_first_type_value);
  RUN_TEST(test_output_disabled_when_type_is_none);
  RUN_TEST(test_output_disabled_when_mode_is_off);
  RUN_TEST(test_output_enabled_once_a_type_is_chosen_and_the_mode_drives);
  RUN_TEST(test_choosing_a_type_counts_as_entering_from_off);
  RUN_TEST(test_leaving_mode_off_still_counts_as_entering_from_off);
  RUN_TEST(test_swapping_type_while_already_live_is_not_entering_from_off);
  RUN_TEST(test_still_disabled_is_not_entering_from_off);
  return UNITY_END();

}
