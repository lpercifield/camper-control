// The unit the crew reads, and the conversion behind it.
//
// A temperature is stored in Celsius everywhere in this project and converted
// once, on the way to the screen. These tests pin both halves of that: the
// arithmetic, and the rule that value() keeps reporting Celsius no matter what
// the display says - because that is what alarm thresholds compare against.

#include <unity.h>

#include "core/entity.h"
#include "core/units.h"

using namespace cc;

static char buf[32];

void setUp() {
  ccTestSetMillis(1);
  setTempUnit(TempUnit::Celsius);  // the unit is global; start each case level
}
void tearDown() { setTempUnit(TempUnit::Celsius); }

// ---- conversion -------------------------------------------------------------

static void test_celsius_is_the_identity(void) {
  TEST_ASSERT_EQUAL_FLOAT(21.5f, tempForDisplay(21.5f));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, tempForDisplay(0.0f));
  TEST_ASSERT_EQUAL_FLOAT(-17.0f, tempForDisplay(-17.0f));
}

static void test_fahrenheit_at_the_fixed_points(void) {
  setTempUnit(TempUnit::Fahrenheit);
  TEST_ASSERT_EQUAL_FLOAT(32.0f, tempForDisplay(0.0f));
  TEST_ASSERT_EQUAL_FLOAT(212.0f, tempForDisplay(100.0f));
  // The one both scales agree on, which is the sign test for a flipped formula.
  TEST_ASSERT_EQUAL_FLOAT(-40.0f, tempForDisplay(-40.0f));
}

static void test_fahrenheit_below_freezing(void) {
  setTempUnit(TempUnit::Fahrenheit);
  // A van in February. Getting the sign wrong here is the failure that matters.
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 14.0f, tempForDisplay(-10.0f));
}

static void test_suffix_follows_the_unit(void) {
  TEST_ASSERT_EQUAL_STRING("\xC2\xB0" "C", tempUnitSuffix());
  setTempUnit(TempUnit::Fahrenheit);
  TEST_ASSERT_EQUAL_STRING("\xC2\xB0" "F", tempUnitSuffix());
}

// ---- formatTemp -------------------------------------------------------------

static void test_format_in_celsius(void) {
  TEST_ASSERT_EQUAL_STRING("21.4 \xC2\xB0" "C", formatTemp(21.44f, buf, sizeof(buf)));
}

static void test_format_in_fahrenheit(void) {
  setTempUnit(TempUnit::Fahrenheit);
  TEST_ASSERT_EQUAL_STRING("70.5 \xC2\xB0" "F", formatTemp(21.4f, buf, sizeof(buf)));
}

static void test_format_honours_decimals(void) {
  TEST_ASSERT_EQUAL_STRING("21 \xC2\xB0" "C", formatTemp(21.4f, buf, sizeof(buf), 0));
}

static void test_format_survives_a_useless_buffer(void) {
  TEST_ASSERT_EQUAL_STRING("", formatTemp(21.4f, nullptr, 8));
  TEST_ASSERT_EQUAL_STRING("", formatTemp(21.4f, buf, 0));
}

// ---- TemperatureEntity ------------------------------------------------------

static void test_entity_unset_reads_as_dashes(void) {
  TemperatureEntity e("t", "Fridge", Domain::Climate);
  TEST_ASSERT_EQUAL_STRING("--", e.format(buf, sizeof(buf)));
}

static void test_entity_follows_the_unit(void) {
  TemperatureEntity e("t", "Fridge", Domain::Climate);
  e.set(20.0f);
  TEST_ASSERT_EQUAL_STRING("20.0 \xC2\xB0" "C", e.format(buf, sizeof(buf)));
  setTempUnit(TempUnit::Fahrenheit);
  TEST_ASSERT_EQUAL_STRING("68.0 \xC2\xB0" "F", e.format(buf, sizeof(buf)));
}

// The whole reason for a separate type. Alarm thresholds are written in Celsius
// and compared against value(); if switching the display changed the stored
// number, every threshold in the project would silently move.
static void test_entity_value_stays_celsius(void) {
  TemperatureEntity e("t", "Fridge", Domain::Climate);
  e.set(20.0f);
  setTempUnit(TempUnit::Fahrenheit);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, e.value());
}

static void test_entity_unit_reports_the_display_unit(void) {
  TemperatureEntity e("t", "Fridge", Domain::Climate);
  TEST_ASSERT_EQUAL_STRING("\xC2\xB0" "C", e.unit());
  setTempUnit(TempUnit::Fahrenheit);
  TEST_ASSERT_EQUAL_STRING("\xC2\xB0" "F", e.unit());
}

static void test_entity_is_numeric_kind(void) {
  TemperatureEntity e("t", "Fridge", Domain::Climate);
  TEST_ASSERT_EQUAL(EntityKind::Numeric, e.kind());
}

// format() is virtual, and the UI reaches these through a NumericEntity* it got
// out of the registry. If that dispatch ever broke, every temperature on the
// Power page would quietly go back to Celsius.
static void test_entity_converts_through_a_base_pointer(void) {
  TemperatureEntity e("t", "Battery", Domain::Power);
  e.set(20.0f);
  setTempUnit(TempUnit::Fahrenheit);
  NumericEntity* base = &e;
  TEST_ASSERT_EQUAL_STRING("68.0 \xC2\xB0" "F", base->format(buf, sizeof(buf)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_celsius_is_the_identity);
  RUN_TEST(test_fahrenheit_at_the_fixed_points);
  RUN_TEST(test_fahrenheit_below_freezing);
  RUN_TEST(test_suffix_follows_the_unit);
  RUN_TEST(test_format_in_celsius);
  RUN_TEST(test_format_in_fahrenheit);
  RUN_TEST(test_format_honours_decimals);
  RUN_TEST(test_format_survives_a_useless_buffer);
  RUN_TEST(test_entity_unset_reads_as_dashes);
  RUN_TEST(test_entity_follows_the_unit);
  RUN_TEST(test_entity_value_stays_celsius);
  RUN_TEST(test_entity_unit_reports_the_display_unit);
  RUN_TEST(test_entity_is_numeric_kind);
  RUN_TEST(test_entity_converts_through_a_base_pointer);
  return UNITY_END();
}
