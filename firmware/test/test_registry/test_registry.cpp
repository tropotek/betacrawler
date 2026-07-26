// Registry tests use only fake modules on purpose: core/ must work for any
// module set, so proving it against the LED would prove the wrong thing (and
// would break every time the board config changed). The real modules are
// covered where they matter -- through the schema golden fixture in
// test_dispatch.
#include <unity.h>
#include <string.h>
#include "core/registry.h"

using namespace core;

// --- fake modules ------------------------------------------------------------

static const char* const kColours[] = {"red", "green"};

static const ParamDef kAlphaParams[] = {
  {"alpha.level", ParamType::U8,   "Level",  "%",     0, 100, nullptr,  0, 0, 40, nullptr, nullptr},
  {"alpha.hue",   ParamType::Enum, "Hue",    nullptr, 0, 0,   kColours, 2, 0, 1,  nullptr, "Colour"},
};
static const TlmDef kAlphaTlm[] = {
  {"a.load", "Load", "%", TlmType::U32, 0, 0, nullptr},
};
static const ModuleDesc kAlphaDesc = {"alpha", "Alpha", kAlphaParams, 2, kAlphaTlm, 1};

static const ParamDef kBetaParams[] = {
  {"beta.name", ParamType::Str, "Name", nullptr, 0, 0, nullptr, 0, kMaxStrLen, 0, "bee", nullptr},
};
static const TlmDef kBetaTlm[] = {
  {"b.temp", "Temp", "°C", TlmType::F32, 0, 1, nullptr},
  {"b.mv",   "Volts", "V", TlmType::I32, 1000, 2, "Power"},
};
static const ModuleDesc kBetaDesc = {"beta", "Beta", kBetaParams, 1, kBetaTlm, 2};

// Contributes nothing of its own -- it only observes, like a display.
static const ModuleDesc kObserverDesc = {"obs", "Observer", nullptr, 0, nullptr, 0};

// Lifecycle calls stamp this so ordering across modules is provable, not just
// ordering within one module.
static int g_seq = 0;

struct FakeDriver : Module {
  int      beginCalls = 0;
  int      tickCalls = 0;
  int      changeCalls = 0;
  uint8_t  lastLocal = 0xFF;
  int32_t  lastValue = -1;
  uint32_t tlmSeed = 0;

  int             attachCalls   = 0;
  int             attachSeq     = 0;
  int             beginSeq      = 0;
  const Registry* attachedReg   = nullptr;
  const Params*   attachedParams = nullptr;

  void attach(const Registry& reg, const Params& p) override {
    ++attachCalls;
    attachSeq = ++g_seq;
    attachedReg = &reg;
    attachedParams = &p;
  }
  void begin() override { ++beginCalls; beginSeq = ++g_seq; }
  void tick(uint32_t) override { ++tickCalls; }
  void onParamChanged(uint8_t local, const Params& p) override {
    ++changeCalls;
    lastLocal = local;
    // Proves globalParam() maps the local index back onto the right slot.
    lastValue = p.num(globalParam(local));
  }
  void readTelemetry(TlmValue* out) override { out[0].u = tlmSeed; }
};

// The display's pattern in miniature: a module that reads OTHER modules'
// state, which is the whole reason attach() exists.
struct ObserverDriver : Module {
  int32_t seenLevel = -1;
  bool    foundTlm  = false;
  uint8_t tlmIdx    = 0xFF;
  bool    foundMissing = true;

  void attach(const Registry& reg, const Params& p) override {
    ParamId id = kNoParam;
    if (reg.findParam("alpha.level", &id)) seenLevel = p.num(id);
    foundTlm = reg.findTlm("b.temp", &tlmIdx);
    uint8_t junk = 0;
    foundMissing = reg.findTlm("nope", &junk);
  }
};

// --- tests -------------------------------------------------------------------

void test_flattens_params_in_registration_order() {
  Registry r;
  TEST_ASSERT_TRUE(r.add(kAlphaDesc));
  TEST_ASSERT_TRUE(r.add(kBetaDesc));

  TEST_ASSERT_EQUAL_UINT8(2, r.moduleCount());
  TEST_ASSERT_EQUAL_UINT8(3, r.paramCount());
  TEST_ASSERT_EQUAL_STRING("alpha.level", r.paramDef(0).key);
  TEST_ASSERT_EQUAL_STRING("alpha.hue",   r.paramDef(1).key);
  TEST_ASSERT_EQUAL_STRING("beta.name",   r.paramDef(2).key);
  TEST_ASSERT_EQUAL_STRING("alpha", r.moduleId(0));
  TEST_ASSERT_EQUAL_STRING("beta",  r.moduleId(1));
}

void test_find_param_spans_modules() {
  Registry r;
  r.add(kAlphaDesc);
  r.add(kBetaDesc);

  ParamId id = kNoParam;
  TEST_ASSERT_TRUE(r.findParam("beta.name", &id));
  TEST_ASSERT_EQUAL_UINT8(2, id);
  TEST_ASSERT_TRUE(r.findParam("alpha.level", &id));
  TEST_ASSERT_EQUAL_UINT8(0, id);
  TEST_ASSERT_FALSE(r.findParam("nope.missing", &id));
}

