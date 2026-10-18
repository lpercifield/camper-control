// The day log's text format, its rotation rule, and the daily high and low.
//
// Everything here decides what ends up in a file that outlives the boot that
// wrote it, so the failures are the quiet kind: a row that parses into the
// wrong field, a rotation rule that deletes something it did not write, or a
// high and low that silently resets when the power blinks.

#include <unity.h>

#include <string.h>

#include "core/datalog.h"

using namespace cc;

static char buf[128];

void setUp() { ccTestSetMillis(1); }
void tearDown() {}

static Epoch at(uint16_t y, uint8_t mo, uint8_t d, uint8_t h = 0, uint8_t mi = 0) {
  DateTime dt;
  dt.year = y; dt.month = mo; dt.day = d; dt.hour = h; dt.minute = mi;
  return Clock::make(dt);
}

// ---- sensor keys ------------------------------------------------------------

static void test_key_is_the_last_six_hex_digits(void) {
  char key[kSensorKeyLen];
  TEST_ASSERT_TRUE(sensorKey("f3:a9:c3:46:0a:4a", key, sizeof(key)));
  TEST_ASSERT_EQUAL_STRING("460a4a", key);
}

static void test_key_is_case_and_separator_insensitive(void) {
  char a[kSensorKeyLen], b[kSensorKeyLen];
  TEST_ASSERT_TRUE(sensorKey("F3:A9:C3:46:0A:4A", a, sizeof(a)));
  TEST_ASSERT_TRUE(sensorKey("f3a9c3460a4a", b, sizeof(b)));
  TEST_ASSERT_EQUAL_STRING("460a4a", a);
  TEST_ASSERT_EQUAL_STRING(a, b);
}

static void test_a_bad_address_yields_no_key(void) {
  char key[kSensorKeyLen];
  TEST_ASSERT_FALSE(sensorKey("not-an-address", key, sizeof(key)));
  TEST_ASSERT_FALSE(sensorKey("aabb", key, sizeof(key)));  // too short
  TEST_ASSERT_FALSE(sensorKey(nullptr, key, sizeof(key)));
}

// ---- file names -------------------------------------------------------------

static void test_day_file_path(void) {
  TEST_ASSERT_EQUAL_STRING("/log/2026-09-17.csv",
                           dayFilePath(at(2026, 9, 17, 14, 3), buf, sizeof(buf)));
}

static void test_day_file_name_round_trips(void) {
  const Epoch e = at(2026, 9, 17);
  dayFilePath(e, buf, sizeof(buf));
  uint32_t day = 0;
  TEST_ASSERT_TRUE(parseDayFileName(buf, day));
  TEST_ASSERT_EQUAL_UINT32(Clock::dayIndexOf(e), day);
}

static void test_foreign_names_are_not_ours(void) {
  uint32_t day = 0;
  TEST_ASSERT_FALSE(parseDayFileName("settings.json", day));
  TEST_ASSERT_FALSE(parseDayFileName("2026-09-17.txt", day));
  TEST_ASSERT_FALSE(parseDayFileName("2026-9-17.csv", day));
  TEST_ASSERT_FALSE(parseDayFileName("20260917.csv", day));
  TEST_ASSERT_FALSE(parseDayFileName("2026-13-01.csv", day));  // no month 13
  TEST_ASSERT_FALSE(parseDayFileName("2026-02-30.csv", day));  // no Feb 30
  TEST_ASSERT_FALSE(parseDayFileName("", day));
  TEST_ASSERT_FALSE(parseDayFileName(nullptr, day));
}

// ---- rotation ---------------------------------------------------------------

static void test_rotation_keeps_the_recent_days(void) {
  const uint32_t today = Clock::dayIndexOf(at(2026, 9, 17));
  TEST_ASSERT_FALSE(shouldRotate("2026-09-17.csv", today, 14));  // today
  TEST_ASSERT_FALSE(shouldRotate("2026-09-04.csv", today, 14));  // 13 days old
  TEST_ASSERT_TRUE(shouldRotate("2026-09-03.csv", today, 14));   // 14 days old
  TEST_ASSERT_TRUE(shouldRotate("2025-01-01.csv", today, 14));
}

