// Alarm severity, silencing, and the fixed-size table.
//
// This is the code that decides what the crew is told and whether the van is
// allowed to stay quiet about it. The auto-unmute rule in particular is a
// safety property: silencing one fault must never silence the next one.

#include <unity.h>

#include "core/alarms.h"

using namespace cc;

static Alarms& al() { return Alarms::instance(); }

void setUp() {
  ccTestSetMillis(1);
  al().testReset();
}
void tearDown() { al().testReset(); }

// ---- raise / clear ----------------------------------------------------------

static void test_starts_quiet(void) {
  TEST_ASSERT_FALSE(al().any());
  TEST_ASSERT_EQUAL_size_t(0, al().activeCount());
  TEST_ASSERT_NULL(al().worstAlarm());
  TEST_ASSERT_EQUAL(Severity::Info, al().worst());
  TEST_ASSERT_FALSE(al().shouldSound());
}

static void test_raise_then_clear(void) {
  al().raise("bms.cell_over", "Cell over voltage", Severity::Critical);
  TEST_ASSERT_TRUE(al().any());
  TEST_ASSERT_EQUAL_size_t(1, al().activeCount());
  TEST_ASSERT_EQUAL_STRING("Cell over voltage", al().worstAlarm()->text);

  al().clear("bms.cell_over");
  TEST_ASSERT_FALSE(al().any());
  TEST_ASSERT_EQUAL_size_t(0, al().activeCount());
}

static void test_raise_records_when_it_started(void) {
  ccTestSetMillis(5000);
  al().raise("bms.cell_over", "Cell over voltage", Severity::Critical);
  TEST_ASSERT_EQUAL_UINT32(5000, al().worstAlarm()->sinceMs);
}

// The condition is still the same condition. Re-raising an already-active
// alarm must not restart its clock, or "active for 40 minutes" resets to zero
// every polling cycle and the crew loses the duration.
static void test_reraise_does_not_restart_the_clock(void) {
  ccTestSetMillis(5000);
  al().raise("bms.cell_over", "Cell over voltage", Severity::Critical);
  ccTestSetMillis(9000);
  al().raise("bms.cell_over", "Cell over voltage", Severity::Critical);
  TEST_ASSERT_EQUAL_UINT32(5000, al().worstAlarm()->sinceMs);
}

static void test_reraise_after_clear_restarts_the_clock(void) {
  ccTestSetMillis(5000);
  al().raise("bms.cell_over", "Cell over voltage", Severity::Critical);
  al().clear("bms.cell_over");
  ccTestSetMillis(9000);
  al().raise("bms.cell_over", "Cell over voltage", Severity::Critical);
  TEST_ASSERT_EQUAL_UINT32(9000, al().worstAlarm()->sinceMs);
}

static void test_reraise_updates_text_and_severity(void) {
  al().raise("bms.v", "Pack low", Severity::Warning);
  al().raise("bms.v", "Pack critically low", Severity::Critical);
  TEST_ASSERT_EQUAL_size_t(1, al().activeCount());
  TEST_ASSERT_EQUAL_STRING("Pack critically low", al().worstAlarm()->text);
  TEST_ASSERT_EQUAL(Severity::Critical, al().worst());
}

static void test_clearing_an_unknown_id_is_harmless(void) {
  al().raise("bms.v", "Pack low", Severity::Warning);
  al().clear("nothing.here");
  TEST_ASSERT_EQUAL_size_t(1, al().activeCount());
}

// A slot is reused rather than reclaimed - count_ only grows (docs/ROADMAP.md).
// Cleared alarms stay in the table so they keep their identity; what matters is
// that they do not come back as active.
static void test_cleared_slot_is_reused(void) {
  al().raise("bms.v", "Pack low", Severity::Warning);
  TEST_ASSERT_EQUAL_size_t(1, al().size());
  al().clear("bms.v");
  al().raise("bms.v", "Pack low again", Severity::Warning);
  TEST_ASSERT_EQUAL_size_t(1, al().size());
  TEST_ASSERT_EQUAL_size_t(1, al().activeCount());
}

// ---- clearPrefix ------------------------------------------------------------

// The BMS drops every one of its own alarms when it goes offline; a stale
// "cell over voltage" from a battery we can no longer hear is worse than none.
static void test_clear_prefix_drops_only_that_family(void) {
  al().raise("bms.cell_over", "Cell over", Severity::Critical);
  al().raise("bms.pack_under", "Pack under", Severity::Warning);
  al().raise("water.low", "Water low", Severity::Warning);

  al().clearPrefix("bms.");
  TEST_ASSERT_EQUAL_size_t(1, al().activeCount());
  TEST_ASSERT_EQUAL_STRING("Water low", al().worstAlarm()->text);
}

static void test_clear_prefix_of_everything(void) {
  al().raise("bms.a", "A", Severity::Warning);
  al().raise("water.b", "B", Severity::Warning);
  al().clearPrefix("");
  TEST_ASSERT_FALSE(al().any());
}

// ---- worst ------------------------------------------------------------------

static void test_worst_picks_the_highest_severity(void) {
  al().raise("a", "Info", Severity::Info);
  al().raise("b", "Critical", Severity::Critical);
  al().raise("c", "Warning", Severity::Warning);
  TEST_ASSERT_EQUAL(Severity::Critical, al().worst());
  TEST_ASSERT_EQUAL_STRING("Critical", al().worstAlarm()->text);
}

static void test_worst_ignores_cleared_alarms(void) {
  al().raise("a", "Warning", Severity::Warning);
  al().raise("b", "Critical", Severity::Critical);
  al().clear("b");
  TEST_ASSERT_EQUAL(Severity::Warning, al().worst());
  TEST_ASSERT_EQUAL_STRING("Warning", al().worstAlarm()->text);
}

