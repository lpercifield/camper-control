// The trend ring behind the Climate page's graph.
//
// It is a ring buffer with a clock in it, which is two things that go wrong
// quietly: an off-by-one in the wrap puts the oldest sample where the newest
// should be, and a signed subtraction stops sampling altogether 49 days in.
// Neither would crash, and both would draw a plausible, wrong line.

#include <unity.h>

#include "core/history.h"

using namespace cc;

static History h;

void setUp() { h.clear(); }
void tearDown() {}

// Records at nowMs, asserting it actually took the sample.
static void feed(uint32_t ms, float tempC, float humPct) {
  TEST_ASSERT_TRUE(h.record(ms, true, tempC, true, humPct));
}

// ---- the interval gate ------------------------------------------------------

static void test_starts_empty(void) {
  TEST_ASSERT_TRUE(h.empty());
  TEST_ASSERT_EQUAL_UINT32(0, h.count());
  TEST_ASSERT_EQUAL_UINT32(0, h.spanMs());
}

// A graph that shows nothing for the first minute looks broken, so the first
// call always lands.
static void test_first_sample_is_taken_immediately(void) {
  TEST_ASSERT_TRUE(h.record(0, true, 20.0f, true, 50.0f));
  TEST_ASSERT_EQUAL_UINT32(1, h.count());
}

static void test_records_are_rate_limited(void) {
  feed(1000, 20.0f, 50.0f);
  TEST_ASSERT_FALSE(h.record(1000 + History::kIntervalMs - 1, true, 21.0f, true, 51.0f));
  TEST_ASSERT_EQUAL_UINT32(1, h.count());
  TEST_ASSERT_TRUE(h.record(1000 + History::kIntervalMs, true, 21.0f, true, 51.0f));
  TEST_ASSERT_EQUAL_UINT32(2, h.count());
}

// The device lives in a van and is expected to run for months, so millis()
// wrapping is a real event rather than a theoretical one. Unsigned subtraction
// is what keeps it sampling across the boundary.
static void test_sampling_survives_the_millis_wrap(void) {
  const uint32_t nearEnd = 0xFFFFFFFFUL - (History::kIntervalMs / 2);
  TEST_ASSERT_TRUE(h.record(nearEnd, true, 20.0f, true, 50.0f));
  // Half an interval before the wrap plus a full one lands just after zero.
  const uint32_t afterWrap = nearEnd + History::kIntervalMs;
  TEST_ASSERT_TRUE(h.record(afterWrap, true, 21.0f, true, 51.0f));
  TEST_ASSERT_EQUAL_UINT32(2, h.count());
}

// ---- ordering and the wrap --------------------------------------------------

static void test_samples_read_back_oldest_first(void) {
  for (int i = 0; i < 3; i++) {
    feed(i * History::kIntervalMs, 20.0f + i, 50.0f + i);
  }
  TEST_ASSERT_EQUAL_INT16(200, h.tempAt(0));
  TEST_ASSERT_EQUAL_INT16(210, h.tempAt(1));
  TEST_ASSERT_EQUAL_INT16(220, h.tempAt(2));
  TEST_ASSERT_EQUAL_INT16(50, h.humAt(0));
  TEST_ASSERT_EQUAL_INT16(52, h.humAt(2));
}

static void test_count_stops_at_capacity(void) {
  for (size_t i = 0; i < History::kSamples + 10; i++) {
    feed((uint32_t)i * History::kIntervalMs, 20.0f, 50.0f);
  }
  TEST_ASSERT_EQUAL_UINT32(History::kSamples, h.count());
}

// The one that actually bites: once the ring wraps, index 0 must be the oldest
// surviving sample, not the first one ever written.
static void test_oldest_is_dropped_once_it_wraps(void) {
  for (size_t i = 0; i < History::kSamples + 5; i++) {
    feed((uint32_t)i * History::kIntervalMs, (float)i, 50.0f);
  }
  TEST_ASSERT_EQUAL_INT16((int16_t)(5 * 10), h.tempAt(0));
  TEST_ASSERT_EQUAL_INT16((int16_t)((History::kSamples + 4) * 10),
                          h.tempAt(History::kSamples - 1));
}

static void test_reading_past_the_end_is_a_gap(void) {
  feed(0, 20.0f, 50.0f);
  TEST_ASSERT_EQUAL_INT16(History::kNoValue, h.tempAt(1));
  TEST_ASSERT_EQUAL_INT16(History::kNoValue, h.humAt(99));
}

static void test_clear_empties_it(void) {
  for (int i = 0; i < 5; i++) feed(i * History::kIntervalMs, 20.0f, 50.0f);
  h.clear();
  TEST_ASSERT_TRUE(h.empty());
  // And the clock with it: the next sample must land rather than be rate-limited
  // against a reading that is no longer about this sensor.
  TEST_ASSERT_TRUE(h.record(5 * History::kIntervalMs, true, 20.0f, true, 50.0f));
}

// ---- gaps -------------------------------------------------------------------

// A sensor that has gone quiet gets a gap, not a repeat of its last reading.
// Drawing a flat line through an outage is the kind of plausible lie this
// project is careful about.
static void test_a_missing_reading_is_a_gap_not_a_repeat(void) {
  feed(0, 20.0f, 50.0f);
  TEST_ASSERT_TRUE(h.record(History::kIntervalMs, false, 0.0f, false, 0.0f));
  TEST_ASSERT_EQUAL_INT16(200, h.tempAt(0));
  TEST_ASSERT_EQUAL_INT16(History::kNoValue, h.tempAt(1));
  TEST_ASSERT_EQUAL_INT16(History::kNoValue, h.humAt(1));
}