void test_group_defaults_to_module_label_and_can_be_overridden() {
  Registry r;
  r.add(kAlphaDesc);
  r.add(kBetaDesc);

  TEST_ASSERT_EQUAL_STRING("Alpha",  r.paramGroup(0));   // inherited
  TEST_ASSERT_EQUAL_STRING("Colour", r.paramGroup(1));   // overridden
  TEST_ASSERT_EQUAL_STRING("Beta",   r.paramGroup(2));
  TEST_ASSERT_EQUAL_STRING("Alpha",  r.tlmGroup(0));
  TEST_ASSERT_EQUAL_STRING("Beta",   r.tlmGroup(1));
  TEST_ASSERT_EQUAL_STRING("Power",  r.tlmGroup(2));     // overridden
}

// The heart of the module seam: a module is told which of ITS OWN parameters
// changed, never a global index, so its code cannot depend on what else is
// registered alongside it.
void test_notify_routes_to_owner_with_local_index() {
  Registry r;
  FakeDriver alpha, beta;
  r.add(kAlphaDesc, &alpha);
  r.add(kBetaDesc, &beta);
  Params p(r);

  r.notify(1, p);   // global 1 == alpha's local 1 (alpha.hue)
  TEST_ASSERT_EQUAL_INT(1, alpha.changeCalls);
  TEST_ASSERT_EQUAL_INT(0, beta.changeCalls);
  TEST_ASSERT_EQUAL_UINT8(1, alpha.lastLocal);
  TEST_ASSERT_EQUAL_INT32(1, alpha.lastValue);   // kAlphaParams[1].defNum

  r.notify(2, p);   // global 2 == beta's local 0 (beta.name)
  TEST_ASSERT_EQUAL_INT(1, alpha.changeCalls);
  TEST_ASSERT_EQUAL_INT(1, beta.changeCalls);
  TEST_ASSERT_EQUAL_UINT8(0, beta.lastLocal);
}

void test_every_module_is_attached_before_any_module_begins() {
  // Two passes, not attach-then-begin per module: a driver's begin() may look
  // at state another module published, so "everything attached" has to be true
  // before the first begin() runs.
  Registry r;
  Params p(r);
  FakeDriver alpha, beta;
  r.add(kAlphaDesc, &alpha);
  r.add(kBetaDesc, &beta);

  g_seq = 0;
  r.begin(p);

  TEST_ASSERT_EQUAL_INT(1, alpha.attachCalls);
  TEST_ASSERT_EQUAL_INT(1, beta.attachCalls);
  TEST_ASSERT_TRUE(alpha.attachSeq < beta.beginSeq);
  TEST_ASSERT_TRUE(beta.attachSeq  < alpha.beginSeq);
}

void test_attach_hands_over_the_registry_and_params() {
  Registry r;
  Params p(r);
  FakeDriver alpha;
  r.add(kAlphaDesc, &alpha);

  r.begin(p);

  TEST_ASSERT_EQUAL_PTR(&r, alpha.attachedReg);
  TEST_ASSERT_EQUAL_PTR(&p, alpha.attachedParams);
}

void test_attach_lets_a_module_read_another_modules_state() {
  Registry r;
  Params p(r);
  ObserverDriver obs;
  r.add(kAlphaDesc);          // descriptor only -- alpha owns the value
  r.add(kBetaDesc);
  r.add(kObserverDesc, &obs);
  p.loadDefaults();

  r.begin(p);

  TEST_ASSERT_EQUAL_INT32(40, obs.seenLevel);   // alpha.level's default
  TEST_ASSERT_TRUE(obs.foundTlm);
  TEST_ASSERT_EQUAL_UINT8(1, obs.tlmIdx);       // b.temp: alpha's a.load is 0
  TEST_ASSERT_FALSE(obs.foundMissing);          // an absent key reports absent
}

void test_driverless_module_is_not_attached() {
  Registry r;
  Params p(r);
  r.add(kAlphaDesc);          // no driver: must not be dereferenced
  r.begin(p);                 // reaching here without a crash is the assertion
  TEST_ASSERT_EQUAL_UINT8(1, r.moduleCount());
}

void test_begin_and_tick_reach_every_driver() {
  Registry r;
  Params p(r);
  FakeDriver alpha, beta;
  r.add(kAlphaDesc, &alpha);
  r.add(kBetaDesc, &beta);

  r.begin(p);
  r.tick(100);
  r.tick(200);

  TEST_ASSERT_EQUAL_INT(1, alpha.beginCalls);
  TEST_ASSERT_EQUAL_INT(1, beta.beginCalls);
  TEST_ASSERT_EQUAL_INT(2, alpha.tickCalls);
  TEST_ASSERT_EQUAL_INT(2, beta.tickCalls);
}

