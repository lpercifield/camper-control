// Registry lookup and domain filtering.
//
// The registry is the seam the whole architecture rests on: integrations add
// to it, screens read from it, and nothing else connects the two. A duplicate
// id or a domain filter that misses an entity is a screen with a missing row.

#include <unity.h>

#include "core/registry.h"

using namespace cc;

static Registry& reg() { return Registry::instance(); }

void setUp() {
  ccTestSetMillis(1);
  reg().testReset();
}
void tearDown() { reg().testReset(); }

// ---- add / find -------------------------------------------------------------

static void test_starts_empty(void) {
  TEST_ASSERT_EQUAL_size_t(0, reg().size());
}

static void test_add_then_find(void) {
  NumericEntity v("bat.v", "Battery", Domain::Power, "V", 2);
  reg().add(&v);
  TEST_ASSERT_EQUAL_size_t(1, reg().size());
  TEST_ASSERT_EQUAL_PTR(&v, reg().find("bat.v"));
}

static void test_find_missing_returns_null(void) {
  NumericEntity v("bat.v", "Battery", Domain::Power, "V", 2);
  reg().add(&v);
  TEST_ASSERT_NULL(reg().find("nope"));
}

static void test_find_null_id_returns_null(void) {
  TEST_ASSERT_NULL(reg().find(nullptr));
}

static void test_find_is_exact_not_prefix(void) {
  // find() compares with strcmp, so "bat" must not match "bat.v". A prefix
  // match here would silently hand a screen the wrong entity.
  NumericEntity v("bat.v", "Battery", Domain::Power, "V", 2);
  reg().add(&v);
  TEST_ASSERT_NULL(reg().find("bat"));
  TEST_ASSERT_NULL(reg().find("bat.volts"));
}

static void test_add_null_is_ignored(void) {
  reg().add(nullptr);
  TEST_ASSERT_EQUAL_size_t(0, reg().size());
}

// Two integrations claiming one id is a real risk once there is more than one
// accessory. The second registration loses, and the first entity stays.
static void test_duplicate_id_is_rejected_and_first_wins(void) {
  NumericEntity first("bat.v", "Battery", Domain::Power, "V", 2);
  NumericEntity second("bat.v", "Impostor", Domain::Water, "L", 0);
  reg().add(&first);
  reg().add(&second);
  TEST_ASSERT_EQUAL_size_t(1, reg().size());
  TEST_ASSERT_EQUAL_PTR(&first, reg().find("bat.v"));
  TEST_ASSERT_EQUAL_STRING("Battery", reg().find("bat.v")->name());
}

// ---- ordering ---------------------------------------------------------------

static void test_at_preserves_registration_order(void) {
  NumericEntity a("a", "A", Domain::Power, "V", 1);
  NumericEntity b("b", "B", Domain::Power, "V", 1);
  NumericEntity c("c", "C", Domain::Power, "V", 1);
  reg().add(&a);
  reg().add(&b);
  reg().add(&c);
  TEST_ASSERT_EQUAL_PTR(&a, reg().at(0));
  TEST_ASSERT_EQUAL_PTR(&b, reg().at(1));
  TEST_ASSERT_EQUAL_PTR(&c, reg().at(2));
}

// ---- domain filtering -------------------------------------------------------

static void test_in_domain_selects_and_orders(void) {
  NumericEntity v("bat.v", "Battery", Domain::Power, "V", 2);
  BinaryEntity pump("pump", "Pump", Domain::Water);
  NumericEntity a("bat.a", "Current", Domain::Power, "A", 2);
  reg().add(&v);
  reg().add(&pump);
  reg().add(&a);

  std::vector<Entity*> power = reg().inDomain(Domain::Power);
  TEST_ASSERT_EQUAL_size_t(2, power.size());
  TEST_ASSERT_EQUAL_PTR(&v, power[0]);   // registration order, not id order
  TEST_ASSERT_EQUAL_PTR(&a, power[1]);

  std::vector<Entity*> water = reg().inDomain(Domain::Water);
  TEST_ASSERT_EQUAL_size_t(1, water.size());
  TEST_ASSERT_EQUAL_PTR(&pump, water[0]);
}

static void test_empty_domain_is_empty_not_null(void) {
  NumericEntity v("bat.v", "Battery", Domain::Power, "V", 2);
  reg().add(&v);
  TEST_ASSERT_EQUAL_size_t(0, reg().inDomain(Domain::Climate).size());
  TEST_ASSERT_EQUAL_size_t(0, reg().countInDomain(Domain::Climate));
}

// countInDomain exists so the UI can size a page without the allocation
// inDomain does at 4 Hz (docs/ROADMAP.md). The two must never disagree.
static void test_count_agrees_with_in_domain(void) {
  NumericEntity v("bat.v", "Battery", Domain::Power, "V", 2);
  NumericEntity a("bat.a", "Current", Domain::Power, "A", 2);
  BinaryEntity pump("pump", "Pump", Domain::Water);
  reg().add(&v);
  reg().add(&a);
  reg().add(&pump);

  for (uint8_t d = 0; d < static_cast<uint8_t>(Domain::COUNT); d++) {
    Domain dom = static_cast<Domain>(d);
    TEST_ASSERT_EQUAL_size_t(reg().inDomain(dom).size(), reg().countInDomain(dom));
  }
}

static void test_system_domain_is_registrable(void) {
  // Domain::System has no page yet, so entities registered there are invisible
  // on the device (docs/ROADMAP.md). The registry itself must still carry
  // them - the gap is in the UI's domain list, not here.
  NumericEntity heap("sys.heap", "Free heap", Domain::System, "B", 0);
  reg().add(&heap);
  TEST_ASSERT_EQUAL_size_t(1, reg().countInDomain(Domain::System));
  TEST_ASSERT_EQUAL_PTR(&heap, reg().find("sys.heap"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_empty);
  RUN_TEST(test_add_then_find);
  RUN_TEST(test_find_missing_returns_null);
  RUN_TEST(test_find_null_id_returns_null);
  RUN_TEST(test_find_is_exact_not_prefix);
  RUN_TEST(test_add_null_is_ignored);
  RUN_TEST(test_duplicate_id_is_rejected_and_first_wins);
  RUN_TEST(test_at_preserves_registration_order);
  RUN_TEST(test_in_domain_selects_and_orders);
  RUN_TEST(test_empty_domain_is_empty_not_null);
  RUN_TEST(test_count_agrees_with_in_domain);
  RUN_TEST(test_system_domain_is_registrable);
  return UNITY_END();
}
