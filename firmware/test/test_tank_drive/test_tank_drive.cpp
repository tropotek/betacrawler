#include <unity.h>
#include "features/tank_drive/tank_drive_math.h"

using namespace tank_drive;

// --- deadbanded ----------------------------------------------------------------

void test_deadband_collapses_value_inside_band_to_center() {
  TEST_ASSERT_EQUAL_INT16(1500, deadbanded(1510, 1500, 20));
  TEST_ASSERT_EQUAL_INT16(1500, deadbanded(1490, 1500, 20));
}

void test_deadband_passes_through_value_outside_band_unchanged() {
  TEST_ASSERT_EQUAL_INT16(1530, deadbanded(1530, 1500, 20));
  TEST_ASSERT_EQUAL_INT16(1470, deadbanded(1470, 1500, 20));
}

void test_deadband_boundary_is_inclusive() {
  TEST_ASSERT_EQUAL_INT16(1500, deadbanded(1520, 1500, 20));
}

// --- linkFresh -------------------------------------------------------------------

void test_link_fresh_never_marked_reads_stale() {
  TEST_ASSERT_FALSE(linkFresh(0, 100000, 500));
}

void test_link_fresh_within_window_reads_fresh() {
  TEST_ASSERT_TRUE(linkFresh(1000, 1400, 500));
}

void test_link_fresh_past_window_reads_stale() {
  TEST_ASSERT_FALSE(linkFresh(1000, 1600, 500));
}

// --- mix -------------------------------------------------------------------------

void test_mix_straight_forward_no_steer() {
  MixResult r = mix(1800, 1500, 1500, 1000, 2000, 100, 0);
  TEST_ASSERT_EQUAL_UINT16(1800, r.leftUs);
  TEST_ASSERT_EQUAL_UINT16(1800, r.rightUs);
}

void test_mix_full_ratio_reverse_is_unscaled() {
  MixResult r = mix(1200, 1500, 1500, 1000, 2000, 100, 0);
  TEST_ASSERT_EQUAL_UINT16(1200, r.leftUs);
  TEST_ASSERT_EQUAL_UINT16(1200, r.rightUs);
}

void test_mix_reverse_ratio_scales_reverse_power() {
  // Full-reverse stick (1000), 50% reverse ratio -> half the distance from
  // center: 1500 + (1000-1500)*50/100 = 1250.
  MixResult r = mix(1000, 1500, 1500, 1000, 2000, 50, 0);
  TEST_ASSERT_EQUAL_UINT16(1250, r.leftUs);
  TEST_ASSERT_EQUAL_UINT16(1250, r.rightUs);
}

void test_mix_forward_throttle_is_never_ratio_scaled() {
  // reverseRatioPct only applies below center -- full forward at 50% ratio
  // must still be full forward, not halved.
  MixResult r = mix(2000, 1500, 1500, 1000, 2000, 50, 0);
  TEST_ASSERT_EQUAL_UINT16(2000, r.leftUs);
  TEST_ASSERT_EQUAL_UINT16(2000, r.rightUs);
}

void test_mix_in_place_pivot() {
  // Throttle at center, full-right steer: one track full forward, the
  // other full reverse, neither needs clamping.
  MixResult r = mix(1500, 2000, 1500, 1000, 2000, 100, 0);
  TEST_ASSERT_EQUAL_UINT16(2000, r.leftUs);
  TEST_ASSERT_EQUAL_UINT16(1000, r.rightUs);
}

void test_mix_proportional_clamp_preserves_turn_ratio() {
  // Full forward (2000) + half-right steer (1750, offset 250).
  // leftOffset = 500+250 = 750 (exceeds the +-500 range -> scale 500*100/750 = 66).
  // rightOffset = 500-250 = 250 (within range on its own).
  // Proportional: BOTH offsets scaled by 66% -> left=1500+495=1995, right=1500+165=1665.
  // (An independent-clamp implementation would instead give 2000/1750 --
  // asserting the proportional numbers here is what catches that regression.)
  MixResult r = mix(2000, 1750, 1500, 1000, 2000, 100, 0);
  TEST_ASSERT_EQUAL_UINT16(1995, r.leftUs);
  TEST_ASSERT_EQUAL_UINT16(1665, r.rightUs);
}

void test_mix_applies_deadband_to_both_inputs() {
  // Throttle 10us off center, steer exactly center, deadband 20 -> both
  // collapse to center, no motion at all.
  MixResult r = mix(1510, 1500, 1500, 1000, 2000, 100, 20);
  TEST_ASSERT_EQUAL_UINT16(1500, r.leftUs);
  TEST_ASSERT_EQUAL_UINT16(1500, r.rightUs);
}

// --- computeArmed ----------------------------------------------------------------

void test_armed_requires_fresh_link() {
  // rxFresh=false must force unarmed regardless of arm_src/range -- a stale
  // link can never leave the vehicle armed, same reasoning as tank_drive's
  // own failsafe for left/right.
  TEST_ASSERT_FALSE(computeArmed(false, true, 0, 1700, 2000));
  TEST_ASSERT_FALSE(computeArmed(false, false, 1800, 1700, 2000));
}

void test_armed_true_when_no_arm_src_selected_and_link_fresh() {
  // arm_src=none means the feature is off: always armed once rxFresh, the
  // exact "zero behavior change" default the design doc requires.
  TEST_ASSERT_TRUE(computeArmed(true, true, 0, 1700, 2000));
}

void test_armed_true_when_channel_within_range() {
  TEST_ASSERT_TRUE(computeArmed(true, false, 1900, 1700, 2000));
}

void test_armed_false_when_channel_below_range() {
  TEST_ASSERT_FALSE(computeArmed(true, false, 1000, 1700, 2000));
}

void test_armed_false_when_channel_above_range() {
  TEST_ASSERT_FALSE(computeArmed(true, false, 2500, 1700, 2000));
}

void test_armed_boundary_is_inclusive_at_min_and_max() {
  TEST_ASSERT_TRUE(computeArmed(true, false, 1700, 1700, 2000));
  TEST_ASSERT_TRUE(computeArmed(true, false, 2000, 1700, 2000));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_deadband_collapses_value_inside_band_to_center);
  RUN_TEST(test_deadband_passes_through_value_outside_band_unchanged);
  RUN_TEST(test_deadband_boundary_is_inclusive);
  RUN_TEST(test_link_fresh_never_marked_reads_stale);
  RUN_TEST(test_link_fresh_within_window_reads_fresh);
  RUN_TEST(test_link_fresh_past_window_reads_stale);
  RUN_TEST(test_mix_straight_forward_no_steer);
  RUN_TEST(test_mix_full_ratio_reverse_is_unscaled);
  RUN_TEST(test_mix_reverse_ratio_scales_reverse_power);
  RUN_TEST(test_mix_forward_throttle_is_never_ratio_scaled);
  RUN_TEST(test_mix_in_place_pivot);
  RUN_TEST(test_mix_proportional_clamp_preserves_turn_ratio);
  RUN_TEST(test_mix_applies_deadband_to_both_inputs);
  RUN_TEST(test_armed_requires_fresh_link);
  RUN_TEST(test_armed_true_when_no_arm_src_selected_and_link_fresh);
  RUN_TEST(test_armed_true_when_channel_within_range);
  RUN_TEST(test_armed_false_when_channel_below_range);
  RUN_TEST(test_armed_false_when_channel_above_range);
  RUN_TEST(test_armed_boundary_is_inclusive_at_min_and_max);
  return UNITY_END();
}