void test_collect_telemetry_writes_each_module_into_its_own_slice() {
  Registry r;
  FakeDriver alpha, beta;
  alpha.tlmSeed = 11;
  beta.tlmSeed = 22;
  r.add(kAlphaDesc, &alpha);
  r.add(kBetaDesc, &beta);

  TlmValue vals[FW_MAX_TLM];
  memset(vals, 0xEE, sizeof(vals));
  r.collectTelemetry(vals);

  TEST_ASSERT_EQUAL_UINT8(3, r.tlmCount());
  TEST_ASSERT_EQUAL_UINT32(11, vals[0].u);   // alpha's slice starts at 0
  TEST_ASSERT_EQUAL_UINT32(22, vals[1].u);   // beta's starts at 1
}

// A descriptor-only registration is what the native build produces for every
// real module, so it must never read a driver through a null pointer.
void test_driverless_module_zeroes_its_telemetry_slice() {
  Registry r;
  r.add(kAlphaDesc);          // no driver
  r.add(kBetaDesc);

  TlmValue vals[FW_MAX_TLM];
  memset(vals, 0xEE, sizeof(vals));
  r.collectTelemetry(vals);
  Params p(r);
  r.notify(0, p);             // must not crash

  TEST_ASSERT_EQUAL_UINT32(0, vals[0].u);
  TEST_ASSERT_EQUAL_UINT32(0, vals[1].u);
  TEST_ASSERT_EQUAL_UINT32(0, vals[2].u);
}

void test_add_refuses_to_exceed_capacity() {
  Registry r;
  // FW_MAX_MODULES modules of 1 param each fit; the next must be refused
  // rather than overrunning mods_[].
  for (uint8_t i = 0; i < FW_MAX_MODULES; ++i)
    TEST_ASSERT_TRUE(r.add(kBetaDesc));
  TEST_ASSERT_FALSE(r.add(kBetaDesc));
  TEST_ASSERT_EQUAL_UINT8(FW_MAX_MODULES, r.moduleCount());
}

// The flash header stores this; if it did not change with the layout, a build
// with a different module set would happily load the previous build's values
// into the wrong parameters.
void test_fingerprint_changes_with_the_module_set() {
  Registry justAlpha;
  justAlpha.add(kAlphaDesc);

  Registry both;
  both.add(kAlphaDesc);
  both.add(kBetaDesc);

  Registry reordered;
  reordered.add(kBetaDesc);
  reordered.add(kAlphaDesc);

  TEST_ASSERT_NOT_EQUAL(justAlpha.fingerprint(), both.fingerprint());
  TEST_ASSERT_NOT_EQUAL(both.fingerprint(), reordered.fingerprint());

  Registry sameAsBoth;
  sameAsBoth.add(kAlphaDesc);
  sameAsBoth.add(kBetaDesc);
  TEST_ASSERT_EQUAL_UINT32(both.fingerprint(), sameAsBoth.fingerprint());
}

// Widening a range does not move a single byte, but it does change what a
// stored value is allowed to mean -- so it must invalidate saved settings too.
void test_fingerprint_changes_when_a_bound_changes() {
  static const ParamDef kNarrow[] = {
    {"x.v", ParamType::U8, "V", nullptr, 0, 10, nullptr, 0, 0, 5, nullptr, nullptr},
  };
  static const ParamDef kWide[] = {
    {"x.v", ParamType::U8, "V", nullptr, 0, 99, nullptr, 0, 0, 5, nullptr, nullptr},
  };
  static const ModuleDesc kNarrowDesc = {"x", "X", kNarrow, 1, nullptr, 0};
  static const ModuleDesc kWideDesc   = {"x", "X", kWide,   1, nullptr, 0};

  Registry a, b;
  a.add(kNarrowDesc);
  b.add(kWideDesc);
  TEST_ASSERT_NOT_EQUAL(a.fingerprint(), b.fingerprint());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_flattens_params_in_registration_order);
  RUN_TEST(test_find_param_spans_modules);
  RUN_TEST(test_group_defaults_to_module_label_and_can_be_overridden);
  RUN_TEST(test_notify_routes_to_owner_with_local_index);
  RUN_TEST(test_every_module_is_attached_before_any_module_begins);
  RUN_TEST(test_attach_hands_over_the_registry_and_params);
  RUN_TEST(test_attach_lets_a_module_read_another_modules_state);
  RUN_TEST(test_driverless_module_is_not_attached);
  RUN_TEST(test_begin_and_tick_reach_every_driver);
  RUN_TEST(test_collect_telemetry_writes_each_module_into_its_own_slice);
  RUN_TEST(test_driverless_module_zeroes_its_telemetry_slice);
  RUN_TEST(test_add_refuses_to_exceed_capacity);
  RUN_TEST(test_fingerprint_changes_with_the_module_set);
  RUN_TEST(test_fingerprint_changes_when_a_bound_changes);
  return UNITY_END();
}
