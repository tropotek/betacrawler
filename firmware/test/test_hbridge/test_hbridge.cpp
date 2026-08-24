#include <unity.h>
#include "hardware/motor/hbridge_math.h"

using namespace motor;

// --- signedDutyPermille --------------------------------------------------

void test_duty_at_neutral_is_zero() {
  TEST_ASSERT_EQUAL_INT16(0, signedDutyPermille(1500, 1000, 2000, 1500));
}

void test_duty_at_max_is_full_positive() {
  TEST_ASSERT_EQUAL_INT16(1000, signedDutyPermille(2000, 1000, 2000, 1500));
}

void test_duty_at_min_is_full_negative() {
  TEST_ASSERT_EQUAL_INT16(-1000, signedDutyPermille(1000, 1000, 2000, 1500));
}

void test_duty_halfway_above_neutral_is_half_positive() {
  TEST_ASSERT_EQUAL_INT16(500, signedDutyPermille(1750, 1000, 2000, 1500));
}

void test_duty_halfway_below_neutral_is_half_negative() {
  TEST_ASSERT_EQUAL_INT16(-500, signedDutyPermille(1250, 1000, 2000, 1500));
}

void test_duty_neutral_at_min_does_not_divide_by_zero() {
  // signedDutyPermille takes neutralUs as a plain argument, so it must stay
  // safe against a zero-width below-neutral span whatever produced it.
  TEST_ASSERT_EQUAL_INT16(0, signedDutyPermille(1000, 1000, 2000, 1000));
  TEST_ASSERT_EQUAL_INT16(1000, signedDutyPermille(2000, 1000, 2000, 1000));
}

void test_duty_degenerate_span_does_not_divide_by_zero() {
  TEST_ASSERT_EQUAL_INT16(0, signedDutyPermille(1500, 1500, 1500, 1500));
}

// --- splitPinDuty ----------------------------------------------------------

void test_split_zero_coasts_by_default() {
  PinDuty d = splitPinDuty(0, false, false);
  TEST_ASSERT_EQUAL_UINT16(0, d.a);
  TEST_ASSERT_EQUAL_UINT16(0, d.b);
}

void test_split_zero_brakes_when_requested() {
  PinDuty d = splitPinDuty(0, false, true);
  TEST_ASSERT_EQUAL_UINT16(1000, d.a);
  TEST_ASSERT_EQUAL_UINT16(1000, d.b);
}

void test_split_positive_drives_pin_a() {
  PinDuty d = splitPinDuty(700, false, false);
  TEST_ASSERT_EQUAL_UINT16(700, d.a);
  TEST_ASSERT_EQUAL_UINT16(0, d.b);
}

void test_split_negative_drives_pin_b() {
  PinDuty d = splitPinDuty(-700, false, false);
  TEST_ASSERT_EQUAL_UINT16(0, d.a);
  TEST_ASSERT_EQUAL_UINT16(700, d.b);
}

void test_split_inverted_swaps_which_pin_drives_forward() {
  PinDuty d = splitPinDuty(700, true, false);
  TEST_ASSERT_EQUAL_UINT16(0, d.a);
  TEST_ASSERT_EQUAL_UINT16(700, d.b);
}

void test_split_inverted_swaps_which_pin_drives_reverse() {
  PinDuty d = splitPinDuty(-700, true, false);
  TEST_ASSERT_EQUAL_UINT16(700, d.a);
  TEST_ASSERT_EQUAL_UINT16(0, d.b);
}

void test_split_brake_only_applies_at_zero() {
  // brakeOnZero must not leak into a nonzero command -- only "both low" vs
  // "both high" AT zero is ever ambiguous; a real command is unambiguous.
  PinDuty d = splitPinDuty(300, false, true);
  TEST_ASSERT_EQUAL_UINT16(300, d.a);
  TEST_ASSERT_EQUAL_UINT16(0, d.b);
}

void setUp() {}
void tearDown() {}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_duty_at_neutral_is_zero);
  RUN_TEST(test_duty_at_max_is_full_positive);
  RUN_TEST(test_duty_at_min_is_full_negative);
  RUN_TEST(test_duty_halfway_above_neutral_is_half_positive);
  RUN_TEST(test_duty_halfway_below_neutral_is_half_negative);
  RUN_TEST(test_duty_neutral_at_min_does_not_divide_by_zero);
  RUN_TEST(test_duty_degenerate_span_does_not_divide_by_zero);
  RUN_TEST(test_split_zero_coasts_by_default);
  RUN_TEST(test_split_zero_brakes_when_requested);
  RUN_TEST(test_split_positive_drives_pin_a);
  RUN_TEST(test_split_negative_drives_pin_b);
  RUN_TEST(test_split_inverted_swaps_which_pin_drives_forward);
  RUN_TEST(test_split_inverted_swaps_which_pin_drives_reverse);
  RUN_TEST(test_split_brake_only_applies_at_zero);
  return UNITY_END();
}
