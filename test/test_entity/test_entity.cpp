// Entity formatting and staleness.
//
// These are the strings the crew actually reads at 3am, and the rule that
// decides whether a number on screen can still be believed. Both are pure
// functions of state, so both are testable on a laptop.

#include <unity.h>

#include "core/entity.h"

using namespace cc;

static char buf[32];

void setUp() { ccTestSetMillis(1); }
void tearDown() {}

// ---- domainName -------------------------------------------------------------

// Every domain must name itself. System is in the enum but has no page yet
// (docs/ROADMAP.md), which is exactly the kind of half-wired domain this
// catches - the enum and the name are meant to stay in step regardless.
static void test_domain_names(void) {
  TEST_ASSERT_EQUAL_STRING("Power", domainName(Domain::Power));
  TEST_ASSERT_EQUAL_STRING("Lights", domainName(Domain::Lighting));
  TEST_ASSERT_EQUAL_STRING("Water", domainName(Domain::Water));
  TEST_ASSERT_EQUAL_STRING("Climate", domainName(Domain::Climate));
  TEST_ASSERT_EQUAL_STRING("System", domainName(Domain::System));
}

static void test_domain_name_never_null(void) {
  // COUNT is not a real domain, but a caller that reaches it must still get a
  // printable string rather than a null the UI would dereference.
  TEST_ASSERT_NOT_NULL(domainName(Domain::COUNT));
  TEST_ASSERT_EQUAL_STRING("?", domainName(Domain::COUNT));
}

// ---- NumericEntity ----------------------------------------------------------

static void test_numeric_unset_reads_as_dashes(void) {
  NumericEntity e("bat.v", "Battery", Domain::Power, "V", 2);
  TEST_ASSERT_EQUAL_STRING("--", e.format(buf, sizeof(buf)));
}

static void test_numeric_formats_with_unit_and_decimals(void) {
  NumericEntity e("bat.v", "Battery", Domain::Power, "V", 2);
  e.set(13.42f);
  TEST_ASSERT_EQUAL_STRING("13.42 V", e.format(buf, sizeof(buf)));
}

static void test_numeric_decimals_round(void) {
  NumericEntity e("bat.a", "Current", Domain::Power, "A", 1);
  e.set(13.46f);
  TEST_ASSERT_EQUAL_STRING("13.5 A", e.format(buf, sizeof(buf)));

  NumericEntity whole("soc", "SoC", Domain::Power, "%", 0);
  whole.set(87.6f);
  TEST_ASSERT_EQUAL_STRING("88 %", whole.format(buf, sizeof(buf)));
}

static void test_numeric_negative(void) {
  // Discharge current is negative and must not lose its sign.
  NumericEntity e("bat.a", "Current", Domain::Power, "A", 2);
  e.set(-4.25f);
  TEST_ASSERT_EQUAL_STRING("-4.25 A", e.format(buf, sizeof(buf)));
}

static void test_numeric_without_unit_has_no_trailing_space(void) {
  NumericEntity e("cells", "Cells", Domain::Power, "", 0);
  e.set(4.0f);
  TEST_ASSERT_EQUAL_STRING("4", e.format(buf, sizeof(buf)));
}

static void test_numeric_null_unit_is_treated_as_empty(void) {
  // The constructor maps a null unit to "", so format must not dereference it.
  NumericEntity e("x", "X", Domain::Power, nullptr, 1);
  e.set(1.5f);
  TEST_ASSERT_EQUAL_STRING("1.5", e.format(buf, sizeof(buf)));
}

