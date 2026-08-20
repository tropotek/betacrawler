#include <unity.h>
#include "hardware/vbat/vbat_math.h"

using namespace vbat;

// --- packMvFromTap ---------------------------------------------------------

void test_scale_converts_tap_to_pack_millivolts() {
  // 47k/5k6 divider, scale 9393. A full 4S reads 1793mV at the tap.
  TEST_ASSERT_EQUAL_UINT16(16841, packMvFromTap(1793, 9393));
}

void test_scale_of_one_thousand_is_unity() {
  TEST_ASSERT_EQUAL_UINT16(1793, packMvFromTap(1793, 1000));
}

void test_scale_saturates_rather_than_wrapping_at_the_extremes() {
  // Worst case the parameter ranges allow: 3300mV tap, scale 30000, giving
  // 99000mV -- more than a uint16_t holds. Saturating is deliberate: wrapping
  // would report a plausible-looking 33.4V, while 65535 is obviously pegged.
  TEST_ASSERT_EQUAL_UINT16(65535, packMvFromTap(3300, 30000));
  // A realistic scale stays far below the ceiling.
  TEST_ASSERT_EQUAL_UINT16(30996, packMvFromTap(3300, 9393));
}

void test_scale_of_zero_or_less_reads_nothing() {
  TEST_ASSERT_EQUAL_UINT16(0, packMvFromTap(1793, 0));
  TEST_ASSERT_EQUAL_UINT16(0, packMvFromTap(1793, -1));
}

// --- detectCells -----------------------------------------------------------

void test_detect_cells_resolves_every_charged_pack() {
  TEST_ASSERT_EQUAL_UINT8(2, detectCells(8400));
  TEST_ASSERT_EQUAL_UINT8(3, detectCells(12600));
  TEST_ASSERT_EQUAL_UINT8(4, detectCells(16800));
  TEST_ASSERT_EQUAL_UINT8(6, detectCells(25200));
}

void test_detect_cells_returns_zero_below_the_validity_floor() {
  TEST_ASSERT_EQUAL_UINT8(0, detectCells(0));
  TEST_ASSERT_EQUAL_UINT8(0, detectCells(kMinValidMv - 1));
}

void test_detect_cells_holds_a_four_s_down_to_its_low_voltage_cutoff() {
  TEST_ASSERT_EQUAL_UINT8(4, detectCells(13200));   // 3.30 V/cell
}

// A part-drained 6S misdetects as 5S at and below 21500mV (3.583 V/cell),
// which is inside normal discharge range. Inherent to voltage-only detection
// and why an explicit vbat.cells exists. Asserted so it stays a known
// behaviour rather than being "fixed" into a regression.
void test_detect_cells_misreads_a_part_drained_six_s() {
  TEST_ASSERT_EQUAL_UINT8(6, detectCells(21501));
  TEST_ASSERT_EQUAL_UINT8(5, detectCells(21500));
}

// --- remainingPct ----------------------------------------------------------

void test_remaining_is_full_at_four_point_two_volts_per_cell() {
  TEST_ASSERT_EQUAL_UINT8(100, remainingPct(16800, 4));
}

void test_remaining_is_empty_at_three_point_three_volts_per_cell() {
  TEST_ASSERT_EQUAL_UINT8(0, remainingPct(13200, 4));
}

void test_remaining_is_linear_between_the_endpoints() {
  // 3.75 V/cell is the midpoint of 3.3..4.2
  TEST_ASSERT_EQUAL_UINT8(50, remainingPct(15000, 4));
}

void test_remaining_clamps_beyond_both_ends() {
  TEST_ASSERT_EQUAL_UINT8(100, remainingPct(18000, 4));
  TEST_ASSERT_EQUAL_UINT8(0, remainingPct(10000, 4));
}

void test_remaining_is_zero_when_cell_count_is_unknown() {
  TEST_ASSERT_EQUAL_UINT8(0, remainingPct(16800, 0));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_scale_converts_tap_to_pack_millivolts);
  RUN_TEST(test_scale_of_one_thousand_is_unity);
  RUN_TEST(test_scale_saturates_rather_than_wrapping_at_the_extremes);
  RUN_TEST(test_scale_of_zero_or_less_reads_nothing);
  RUN_TEST(test_detect_cells_resolves_every_charged_pack);
  RUN_TEST(test_detect_cells_returns_zero_below_the_validity_floor);
  RUN_TEST(test_detect_cells_holds_a_four_s_down_to_its_low_voltage_cutoff);
  RUN_TEST(test_detect_cells_misreads_a_part_drained_six_s);
  RUN_TEST(test_remaining_is_full_at_four_point_two_volts_per_cell);
  RUN_TEST(test_remaining_is_empty_at_three_point_three_volts_per_cell);
  RUN_TEST(test_remaining_is_linear_between_the_endpoints);
  RUN_TEST(test_remaining_clamps_beyond_both_ends);
  RUN_TEST(test_remaining_is_zero_when_cell_count_is_unknown);
  return UNITY_END();
}