// A SwitchBot sends battery and readings in separate advertisements, so one
// quantity present and the other absent is the normal case, not an error.
static void test_one_quantity_can_be_present_without_the_other(void) {
  TEST_ASSERT_TRUE(h.record(0, true, 20.0f, false, 0.0f));
  TEST_ASSERT_EQUAL_INT16(200, h.tempAt(0));
  TEST_ASSERT_EQUAL_INT16(History::kNoValue, h.humAt(0));
}

// ---- quantisation -----------------------------------------------------------

static void test_temperature_keeps_a_tenth(void) {
  feed(0, 21.44f, 50.0f);
  TEST_ASSERT_EQUAL_INT16(214, h.tempAt(0));
}

static void test_negative_temperatures_round_away_from_zero(void) {
  feed(0, -5.26f, 50.0f);
  TEST_ASSERT_EQUAL_INT16(-53, h.tempAt(0));
}

// A decoder that produces nonsense must not wrap into a plausible reading.
static void test_absurd_values_clamp_rather_than_wrap(void) {
  feed(0, 9000.0f, 50.0f);
  TEST_ASSERT_EQUAL_INT16(32000, h.tempAt(0));
  h.clear();
  feed(0, -9000.0f, 50.0f);
  TEST_ASSERT_EQUAL_INT16(-32000, h.tempAt(0));
}

// ---- ranges -----------------------------------------------------------------

static void test_range_of_nothing_is_false(void) {
  float lo = 99.0f, hi = 99.0f;
  TEST_ASSERT_FALSE(h.tempRangeC(lo, hi));
  TEST_ASSERT_EQUAL_FLOAT(99.0f, lo);  // untouched
  TEST_ASSERT_EQUAL_FLOAT(99.0f, hi);
}

static void test_range_ignores_gaps(void) {
  feed(0, 20.0f, 40.0f);
  TEST_ASSERT_TRUE(h.record(History::kIntervalMs, false, 0.0f, false, 0.0f));
  feed(2 * History::kIntervalMs, 24.0f, 60.0f);

  float lo = 0.0f, hi = 0.0f;
  TEST_ASSERT_TRUE(h.tempRangeC(lo, hi));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, lo);
  TEST_ASSERT_EQUAL_FLOAT(24.0f, hi);

  TEST_ASSERT_TRUE(h.humRange(lo, hi));
  TEST_ASSERT_EQUAL_FLOAT(40.0f, lo);
  TEST_ASSERT_EQUAL_FLOAT(60.0f, hi);
}

// A gap is INT16_MIN, which is also the smallest number there is. A range that
// forgot to skip it would report -3276.8 C and squash the whole plot flat.
static void test_a_gap_never_becomes_the_minimum(void) {
  TEST_ASSERT_TRUE(h.record(0, false, 0.0f, false, 0.0f));
  feed(History::kIntervalMs, 20.0f, 50.0f);
  float lo = 0.0f, hi = 0.0f;
  TEST_ASSERT_TRUE(h.tempRangeC(lo, hi));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, lo);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, hi);
}

static void test_range_of_one_sample_is_a_point(void) {
  feed(0, 20.0f, 50.0f);
  float lo = 0.0f, hi = 0.0f;
  TEST_ASSERT_TRUE(h.tempRangeC(lo, hi));
  TEST_ASSERT_EQUAL_FLOAT(20.0f, lo);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, hi);
}

// ---- span -------------------------------------------------------------------

static void test_span_needs_two_samples(void) {
  feed(0, 20.0f, 50.0f);
  TEST_ASSERT_EQUAL_UINT32(0, h.spanMs());
  feed(History::kIntervalMs, 20.0f, 50.0f);
  TEST_ASSERT_EQUAL_UINT32(History::kIntervalMs, h.spanMs());
}

static void test_span_saturates_at_the_window(void) {
  for (size_t i = 0; i < History::kSamples + 20; i++) {
    feed((uint32_t)i * History::kIntervalMs, 20.0f, 50.0f);
  }
  TEST_ASSERT_EQUAL_UINT32((History::kSamples - 1) * History::kIntervalMs,
                           h.spanMs());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_empty);
  RUN_TEST(test_first_sample_is_taken_immediately);
  RUN_TEST(test_records_are_rate_limited);
  RUN_TEST(test_sampling_survives_the_millis_wrap);
  RUN_TEST(test_samples_read_back_oldest_first);
  RUN_TEST(test_count_stops_at_capacity);
  RUN_TEST(test_oldest_is_dropped_once_it_wraps);
  RUN_TEST(test_reading_past_the_end_is_a_gap);
  RUN_TEST(test_clear_empties_it);
  RUN_TEST(test_a_missing_reading_is_a_gap_not_a_repeat);
  RUN_TEST(test_one_quantity_can_be_present_without_the_other);
  RUN_TEST(test_temperature_keeps_a_tenth);
  RUN_TEST(test_negative_temperatures_round_away_from_zero);
  RUN_TEST(test_absurd_values_clamp_rather_than_wrap);
  RUN_TEST(test_range_of_nothing_is_false);
  RUN_TEST(test_range_ignores_gaps);
  RUN_TEST(test_a_gap_never_becomes_the_minimum);
  RUN_TEST(test_range_of_one_sample_is_a_point);
  RUN_TEST(test_span_needs_two_samples);
  RUN_TEST(test_span_saturates_at_the_window);
  return UNITY_END();
}