// Ties go to the first raised, so a screen showing "the worst alarm" does not
// flicker between two equals every refresh.
static void test_worst_keeps_the_first_of_equals(void) {
  al().raise("a", "First", Severity::Warning);
  al().raise("b", "Second", Severity::Warning);
  TEST_ASSERT_EQUAL_STRING("First", al().worstAlarm()->text);
}

// ---- silencing --------------------------------------------------------------

static void test_warning_and_critical_sound_info_does_not(void) {
  al().raise("a", "Just so you know", Severity::Info);
  TEST_ASSERT_FALSE(al().shouldSound());

  al().raise("b", "Pack low", Severity::Warning);
  TEST_ASSERT_TRUE(al().shouldSound());

  al().clear("b");
  al().raise("c", "Cell over", Severity::Critical);
  TEST_ASSERT_TRUE(al().shouldSound());
}

static void test_silence_stops_the_sound_but_not_the_alarm(void) {
  al().raise("bms.v", "Pack low", Severity::Warning);
  al().silence();
  TEST_ASSERT_TRUE(al().silenced());
  TEST_ASSERT_FALSE(al().shouldSound());
  TEST_ASSERT_TRUE(al().any());              // the condition is still true
  TEST_ASSERT_EQUAL(Severity::Warning, al().worst());
}

// The safety property. Silencing a fault must not silence the van: once
// everything clears, the next fault has to get attention again.
static void test_silence_lifts_when_everything_clears(void) {
  al().raise("bms.v", "Pack low", Severity::Warning);
  al().silence();
  TEST_ASSERT_TRUE(al().silenced());

  al().clear("bms.v");
  TEST_ASSERT_FALSE(al().silenced());

  al().raise("bms.cell_over", "Cell over", Severity::Critical);
  TEST_ASSERT_TRUE(al().shouldSound());
}

static void test_silence_survives_a_partial_clear(void) {
  // Two faults, one silenced, one cleared: still silenced, because something
  // is still wrong and the crew already acknowledged it.
  al().raise("a", "A", Severity::Warning);
  al().raise("b", "B", Severity::Warning);
  al().silence();
  al().clear("a");
  TEST_ASSERT_TRUE(al().silenced());
  TEST_ASSERT_FALSE(al().shouldSound());
}

static void test_clear_prefix_also_lifts_the_silence(void) {
  al().raise("bms.a", "A", Severity::Warning);
  al().silence();
  al().clearPrefix("bms.");
  TEST_ASSERT_FALSE(al().silenced());
}

// A new fault raised while silenced stays silent - the crew silenced the van,
// not one alarm. Recorded because the opposite is a defensible design and this
// documents which one is implemented.
static void test_a_new_fault_while_silenced_stays_silent(void) {
  al().raise("a", "A", Severity::Warning);
  al().silence();
  al().raise("b", "B", Severity::Critical);
  TEST_ASSERT_FALSE(al().shouldSound());
}

// ---- the table is finite ----------------------------------------------------

static void test_table_fills_without_overrunning(void) {
  char ids[Alarms::kMax + 4][8];
  for (size_t i = 0; i < Alarms::kMax + 4; i++) {
    snprintf(ids[i], sizeof(ids[i]), "a%zu", i);
    al().raise(ids[i], "x", Severity::Warning);
  }
  // Four are dropped with a log, and nothing walks off the end of table_.
  TEST_ASSERT_EQUAL_size_t(Alarms::kMax, al().size());
  TEST_ASSERT_EQUAL_size_t(Alarms::kMax, al().activeCount());
}

static void test_a_full_table_still_updates_known_alarms(void) {
  char ids[Alarms::kMax][8];
  for (size_t i = 0; i < Alarms::kMax; i++) {
    snprintf(ids[i], sizeof(ids[i]), "a%zu", i);
    al().raise(ids[i], "x", Severity::Info);
  }
  al().raise("a0", "escalated", Severity::Critical);
  TEST_ASSERT_EQUAL_size_t(Alarms::kMax, al().size());
  TEST_ASSERT_EQUAL(Severity::Critical, al().worst());
  TEST_ASSERT_EQUAL_STRING("escalated", al().worstAlarm()->text);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_quiet);
  RUN_TEST(test_raise_then_clear);
  RUN_TEST(test_raise_records_when_it_started);
  RUN_TEST(test_reraise_does_not_restart_the_clock);
  RUN_TEST(test_reraise_after_clear_restarts_the_clock);
  RUN_TEST(test_reraise_updates_text_and_severity);
  RUN_TEST(test_clearing_an_unknown_id_is_harmless);
  RUN_TEST(test_cleared_slot_is_reused);
  RUN_TEST(test_clear_prefix_drops_only_that_family);
  RUN_TEST(test_clear_prefix_of_everything);
  RUN_TEST(test_worst_picks_the_highest_severity);
  RUN_TEST(test_worst_ignores_cleared_alarms);
  RUN_TEST(test_worst_keeps_the_first_of_equals);
  RUN_TEST(test_warning_and_critical_sound_info_does_not);
  RUN_TEST(test_silence_stops_the_sound_but_not_the_alarm);
  RUN_TEST(test_silence_lifts_when_everything_clears);
  RUN_TEST(test_silence_survives_a_partial_clear);
  RUN_TEST(test_clear_prefix_also_lifts_the_silence);
  RUN_TEST(test_a_new_fault_while_silenced_stays_silent);
  RUN_TEST(test_table_fills_without_overrunning);
  RUN_TEST(test_a_full_table_still_updates_known_alarms);
  return UNITY_END();
}
