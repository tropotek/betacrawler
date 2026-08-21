#include <unity.h>
#include "core/battery.h"
#include "core/registry.h"

using namespace core;

void test_battery_defaults_are_all_zero() {
  Battery b;
  TEST_ASSERT_EQUAL_UINT16(0, b.milliVolts());
  TEST_ASSERT_EQUAL_UINT8(0, b.cells());
  TEST_ASSERT_EQUAL_UINT8(0, b.remainingPct());
  TEST_ASSERT_EQUAL_UINT32(0, b.lastFreshMs());
}

void test_battery_set_stores_all_three_fields() {
  Battery b;
  b.set(16800, 4, 100);
  TEST_ASSERT_EQUAL_UINT16(16800, b.milliVolts());
  TEST_ASSERT_EQUAL_UINT8(4, b.cells());
  TEST_ASSERT_EQUAL_UINT8(100, b.remainingPct());
}

void test_battery_set_does_not_mark_fresh() {
  Battery b;
  b.set(16800, 4, 100);
  TEST_ASSERT_EQUAL_UINT32(0, b.lastFreshMs());
}

void test_battery_mark_fresh_records_the_timestamp() {
  Battery b;
  b.markFresh(12345);
  TEST_ASSERT_EQUAL_UINT32(12345, b.lastFreshMs());
}

// A board with no vbat module must still let rx call reg.battery() safely,
// and must be distinguishable from a vbat that has gone stale.
void test_registry_battery_falls_back_to_an_empty_bus() {
  Registry reg;
  const Battery& b = reg.battery();
  TEST_ASSERT_EQUAL_UINT32(0, b.lastFreshMs());
  TEST_ASSERT_EQUAL_UINT16(0, b.milliVolts());
}

void test_registry_battery_returns_the_wired_bus() {
  Registry reg;
  Battery bus;
  bus.set(14800, 4, 55);
  bus.markFresh(900);
  reg.setBattery(bus);
  TEST_ASSERT_EQUAL_UINT16(14800, reg.battery().milliVolts());
  TEST_ASSERT_EQUAL_UINT32(900, reg.battery().lastFreshMs());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_battery_defaults_are_all_zero);
  RUN_TEST(test_battery_set_stores_all_three_fields);
  RUN_TEST(test_battery_set_does_not_mark_fresh);
  RUN_TEST(test_battery_mark_fresh_records_the_timestamp);
  RUN_TEST(test_registry_battery_falls_back_to_an_empty_bus);
  RUN_TEST(test_registry_battery_returns_the_wired_bus);
  return UNITY_END();
}