// Rotation runs against a shared filesystem, so it must never delete something
// it did not write. Anything unparseable is somebody else's.
static void test_rotation_never_touches_a_foreign_file(void) {
  const uint32_t today = Clock::dayIndexOf(at(2026, 9, 17));
  TEST_ASSERT_FALSE(shouldRotate("settings.json", today, 14));
  TEST_ASSERT_FALSE(shouldRotate("notes.txt", today, 1));
  TEST_ASSERT_FALSE(shouldRotate("", today, 1));
}

// A clock set backwards makes today look older than files already on disk.
// Deleting them as "future" data would throw away the real log.
static void test_a_file_dated_in_the_future_is_kept(void) {
  const uint32_t today = Clock::dayIndexOf(at(2026, 9, 17));
  TEST_ASSERT_FALSE(shouldRotate("2026-12-25.csv", today, 14));
}

static void test_keep_zero_days_still_keeps_everything(void) {
  // Guards against a config typo wiping the log directory on the next boot.
  const uint32_t today = Clock::dayIndexOf(at(2026, 9, 17));
  TEST_ASSERT_FALSE(shouldRotate("2020-01-01.csv", today, 0));
}

// ---- rows -------------------------------------------------------------------

static void test_row_round_trips(void) {
  LogRecord r;
  r.minuteOfDay = 843;
  strcpy(r.key, "460a4a");
  r.tempCx10 = 214;
  r.humPct = 38;
  r.battPct = 100;

  const size_t n = formatRow(r, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING("843,460a4a,214,38,100\n", buf);

  LogRecord back;
  buf[n - 1] = '\0';  // the reader strips the newline
  TEST_ASSERT_TRUE(parseRow(buf, back));
  TEST_ASSERT_EQUAL_UINT16(843, back.minuteOfDay);
  TEST_ASSERT_EQUAL_STRING("460a4a", back.key);
  TEST_ASSERT_EQUAL_INT16(214, back.tempCx10);
  TEST_ASSERT_EQUAL_INT16(38, back.humPct);
  TEST_ASSERT_EQUAL_INT16(100, back.battPct);
}

static void test_negative_temperatures_round_trip(void) {
  LogRecord r;
  r.minuteOfDay = 0;
  strcpy(r.key, "abcdef");
  r.tempCx10 = -123;
  r.humPct = 40;
  r.battPct = 80;
  const size_t n = formatRow(r, buf, sizeof(buf));
  buf[n - 1] = '\0';
  LogRecord back;
  TEST_ASSERT_TRUE(parseRow(buf, back));
  TEST_ASSERT_EQUAL_INT16(-123, back.tempCx10);
}

static void test_gaps_round_trip_as_gaps(void) {
  LogRecord r;
  r.minuteOfDay = 5;
  strcpy(r.key, "460a4a");
  r.tempCx10 = History::kNoValue;
  r.humPct = History::kNoValue;
  r.battPct = History::kNoValue;
  const size_t n = formatRow(r, buf, sizeof(buf));
  buf[n - 1] = '\0';
  LogRecord back;
  TEST_ASSERT_TRUE(parseRow(buf, back));
  TEST_ASSERT_EQUAL_INT16(History::kNoValue, back.tempCx10);
  TEST_ASSERT_EQUAL_INT16(History::kNoValue, back.humPct);
}

static void test_headers_and_blanks_are_not_rows(void) {
  LogRecord r;
  TEST_ASSERT_FALSE(parseRow("# camper-control log 1", r));
  TEST_ASSERT_FALSE(parseRow("# sensor 460a4a f3:a9:c3:46:0a:4a Fridge", r));
  TEST_ASSERT_FALSE(parseRow("", r));
  TEST_ASSERT_FALSE(parseRow(nullptr, r));
}

// A log cut short by a power failure ends mid-row. Half a row must not parse
// into a whole reading.
static void test_a_truncated_row_is_rejected(void) {
  LogRecord r;
  TEST_ASSERT_FALSE(parseRow("843,460a4a,214", r));
  TEST_ASSERT_FALSE(parseRow("843,460a4a", r));
  TEST_ASSERT_FALSE(parseRow("843,", r));
  TEST_ASSERT_FALSE(parseRow("843", r));
}

// The reason the key is parsed as hex text rather than with strtol: "460a4a"
// would read as the number 460 and shift every later field one place along,
// turning a temperature into a humidity without any error at all.
static void test_a_key_is_not_read_as_a_number(void) {
  LogRecord r;
  TEST_ASSERT_TRUE(parseRow("843,460a4a,214,38,100", r));
  TEST_ASSERT_EQUAL_INT16(214, r.tempCx10);
  TEST_ASSERT_EQUAL_INT16(38, r.humPct);

  // And a key of the wrong length is rejected rather than silently padded.
  TEST_ASSERT_FALSE(parseRow("843,460a4,214,38,100", r));
  TEST_ASSERT_FALSE(parseRow("843,460a4aa,214,38,100", r));
  TEST_ASSERT_FALSE(parseRow("843,46zzzz,214,38,100", r));
}

static void test_minute_out_of_range_is_rejected(void) {
  LogRecord r;
  TEST_ASSERT_FALSE(parseRow("1440,460a4a,214,38,100", r));
  TEST_ASSERT_FALSE(parseRow("-1,460a4a,214,38,100", r));
  TEST_ASSERT_TRUE(parseRow("1439,460a4a,214,38,100", r));
}

static void test_a_row_without_a_key_is_not_written(void) {
  LogRecord r;
  r.minuteOfDay = 1;
  r.key[0] = '\0';
  TEST_ASSERT_EQUAL_UINT32(0, formatRow(r, buf, sizeof(buf)));
}

// Truncation must produce nothing rather than half a row - a partial line
// would corrupt the file for every reader after it.
static void test_a_row_that_does_not_fit_writes_nothing(void) {
  LogRecord r;
  r.minuteOfDay = 1439;
  strcpy(r.key, "460a4a");
  r.tempCx10 = -32768;
  r.humPct = 100;
  r.battPct = 100;
  char tiny[8];
  TEST_ASSERT_EQUAL_UINT32(0, formatRow(r, tiny, sizeof(tiny)));
}

static void test_sensor_header(void) {
  const size_t n = formatSensorHeader("460a4a", "f3:a9:c3:46:0a:4a", "Fridge",
                                      buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING("# sensor 460a4a f3:a9:c3:46:0a:4a Fridge\n", buf);
  // A header is not a row, so replay skips it.
  LogRecord r;
  TEST_ASSERT_FALSE(parseRow(buf, r));
}

static void test_a_nameless_sensor_still_gets_a_header(void) {
  formatSensorHeader("460a4a", "f3:a9:c3:46:0a:4a", "", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("# sensor 460a4a f3:a9:c3:46:0a:4a -\n", buf);
}

// ---- the daily high and low -------------------------------------------------

static void test_extremes_start_empty(void) {
  DayExtremes d;
  d.reset(100);
  TEST_ASSERT_FALSE(d.haveTemp);
  TEST_ASSERT_FALSE(d.haveHum);
  TEST_ASSERT_EQUAL_UINT32(100, d.day);
}

static void test_extremes_track_both_ends(void) {
  DayExtremes d;
  d.reset(1);
  d.observe(true, 20.0f, true, 50.0f);
  d.observe(true, 25.5f, true, 40.0f);
  d.observe(true, 18.0f, true, 60.0f);
  TEST_ASSERT_TRUE(d.haveTemp);
  TEST_ASSERT_EQUAL_FLOAT(18.0f, d.tMinC);
  TEST_ASSERT_EQUAL_FLOAT(25.5f, d.tMaxC);
  TEST_ASSERT_EQUAL_FLOAT(40.0f, d.hMin);
  TEST_ASSERT_EQUAL_FLOAT(60.0f, d.hMax);
}

// The first reading must seed both ends. Starting from 0 would make every
// daily low 0 C until the van actually froze.
static void test_the_first_reading_seeds_both_ends(void) {
  DayExtremes d;
  d.reset(1);
  d.observe(true, 20.0f, true, 50.0f);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, d.tMinC);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, d.tMaxC);
}

static void test_negative_extremes(void) {
  DayExtremes d;
  d.reset(1);
  d.observe(true, -5.0f, false, 0.0f);
  d.observe(true, -12.0f, false, 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(-12.0f, d.tMinC);
  TEST_ASSERT_EQUAL_FLOAT(-5.0f, d.tMaxC);
}

static void test_a_missing_quantity_does_not_count(void) {
  DayExtremes d;
  d.reset(1);
  d.observe(false, 0.0f, true, 50.0f);
  TEST_ASSERT_FALSE(d.haveTemp);
  TEST_ASSERT_TRUE(d.haveHum);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, d.hMin);
}

static void test_reset_clears_the_previous_day(void) {
  DayExtremes d;
  d.reset(1);
  d.observe(true, 20.0f, true, 50.0f);
  d.reset(2);
  TEST_ASSERT_FALSE(d.haveTemp);
  TEST_ASSERT_EQUAL_UINT32(2, d.day);
}

// Replaying the day's file after a reboot is what stops the high and low
// meaning "since this boot" - which on a van with a flaky 12 V feed could be
// five minutes ago.
static void test_extremes_rebuild_from_logged_rows(void) {
  DayExtremes d;
  d.reset(1);
  LogRecord a; strcpy(a.key, "460a4a"); a.minuteOfDay = 1;
  a.tempCx10 = 185; a.humPct = 44; a.battPct = 90;
  LogRecord b; strcpy(b.key, "460a4a"); b.minuteOfDay = 2;
  b.tempCx10 = 260; b.humPct = 30; b.battPct = 90;
  d.observeRecord(a);
  d.observeRecord(b);
  TEST_ASSERT_EQUAL_FLOAT(18.5f, d.tMinC);
  TEST_ASSERT_EQUAL_FLOAT(26.0f, d.tMaxC);
  TEST_ASSERT_EQUAL_FLOAT(30.0f, d.hMin);
  TEST_ASSERT_EQUAL_FLOAT(44.0f, d.hMax);
}

// A gap in the file is an outage, not a reading of -3276.8 C.
static void test_a_logged_gap_never_becomes_an_extreme(void) {
  DayExtremes d;
  d.reset(1);
  LogRecord gap; strcpy(gap.key, "460a4a"); gap.minuteOfDay = 1;
  gap.tempCx10 = History::kNoValue;
  gap.humPct = History::kNoValue;
  gap.battPct = History::kNoValue;
  LogRecord real; strcpy(real.key, "460a4a"); real.minuteOfDay = 2;
  real.tempCx10 = 200; real.humPct = 50; real.battPct = 90;
  d.observeRecord(gap);
  d.observeRecord(real);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, d.tMinC);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, d.tMaxC);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_key_is_the_last_six_hex_digits);
  RUN_TEST(test_key_is_case_and_separator_insensitive);
  RUN_TEST(test_a_bad_address_yields_no_key);
  RUN_TEST(test_day_file_path);
  RUN_TEST(test_day_file_name_round_trips);
  RUN_TEST(test_foreign_names_are_not_ours);
  RUN_TEST(test_rotation_keeps_the_recent_days);
  RUN_TEST(test_rotation_never_touches_a_foreign_file);
  RUN_TEST(test_a_file_dated_in_the_future_is_kept);
  RUN_TEST(test_keep_zero_days_still_keeps_everything);
  RUN_TEST(test_row_round_trips);
  RUN_TEST(test_negative_temperatures_round_trip);
  RUN_TEST(test_gaps_round_trip_as_gaps);
  RUN_TEST(test_headers_and_blanks_are_not_rows);
  RUN_TEST(test_a_truncated_row_is_rejected);
  RUN_TEST(test_a_key_is_not_read_as_a_number);
  RUN_TEST(test_minute_out_of_range_is_rejected);
  RUN_TEST(test_a_row_without_a_key_is_not_written);
  RUN_TEST(test_a_row_that_does_not_fit_writes_nothing);
  RUN_TEST(test_sensor_header);
  RUN_TEST(test_a_nameless_sensor_still_gets_a_header);
  RUN_TEST(test_extremes_start_empty);
  RUN_TEST(test_extremes_track_both_ends);
  RUN_TEST(test_the_first_reading_seeds_both_ends);
  RUN_TEST(test_negative_extremes);
  RUN_TEST(test_a_missing_quantity_does_not_count);
  RUN_TEST(test_reset_clears_the_previous_day);
  RUN_TEST(test_extremes_rebuild_from_logged_rows);
  RUN_TEST(test_a_logged_gap_never_becomes_an_extreme);
  return UNITY_END();
}