static void test_format_survives_a_useless_buffer(void) {
  NumericEntity e("bat.v", "Battery", Domain::Power, "V", 2);
  e.set(13.42f);
  TEST_ASSERT_EQUAL_STRING("", e.format(nullptr, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("", e.format(buf, 0));
}

static void test_format_truncates_rather_than_overruns(void) {
  NumericEntity e("bat.v", "Battery", Domain::Power, "V", 2);
  e.set(13.42f);
  char small[5];
  const char* out = e.format(small, sizeof(small));
  TEST_ASSERT_EQUAL_size_t(4, strlen(out));   // "13.4" plus the terminator
  TEST_ASSERT_EQUAL_STRING("13.4", out);
}

// ---- BinaryEntity -----------------------------------------------------------

static void test_binary_unset_reads_as_dashes(void) {
  BinaryEntity e("pump", "Pump", Domain::Water);
  TEST_ASSERT_EQUAL_STRING("--", e.format(buf, sizeof(buf)));
  TEST_ASSERT_FALSE(e.value());
}

static void test_binary_default_text(void) {
  BinaryEntity e("pump", "Pump", Domain::Water);
  e.set(true);
  TEST_ASSERT_EQUAL_STRING("ON", e.format(buf, sizeof(buf)));
  e.set(false);
  TEST_ASSERT_EQUAL_STRING("OFF", e.format(buf, sizeof(buf)));
}

static void test_binary_custom_text(void) {
  BinaryEntity e("chg", "Charging", Domain::Power, "Charging", "Idle");
  e.set(true);
  TEST_ASSERT_EQUAL_STRING("Charging", e.format(buf, sizeof(buf)));
  e.set(false);
  TEST_ASSERT_EQUAL_STRING("Idle", e.format(buf, sizeof(buf)));
}

// ---- SwitchEntity -----------------------------------------------------------

// The contract from entity.h: state changes only when the integration confirms
// it, so a relay that has gone offline shows the truth rather than the wish.
static void test_switch_command_alone_does_not_change_state(void) {
  bool asked = false;
  SwitchEntity e("light", "Light", Domain::Lighting,
                 [&](bool on) { asked = on; return true; });
  TEST_ASSERT_TRUE(e.command(true));
  TEST_ASSERT_TRUE(asked);
  TEST_ASSERT_FALSE(e.value());          // not until confirm()
  TEST_ASSERT_EQUAL_STRING("--", e.format(buf, sizeof(buf)));
}

static void test_switch_confirm_publishes_state(void) {
  SwitchEntity e("light", "Light", Domain::Lighting,
                 [](bool) { return true; });
  e.confirm(true);
  TEST_ASSERT_TRUE(e.value());
  TEST_ASSERT_EQUAL_STRING("ON", e.format(buf, sizeof(buf)));
}

static void test_switch_reports_a_rejected_write(void) {
  SwitchEntity e("light", "Light", Domain::Lighting,
                 [](bool) { return false; });
  TEST_ASSERT_FALSE(e.command(true));
  TEST_ASSERT_FALSE(e.value());
}

static void test_switch_without_a_writer_fails_closed(void) {
  SwitchEntity e("light", "Light", Domain::Lighting, nullptr);
  TEST_ASSERT_FALSE(e.command(true));
}

static void test_switch_kind_is_switch(void) {
  SwitchEntity e("light", "Light", Domain::Lighting, [](bool) { return true; });
  TEST_ASSERT_EQUAL(EntityKind::Switch, e.kind());
}

// ---- Staleness --------------------------------------------------------------

static void test_never_updated_is_stale(void) {
  NumericEntity e("bat.v", "Battery", Domain::Power, "V", 2);
  TEST_ASSERT_FALSE(e.everUpdated());
  TEST_ASSERT_TRUE(e.stale());
}

static void test_fresh_value_is_not_stale(void) {
  NumericEntity e("bat.v", "Battery", Domain::Power, "V", 2);
  e.set(13.4f);
  TEST_ASSERT_TRUE(e.everUpdated());
  TEST_ASSERT_FALSE(e.stale());
}

static void test_stale_boundary(void) {
  NumericEntity e("bat.v", "Battery", Domain::Power, "V", 2);
  e.set(13.4f);

  // The rule is "older than", so the threshold itself is still good.
  ccTestAdvanceMillis(CFG_STALE_AFTER_MS);
  TEST_ASSERT_FALSE(e.stale());

  ccTestAdvanceMillis(1);
  TEST_ASSERT_TRUE(e.stale());
}

static void test_a_new_value_refreshes(void) {
  NumericEntity e("bat.v", "Battery", Domain::Power, "V", 2);
  e.set(13.4f);
  ccTestAdvanceMillis(CFG_STALE_AFTER_MS * 2);
  TEST_ASSERT_TRUE(e.stale());
  e.set(13.5f);
  TEST_ASSERT_FALSE(e.stale());
}

static void test_epoch_zero(void) {
  // Known and harmless, recorded so it is not mistaken for a regression:
  // "never updated" is encoded as updatedMs_ == 0, so a value set in the very
  // first millisecond after boot reads as never updated and shows "--" until
  // the next update. On the board nothing publishes that early - the BMS takes
  // ~10 s to reach online - so this has never been observable in practice.
  ccTestSetMillis(0);
  NumericEntity e("bat.v", "Battery", Domain::Power, "V", 2);
  e.set(13.4f);
  TEST_ASSERT_FALSE(e.everUpdated());
  TEST_ASSERT_EQUAL_STRING("--", e.format(buf, sizeof(buf)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_domain_names);
  RUN_TEST(test_domain_name_never_null);
  RUN_TEST(test_numeric_unset_reads_as_dashes);
  RUN_TEST(test_numeric_formats_with_unit_and_decimals);
  RUN_TEST(test_numeric_decimals_round);
  RUN_TEST(test_numeric_negative);
  RUN_TEST(test_numeric_without_unit_has_no_trailing_space);
  RUN_TEST(test_numeric_null_unit_is_treated_as_empty);
  RUN_TEST(test_format_survives_a_useless_buffer);
  RUN_TEST(test_format_truncates_rather_than_overruns);
  RUN_TEST(test_binary_unset_reads_as_dashes);
  RUN_TEST(test_binary_default_text);
  RUN_TEST(test_binary_custom_text);
  RUN_TEST(test_switch_command_alone_does_not_change_state);
  RUN_TEST(test_switch_confirm_publishes_state);
  RUN_TEST(test_switch_reports_a_rejected_write);
  RUN_TEST(test_switch_without_a_writer_fails_closed);
  RUN_TEST(test_switch_kind_is_switch);
  RUN_TEST(test_never_updated_is_stale);
  RUN_TEST(test_fresh_value_is_not_stale);
  RUN_TEST(test_stale_boundary);
  RUN_TEST(test_a_new_value_refreshes);
  RUN_TEST(test_epoch_zero);
  return UNITY_END();
}
