// The wall clock, and the calendar behind it.
//
// This board has no RTC, so every timestamp in the log and every "daily" high
// and low rests on this arithmetic. Two things here are easy to get wrong and
// silent when you do: leap years, and losing a fraction of a second on every
// tick so the clock runs slow by minutes a day.

#include <unity.h>

#include "core/clock.h"

using namespace cc;

static Clock& clk() { return Clock::instance(); }

void setUp() {
  ccTestSetMillis(1);
  clk().testReset();
}
void tearDown() { clk().testReset(); }

// ---- the calendar -----------------------------------------------------------

static void test_epoch_is_1970(void) {
  const DateTime dt = Clock::breakDown(0);
  TEST_ASSERT_EQUAL_UINT16(1970, dt.year);
  TEST_ASSERT_EQUAL_UINT8(1, dt.month);
  TEST_ASSERT_EQUAL_UINT8(1, dt.day);
  TEST_ASSERT_EQUAL_UINT8(0, dt.hour);
}

static void test_a_known_timestamp(void) {
  // 2026-09-17 14:03:00 local.
  DateTime dt;
  dt.year = 2026; dt.month = 9; dt.day = 17;
  dt.hour = 14; dt.minute = 3; dt.second = 0;
  const Epoch e = Clock::make(dt);

  const DateTime back = Clock::breakDown(e);
  TEST_ASSERT_EQUAL_UINT16(2026, back.year);
  TEST_ASSERT_EQUAL_UINT8(9, back.month);
  TEST_ASSERT_EQUAL_UINT8(17, back.day);
  TEST_ASSERT_EQUAL_UINT8(14, back.hour);
  TEST_ASSERT_EQUAL_UINT8(3, back.minute);
}

// Every day for eleven years, there and back. A leap-year bug cannot hide from
// this, and leap years are where hand-rolled calendar code goes wrong.
static void test_round_trip_every_day_for_a_decade(void) {
  DateTime start;
  start.year = 2020; start.month = 1; start.day = 1;
  Epoch e = Clock::make(start);
  for (int i = 0; i < 366 * 11; i++) {
    const DateTime dt = Clock::breakDown(e);
    TEST_ASSERT_EQUAL_UINT32(e, Clock::make(dt));
    e += kSecondsPerDay;
  }
}

static void test_leap_years(void) {
  TEST_ASSERT_TRUE(Clock::isLeapYear(2024));
  TEST_ASSERT_TRUE(Clock::isLeapYear(2000));   // divisible by 400
  TEST_ASSERT_FALSE(Clock::isLeapYear(1900));  // divisible by 100, not 400
  TEST_ASSERT_FALSE(Clock::isLeapYear(2026));
  TEST_ASSERT_EQUAL_UINT8(29, Clock::daysInMonth(2024, 2));
  TEST_ASSERT_EQUAL_UINT8(28, Clock::daysInMonth(2026, 2));
  TEST_ASSERT_EQUAL_UINT8(31, Clock::daysInMonth(2026, 12));
}

static void test_leap_day_itself(void) {
  DateTime dt;
  dt.year = 2024; dt.month = 2; dt.day = 29;
  dt.hour = 12;
  const DateTime back = Clock::breakDown(Clock::make(dt));
  TEST_ASSERT_EQUAL_UINT16(2024, back.year);
  TEST_ASSERT_EQUAL_UINT8(2, back.month);
  TEST_ASSERT_EQUAL_UINT8(29, back.day);
}

