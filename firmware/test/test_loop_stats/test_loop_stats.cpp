#include <unity.h>
#include "core/loop_stats.h"

using namespace core;

// Marks `count` iterations spaced `stepUs` apart, starting one step after
// `fromUs`, and returns the timestamp of the last mark.
static uint32_t markEvery(LoopStats& s, uint32_t fromUs, uint32_t stepUs, uint32_t count) {
  uint32_t t = fromUs;
  for (uint32_t i = 0; i < count; ++i) {
    t += stepUs;
    s.mark(t);
  }
  return t;
}

void test_reports_nothing_before_the_first_window_completes() {
  LoopStats s;
  s.mark(0);
  markEvery(s, 0, 1000, 100);   // 100ms in, nowhere near a full window
  TEST_ASSERT_EQUAL_UINT32(0, s.hz());
  TEST_ASSERT_EQUAL_UINT32(0, s.worstUs());
}

void test_publishes_loop_rate_after_one_second() {
  LoopStats s;
  s.mark(0);
  markEvery(s, 0, 1000, 1000);   // 1000 iterations across exactly 1s
  TEST_ASSERT_EQUAL_UINT32(1000, s.hz());
}

void test_rate_reflects_a_faster_loop() {
  LoopStats s;
  s.mark(0);
  markEvery(s, 0, 50, 20000);    // 20k iterations across exactly 1s
  TEST_ASSERT_EQUAL_UINT32(20000, s.hz());
}

void test_publishes_the_worst_gap_in_the_window() {
  LoopStats s;
  s.mark(0);
  uint32_t t = markEvery(s, 0, 1000, 500);
  t += 87000;                    // one long pass, e.g. a blocking repaint
  s.mark(t);
  markEvery(s, t, 1000, 413);    // carry on to just past the 1s boundary
  TEST_ASSERT_EQUAL_UINT32(87000, s.worstUs());
}

void test_worst_gap_resets_each_window() {
  LoopStats s;
  s.mark(0);
  uint32_t t = markEvery(s, 0, 1000, 500);
  t += 87000;
  s.mark(t);
  t = markEvery(s, t, 1000, 413);        // completes the first window
  TEST_ASSERT_EQUAL_UINT32(87000, s.worstUs());
  markEvery(s, t, 1000, 1000);           // a clean second window
  TEST_ASSERT_EQUAL_UINT32(1000, s.worstUs());
}

// The published figures describe the last COMPLETED window, so they hold
// steady while the next one fills rather than flickering toward zero.
void test_figures_hold_between_windows() {
  LoopStats s;
  s.mark(0);
  uint32_t t = markEvery(s, 0, 1000, 1000);
  TEST_ASSERT_EQUAL_UINT32(1000, s.hz());
  markEvery(s, t, 1000, 10);             // 10ms into the next window
  TEST_ASSERT_EQUAL_UINT32(1000, s.hz());
  TEST_ASSERT_EQUAL_UINT32(1000, s.worstUs());
}

// The very first mark establishes the baseline; there is no interval before
// it, so it must not be counted as one.
void test_first_mark_is_a_baseline_not_an_iteration() {
  LoopStats s;
  s.mark(500000);                        // an arbitrary non-zero start
  markEvery(s, 500000, 1000, 1000);
  TEST_ASSERT_EQUAL_UINT32(1000, s.hz());
}

// micros() wraps every ~71 minutes. Unsigned subtraction handles it, and a
// wrap must not manufacture a vast bogus worst-gap.
void test_survives_the_micros_wraparound() {
  LoopStats s;
  const uint32_t start = 0xFFFFFF00u;    // 256us before the wrap
  s.mark(start);
  markEvery(s, start, 1000, 1000);
  TEST_ASSERT_EQUAL_UINT32(1000, s.hz());
  TEST_ASSERT_EQUAL_UINT32(1000, s.worstUs());
}

void test_shared_instance_is_the_same_object() {
  loopStats().reset();
  loopStats().mark(0);
  markEvery(loopStats(), 0, 1000, 1000);
  TEST_ASSERT_EQUAL_UINT32(1000, loopStats().hz());
  loopStats().reset();
  TEST_ASSERT_EQUAL_UINT32(0, loopStats().hz());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reports_nothing_before_the_first_window_completes);
  RUN_TEST(test_publishes_loop_rate_after_one_second);
  RUN_TEST(test_rate_reflects_a_faster_loop);
  RUN_TEST(test_publishes_the_worst_gap_in_the_window);
  RUN_TEST(test_worst_gap_resets_each_window);
  RUN_TEST(test_figures_hold_between_windows);
  RUN_TEST(test_first_mark_is_a_baseline_not_an_iteration);
  RUN_TEST(test_survives_the_micros_wraparound);
  RUN_TEST(test_shared_instance_is_the_same_object);
  return UNITY_END();
}
