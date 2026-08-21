#include <unity.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

// The two Black Pill headers describe the same physical board with a different
// MCU on it, and blackpill_f401ce.h says its feature block is kept in step with
// the F411's BY HAND. That convention has silently failed four times: vbat and
// tank_drive were enabled on one board only, and both ESC frame periods were
// left absent on the F401, where esc0_params.cpp's #ifndef fallback quietly
// applied 50Hz against the F411's 200Hz. Nothing failed -- the boards just
// shipped differently. These tests make that a red suite instead of a comment
// asking the next person to remember.
//
// Read as text rather than by including both: only one board header can be
// compiled in at a time, and macro values collide. The parse is safe because
// neither file uses trailing comments, function-like macros, line
// continuations, or a #define inside a conditional.

namespace {

const char* const kF411 = "include/boards/blackpill_f411ce.h";
const char* const kF401 = "include/boards/blackpill_f401ce.h";

// Names allowed to hold a DIFFERENT value in each header.
const char* const kMayDiffer[] = {
  "BOARD_ID",   // names its own board, so it must
};

// Names allowed to be absent from one header. A pin only means something when
// the feature that reads it is on, and FEATURE_WIFI is 0 on both boards.
const char* const kMayBeAbsent[] = {
  "WIFI_RX_PIN", "WIFI_TX_PIN", "WIFI_BAUD",
};

bool listed(const char* const* list, size_t n, const std::string& name) {
  for (size_t i = 0; i < n; i++)
    if (name == list[i]) return true;
  return false;
}

std::string trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::map<std::string, std::string> defines(const char* path) {
  std::map<std::string, std::string> out;
  FILE* f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    std::string l = trim(line);
    if (l.rfind("#define ", 0) != 0) continue;
    l = trim(l.substr(8));
    const size_t sp = l.find_first_of(" \t");
    if (sp == std::string::npos) { out[l] = ""; continue; }
    out[trim(l.substr(0, sp))] = trim(l.substr(sp));
  }
  fclose(f);
  return out;
}

}  // namespace

// Every FEATURE_ flag must be present in both headers with the same value.
// This is the whole "the two boards ship the same" rule in one assertion.
void test_both_boards_enable_the_same_features() {
  const auto a = defines(kF411), b = defines(kF401);
  for (const auto& kv : a) {
    if (kv.first.rfind("FEATURE_", 0) != 0) continue;
    const auto it = b.find(kv.first);
    TEST_ASSERT_TRUE_MESSAGE(it != b.end(), kv.first.c_str());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kv.second.c_str(), it->second.c_str(),
                                     kv.first.c_str());
  }
  for (const auto& kv : b) {
    if (kv.first.rfind("FEATURE_", 0) != 0) continue;
    TEST_ASSERT_TRUE_MESSAGE(a.count(kv.first) == 1, kv.first.c_str());
  }
}

// Catches the ESC_FRAME_US class of drift: a tuning define added to one header
// and not the other, where the missing one falls back to a module default and
// the board silently behaves differently.
void test_neither_board_defines_something_the_other_lacks() {
  const auto a = defines(kF411), b = defines(kF401);
  const size_t n = sizeof(kMayBeAbsent) / sizeof(kMayBeAbsent[0]);
  for (const auto& kv : a)
    if (!b.count(kv.first) && !listed(kMayBeAbsent, n, kv.first))
      TEST_FAIL_MESSAGE((kv.first + " is in the F411 header but not the F401's").c_str());
  for (const auto& kv : b)
    if (!a.count(kv.first) && !listed(kMayBeAbsent, n, kv.first))
      TEST_FAIL_MESSAGE((kv.first + " is in the F401 header but not the F411's").c_str());
}

// Shared names must also agree on their value, so a pin or a rate cannot drift
// apart while both headers still mention it.
void test_shared_defines_agree_on_their_value() {
  const auto a = defines(kF411), b = defines(kF401);
  const size_t n = sizeof(kMayDiffer) / sizeof(kMayDiffer[0]);
  for (const auto& kv : a) {
    const auto it = b.find(kv.first);
    if (it == b.end() || listed(kMayDiffer, n, kv.first)) continue;
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kv.second.c_str(), it->second.c_str(),
                                     kv.first.c_str());
  }
}

// A guard on the guard: if the parse silently returned nothing, every test
// above would pass while checking nothing at all.
void test_the_parser_actually_read_both_headers() {
  const auto a = defines(kF411), b = defines(kF401);
  TEST_ASSERT_GREATER_THAN_UINT32(20, (uint32_t)a.size());
  TEST_ASSERT_GREATER_THAN_UINT32(20, (uint32_t)b.size());
  TEST_ASSERT_EQUAL_STRING("\"blackpill_f411ce\"", a.at("BOARD_ID").c_str());
  TEST_ASSERT_EQUAL_STRING("\"blackpill_f401ce\"", b.at("BOARD_ID").c_str());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_parser_actually_read_both_headers);
  RUN_TEST(test_both_boards_enable_the_same_features);
  RUN_TEST(test_neither_board_defines_something_the_other_lacks);
  RUN_TEST(test_shared_defines_agree_on_their_value);
  return UNITY_END();
}