static void test_formatting(void) {
  DateTime dt;
  dt.year = 2026; dt.month = 9; dt.day = 7;
  dt.hour = 4; dt.minute = 5;
  const Epoch e = Clock::make(dt);

  char buf[32];
  TEST_ASSERT_EQUAL_STRING("2026-09-07", Clock::formatDate(e, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("04:05", Clock::formatTime(e, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("2026-09-07 04:05",
                           Clock::formatStamp(e, buf, sizeof(buf)));
}

static void test_format_survives_a_useless_buffer(void) {
  char buf[4];
  TEST_ASSERT_EQUAL_STRING("", Clock::formatDate(0, nullptr, 8));
  TEST_ASSERT_EQUAL_STRING("", Clock::formatDate(0, buf, 0));
}

// ---- ticking ----------------------------------------------------------------

static void test_unset_clock_does_not_run(void) {
  TEST_ASSERT_FALSE(clk().isSet());
  ccTestAdvanceMillis(10000);
  clk().tick(millis());
  TEST_ASSERT_EQUAL_UINT32(0, clk().now());
}

static void test_clock_advances_with_millis(void) {
  clk().set(1000, millis());
  ccTestAdvanceMillis(5000);
  clk().tick(millis());
  TEST_ASSERT_EQUAL_UINT32(1005, clk().now());
}

static void test_sub_second_ticks_do_not_advance_it(void) {
  clk().set(1000, millis());
  ccTestAdvanceMillis(999);
  clk().tick(millis());
  TEST_ASSERT_EQUAL_UINT32(1000, clk().now());
}

// The one that would go unnoticed: if tick() rebased to nowMs instead of
// keeping the remainder, a caller ticking every 1500 ms would throw away half a
// second each time and the clock would lose twelve hours a day.
static void test_no_drift_from_awkward_tick_intervals(void) {
  clk().set(0, millis());
  for (int i = 0; i < 1000; i++) {
    ccTestAdvanceMillis(1500);
    clk().tick(millis());
  }
  TEST_ASSERT_EQUAL_UINT32(1500, clk().now());  // 1000 x 1.5 s exactly
}

static void test_many_small_ticks_still_keep_time(void) {
  clk().set(0, millis());
  for (int i = 0; i < 10000; i++) {
    ccTestAdvanceMillis(7);
    clk().tick(millis());
  }
  TEST_ASSERT_EQUAL_UINT32(70, clk().now());
}

// This device runs for months in a van, so it will see the 49-day millis()
// wrap. Signed arithmetic here would stop the clock dead.
static void test_clock_survives_the_millis_wrap(void) {
  ccTestSetMillis(0xFFFFFFFFUL - 5000);
  clk().set(1000, millis());
  ccTestAdvanceMillis(10000);  // straight through the wrap
  clk().tick(millis());
  TEST_ASSERT_EQUAL_UINT32(1010, clk().now());
}

// ---- days -------------------------------------------------------------------

static void test_day_index_changes_at_local_midnight(void) {
  DateTime dt;
  dt.year = 2026; dt.month = 9; dt.day = 17; dt.hour = 23; dt.minute = 59;
  dt.second = 59;
  const Epoch before = Clock::make(dt);
  TEST_ASSERT_EQUAL_UINT32(Clock::dayIndexOf(before) + 1,
                           Clock::dayIndexOf(before + 1));
}

static void test_minute_of_day(void) {
  DateTime dt;
  dt.year = 2026; dt.month = 9; dt.day = 17; dt.hour = 14; dt.minute = 3;
  clk().set(Clock::make(dt), millis());
  TEST_ASSERT_EQUAL_UINT32(14 * 60 + 3, clk().minuteOfDay());
}

// ---- how it was set ---------------------------------------------------------

static void test_a_restored_clock_says_it_is_approximate(void) {
  clk().set(1000, millis(), /*approximate=*/true);
  TEST_ASSERT_TRUE(clk().isSet());
  TEST_ASSERT_TRUE(clk().approximate());
  // And a person setting it clears that.
  clk().set(2000, millis(), /*approximate=*/false);
  TEST_ASSERT_FALSE(clk().approximate());
}

// ---- stepping the fields by hand --------------------------------------------
// The only way this device ever learns the time, so the arithmetic behind the
// arrows is worth pinning. The failures are all "a field you did not touch
// moved", which is how a hand-set clock ends up a month out.

static DateTime dt(uint16_t y, uint8_t mo, uint8_t d, uint8_t h = 0, uint8_t mi = 0) {
  DateTime v;
  v.year = y; v.month = mo; v.day = d; v.hour = h; v.minute = mi;
  return v;
}

static void test_each_field_steps(void) {
  DateTime v = dt(2026, 6, 15, 10, 30);
  stepDateField(v, DateField::Year, 1);
  TEST_ASSERT_EQUAL_UINT16(2027, v.year);
  stepDateField(v, DateField::Month, 1);
  TEST_ASSERT_EQUAL_UINT8(7, v.month);
  stepDateField(v, DateField::Day, 1);
  TEST_ASSERT_EQUAL_UINT8(16, v.day);
  stepDateField(v, DateField::Hour, 1);
  TEST_ASSERT_EQUAL_UINT8(11, v.hour);
  stepDateField(v, DateField::Minute, 1);
  TEST_ASSERT_EQUAL_UINT8(31, v.minute);
}

static void test_fields_wrap_at_both_ends(void) {
  DateTime v = dt(2026, 12, 15, 23, 59);
  stepDateField(v, DateField::Month, 1);
  TEST_ASSERT_EQUAL_UINT8(1, v.month);
  stepDateField(v, DateField::Hour, 1);
  TEST_ASSERT_EQUAL_UINT8(0, v.hour);
  stepDateField(v, DateField::Minute, 1);
  TEST_ASSERT_EQUAL_UINT8(0, v.minute);

  v = dt(2026, 1, 15, 0, 0);
  stepDateField(v, DateField::Month, -1);
  TEST_ASSERT_EQUAL_UINT8(12, v.month);
  stepDateField(v, DateField::Hour, -1);
  TEST_ASSERT_EQUAL_UINT8(23, v.hour);
  stepDateField(v, DateField::Minute, -1);
  TEST_ASSERT_EQUAL_UINT8(59, v.minute);
}

// The fields are unsigned bytes. Stepping an hour down from 0 without widening
// first gives 255, not 23.
static void test_stepping_down_from_zero_does_not_underflow(void) {
  DateTime v = dt(2026, 6, 15, 0, 0);
  stepDateField(v, DateField::Hour, -1);
  TEST_ASSERT_EQUAL_UINT8(23, v.hour);
  stepDateField(v, DateField::Minute, -1);
  TEST_ASSERT_EQUAL_UINT8(59, v.minute);
  v = dt(2026, 6, 1);
  stepDateField(v, DateField::Day, -1);
  TEST_ASSERT_EQUAL_UINT8(30, v.day);  // June has 30
}

static void test_year_wraps_within_its_range(void) {
  DateTime v = dt(kMaxYear, 6, 15);
  stepDateField(v, DateField::Year, 1);
  TEST_ASSERT_EQUAL_UINT16(kMinYear, v.year);
  stepDateField(v, DateField::Year, -1);
  TEST_ASSERT_EQUAL_UINT16(kMaxYear, v.year);
}

static void test_day_wraps_within_the_current_month(void) {
  DateTime v = dt(2026, 2, 28);
  stepDateField(v, DateField::Day, 1);
  TEST_ASSERT_EQUAL_UINT8(1, v.day);  // 2026 February has 28 days

  v = dt(2024, 2, 28);
  stepDateField(v, DateField::Day, 1);
  TEST_ASSERT_EQUAL_UINT8(29, v.day);  // 2024 is a leap year
}

// The one that makes a setter untrustworthy: stepping the month must not move
// the day into a different month than the one you selected.
static void test_shortening_the_month_clamps_the_day(void) {
  DateTime v = dt(2026, 1, 31);
  stepDateField(v, DateField::Month, 1);
  TEST_ASSERT_EQUAL_UINT8(2, v.month);
  TEST_ASSERT_EQUAL_UINT8(28, v.day);  // not 3 March

  v = dt(2026, 3, 31);
  stepDateField(v, DateField::Month, -1);
  TEST_ASSERT_EQUAL_UINT8(2, v.month);
  TEST_ASSERT_EQUAL_UINT8(28, v.day);
}

// Same trap through the year: 29 February only exists in a leap year.
static void test_leaving_a_leap_year_clamps_the_day(void) {
  DateTime v = dt(2024, 2, 29);
  stepDateField(v, DateField::Year, 1);
  TEST_ASSERT_EQUAL_UINT16(2025, v.year);
  TEST_ASSERT_EQUAL_UINT8(28, v.day);
}

// Stepping one field must leave the others alone - no carry, ever. The whole
// point of a per-field stepper is that it is predictable.
static void test_stepping_one_field_never_carries(void) {
  DateTime v = dt(2026, 12, 31, 23, 59);
  stepDateField(v, DateField::Minute, 1);
  TEST_ASSERT_EQUAL_UINT8(0, v.minute);
  TEST_ASSERT_EQUAL_UINT8(23, v.hour);   // not rolled to the next day
  TEST_ASSERT_EQUAL_UINT8(31, v.day);
  TEST_ASSERT_EQUAL_UINT8(12, v.month);
  TEST_ASSERT_EQUAL_UINT16(2026, v.year);
}

static void test_a_big_step_still_lands_in_range(void) {
  // Long-press repeat sends many steps; none of them may escape the range.
  DateTime v = dt(2026, 6, 15, 10, 30);
  for (int i = 0; i < 200; i++) stepDateField(v, DateField::Minute, 1);
  TEST_ASSERT_TRUE(v.minute <= 59);
  for (int i = 0; i < 200; i++) stepDateField(v, DateField::Year, 1);
  TEST_ASSERT_TRUE(v.year >= kMinYear && v.year <= kMaxYear);
}

// Whatever the arrows build must be a time the rest of the system can use.
static void test_the_result_is_always_a_real_timestamp(void) {
  DateTime v = dt(2026, 1, 31);
  for (int i = 0; i < 24; i++) {
    stepDateField(v, DateField::Month, 1);
    const Epoch e = Clock::make(v);
    const DateTime back = Clock::breakDown(e);
    TEST_ASSERT_EQUAL_UINT16(v.year, back.year);
    TEST_ASSERT_EQUAL_UINT8(v.month, back.month);
    TEST_ASSERT_EQUAL_UINT8(v.day, back.day);
  }
}

static void test_every_field_has_a_name(void) {
  for (int i = 0; i < (int)DateField::COUNT; i++) {
    TEST_ASSERT_NOT_NULL(dateFieldName((DateField)i));
    TEST_ASSERT_TRUE(dateFieldName((DateField)i)[0] != '\0');
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_epoch_is_1970);
  RUN_TEST(test_a_known_timestamp);
  RUN_TEST(test_round_trip_every_day_for_a_decade);
  RUN_TEST(test_leap_years);
  RUN_TEST(test_leap_day_itself);
  RUN_TEST(test_formatting);
  RUN_TEST(test_format_survives_a_useless_buffer);
  RUN_TEST(test_unset_clock_does_not_run);
  RUN_TEST(test_clock_advances_with_millis);
  RUN_TEST(test_sub_second_ticks_do_not_advance_it);
  RUN_TEST(test_no_drift_from_awkward_tick_intervals);
  RUN_TEST(test_many_small_ticks_still_keep_time);
  RUN_TEST(test_clock_survives_the_millis_wrap);
  RUN_TEST(test_day_index_changes_at_local_midnight);
  RUN_TEST(test_minute_of_day);
  RUN_TEST(test_a_restored_clock_says_it_is_approximate);
  RUN_TEST(test_each_field_steps);
  RUN_TEST(test_fields_wrap_at_both_ends);
  RUN_TEST(test_stepping_down_from_zero_does_not_underflow);
  RUN_TEST(test_year_wraps_within_its_range);
  RUN_TEST(test_day_wraps_within_the_current_month);
  RUN_TEST(test_shortening_the_month_clamps_the_day);
  RUN_TEST(test_leaving_a_leap_year_clamps_the_day);
  RUN_TEST(test_stepping_one_field_never_carries);
  RUN_TEST(test_a_big_step_still_lands_in_range);
  RUN_TEST(test_the_result_is_always_a_real_timestamp);
  RUN_TEST(test_every_field_has_a_name);
  return UNITY_END();
}
