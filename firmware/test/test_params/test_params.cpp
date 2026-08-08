// Params is about validation semantics -- range, enum, length, type -- and
// those are the same whatever a board happens to enable. The fake module here
// exercises all three parameter types deliberately, so these tests neither
// break nor silently weaken when the real module set changes. The actual
// shipped defaults are pinned by test/golden/schema.json instead.
#include <unity.h>
#include <string.h>
#include "core/registry.h"

using namespace core;

static const char* const kModes[] = {"off", "on", "blink"};

enum : uint8_t { P_LEVEL = 0, P_MODE, P_NAME };

static const ParamDef kParams[] = {
  {"t.level", ParamType::U8,   "Level", "Hz",    1, 20, nullptr, 0, 0, 2, nullptr,  nullptr},
  {"t.mode",  ParamType::Enum, "Mode",  nullptr, 0, 0,  kModes,  3, 0, 2, nullptr,  nullptr},
  {"t.name",  ParamType::Str,  "Name",  nullptr, 0, 0,  nullptr, 0, kMaxStrLen, 0, "betacrawler", nullptr},
};
static const ModuleDesc kDesc = {"t", "Test", kParams, 3, nullptr, 0};

static Registry reg;

void test_defaults_come_from_the_descriptor() {
  Params p(reg);
  TEST_ASSERT_EQUAL_INT32(2, p.num(P_LEVEL));
  TEST_ASSERT_EQUAL_STRING("blink", p.str(P_MODE));
  TEST_ASSERT_EQUAL_STRING("betacrawler", p.str(P_NAME));
}

void test_numeric_range_rejected_not_clamped() {
  Params p(reg);
  TEST_ASSERT_EQUAL(SetResult::Range, p.setNum(P_LEVEL, 21));
  TEST_ASSERT_EQUAL(SetResult::Range, p.setNum(P_LEVEL, 0));
  // value must be untouched after rejection
  TEST_ASSERT_EQUAL_INT32(2, p.num(P_LEVEL));
  TEST_ASSERT_EQUAL(SetResult::Ok, p.setNum(P_LEVEL, 20));
  TEST_ASSERT_EQUAL_INT32(20, p.num(P_LEVEL));
}

void test_enum_set_by_name_stored_as_index() {
  Params p(reg);
  TEST_ASSERT_EQUAL(SetResult::Ok, p.setStr(P_MODE, "off"));
  TEST_ASSERT_EQUAL_STRING("off", p.str(P_MODE));
  TEST_ASSERT_EQUAL_INT32(0, p.num(P_MODE));
  TEST_ASSERT_EQUAL(SetResult::BadEnum, p.setStr(P_MODE, "purple"));
  TEST_ASSERT_EQUAL_STRING("off", p.str(P_MODE));
}

void test_string_too_long_rejected_not_truncated() {
  Params p(reg);
  char long_name[64];
  memset(long_name, 'x', sizeof(long_name));
  long_name[40] = '\0';
  TEST_ASSERT_EQUAL(SetResult::TooLong, p.setStr(P_NAME, long_name));
  TEST_ASSERT_EQUAL_STRING("betacrawler", p.str(P_NAME));

  char exact[kMaxStrLen + 1];
  memset(exact, 'y', kMaxStrLen);
  exact[kMaxStrLen] = '\0';
  TEST_ASSERT_EQUAL(SetResult::Ok, p.setStr(P_NAME, exact));
  TEST_ASSERT_EQUAL_STRING(exact, p.str(P_NAME));
}

void test_wrong_type_rejected() {
  Params p(reg);
  TEST_ASSERT_EQUAL(SetResult::WrongType, p.setNum(P_NAME, 5));
  TEST_ASSERT_EQUAL(SetResult::WrongType, p.setStr(P_LEVEL, "fast"));
}

void test_load_defaults_restores_after_changes() {
  Params p(reg);
  p.setNum(P_LEVEL, 15);
  p.setStr(P_NAME, "changed");
  p.loadDefaults();
  TEST_ASSERT_EQUAL_INT32(2, p.num(P_LEVEL));
  TEST_ASSERT_EQUAL_STRING("betacrawler", p.str(P_NAME));
}

void setUp() {}
void tearDown() {}

int main() {
  reg.add(kDesc);
  UNITY_BEGIN();
  RUN_TEST(test_defaults_come_from_the_descriptor);
  RUN_TEST(test_numeric_range_rejected_not_clamped);
  RUN_TEST(test_enum_set_by_name_stored_as_index);
  RUN_TEST(test_string_too_long_rejected_not_truncated);
  RUN_TEST(test_wrong_type_rejected);
  RUN_TEST(test_load_defaults_restores_after_changes);
  return UNITY_END();
}
