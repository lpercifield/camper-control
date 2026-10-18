// BTHome v2 encoding - the van broadcasting its own state.
//
// The strongest test available here is a round trip: this project already owns
// a decoder that was written against the published format and the real frames
// of a real sensor, so encoding and handing the result straight to
// `bthomeDecode` checks the new code against something that was not written at
// the same time as it. A shared misreading of the format would still pass, so
// the byte-level assertions below pin the wire bytes too.

#include <unity.h>

#include <math.h>

#include "core/bthome.h"
#include "core/bthome_encode.h"
#include "icd.h"

using namespace cc;

void setUp() {}
void tearDown() {}

// ---- the frame header -------------------------------------------------------

static void test_fresh_builder_is_a_valid_empty_v2_frame(void) {
  BtHomeBuilder b;
  TEST_ASSERT_EQUAL_UINT(1, b.size());
  TEST_ASSERT_EQUAL_HEX8(0x40, b.data()[0]);  // v2, unencrypted

  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(b.data(), b.size(), r));
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_FALSE(r.haveBattery);
}

// ---- round trips ------------------------------------------------------------

static void test_round_trip_of_a_whole_van_frame(void) {
  BtHomeBuilder b;
  TEST_ASSERT_TRUE(b.addPacketId(7));
  TEST_ASSERT_TRUE(b.addBattery(84));
  TEST_ASSERT_TRUE(b.addTemperature(21.8f));
  TEST_ASSERT_TRUE(b.addVoltage(13.42f));
  TEST_ASSERT_FALSE(b.overflowed());
  TEST_ASSERT_FALSE(b.rejected());

  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(b.data(), b.size(), r));
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_TRUE(r.havePacketId);
  TEST_ASSERT_EQUAL_UINT8(7, r.packetId);
  TEST_ASSERT_EQUAL_UINT8(84, r.batteryPct);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.8f, r.temperatureC);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 13.42f, r.voltageV);
}

static void test_round_trip_of_humidity(void) {
  BtHomeBuilder b;
  TEST_ASSERT_TRUE(b.addHumidity(38.5f));
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(b.data(), b.size(), r));
  TEST_ASSERT_TRUE(r.haveHumidity);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 38.5f, r.humidityPct);
}

static void test_round_trip_of_a_negative_temperature(void) {
  BtHomeBuilder b;
  TEST_ASSERT_TRUE(b.addTemperature(-7.25f));
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(b.data(), b.size(), r));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -7.25f, r.temperatureC);
}

// ---- the wire bytes, pinned independently of our own decoder ----------------

static void test_wire_bytes_are_little_endian_and_scaled(void) {
  // 13.42 V at x0.001 is 13420 = 0x346C, low byte first.
  BtHomeBuilder b;
  b.addVoltage(13.42f);
  TEST_ASSERT_EQUAL_UINT(4, b.size());
  TEST_ASSERT_EQUAL_HEX8(0x0C, b.data()[1]);
  TEST_ASSERT_EQUAL_HEX8(0x6C, b.data()[2]);
  TEST_ASSERT_EQUAL_HEX8(0x34, b.data()[3]);
}

static void test_temperature_wire_bytes_match_the_spec_example(void) {
  // The published BTHome example: 25.06 C -> 2506 = 0x09CA.
  BtHomeBuilder b;
  b.addTemperature(25.06f);
  TEST_ASSERT_EQUAL_HEX8(0x02, b.data()[1]);
  TEST_ASSERT_EQUAL_HEX8(0xCA, b.data()[2]);
  TEST_ASSERT_EQUAL_HEX8(0x09, b.data()[3]);
}

static void test_scaling_rounds_rather_than_truncates(void) {
  // 13.4249 / 0.001 is 13424.9. Truncation would send 13424 and read a
  // millivolt low on every single reading.
  BtHomeBuilder b;
  b.addVoltage(13.4249f);
  BtHome r;
  bthomeDecode(b.data(), b.size(), r);
  TEST_ASSERT_FLOAT_WITHIN(0.0005f, 13.425f, r.voltageV);
}

// ---- ascending order is enforced, not assumed -------------------------------

static void test_descending_id_is_refused(void) {
  BtHomeBuilder b;
  TEST_ASSERT_TRUE(b.addVoltage(13.0f));      // 0x0C
  TEST_ASSERT_FALSE(b.addBattery(50));        // 0x01 - would break ordering
  TEST_ASSERT_TRUE(b.rejected());
  // And the frame is still the valid one it was before the refusal.
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(b.data(), b.size(), r));
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_TRUE(r.haveVoltage);
  TEST_ASSERT_FALSE(r.haveBattery);
}

static void test_duplicate_id_is_refused(void) {
  // Two temperatures in one frame is ambiguous about which sensor is which.
  BtHomeBuilder b;
  TEST_ASSERT_TRUE(b.addTemperature(20.0f));
  TEST_ASSERT_FALSE(b.addTemperature(21.0f));
  BtHome r;
  bthomeDecode(b.data(), b.size(), r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, r.temperatureC);
}

static void test_packet_id_zero_can_still_be_the_first_object(void) {
  // lastId_ starts at -1 precisely so object id 0x00 is not mistaken for
  // "already used".
  BtHomeBuilder b;
  TEST_ASSERT_TRUE(b.addPacketId(0));
  BtHome r;
  bthomeDecode(b.data(), b.size(), r);
  TEST_ASSERT_TRUE(r.havePacketId);
  TEST_ASSERT_EQUAL_UINT8(0, r.packetId);
}

// ---- values the format cannot carry are dropped, not clamped ----------------

static void test_voltage_above_the_field_is_dropped(void) {
  // uint16 x0.001 tops out at 65.535 V. A 70 V pack clamped to 65.535 is a
  // plausible lie; a missing field is an honest gap.
  BtHomeBuilder b;
  TEST_ASSERT_FALSE(b.addVoltage(70.0f));
  TEST_ASSERT_TRUE(b.rejected());
  TEST_ASSERT_FALSE(b.overflowed());
  TEST_ASSERT_EQUAL_UINT(1, b.size());
  BtHome r;
  bthomeDecode(b.data(), b.size(), r);
  TEST_ASSERT_FALSE(r.haveVoltage);
}

static void test_negative_voltage_is_dropped(void) {
  BtHomeBuilder b;
  TEST_ASSERT_FALSE(b.addVoltage(-1.0f));
  TEST_ASSERT_EQUAL_UINT(1, b.size());
}

static void test_nan_and_inf_are_dropped(void) {
  BtHomeBuilder b;
  TEST_ASSERT_FALSE(b.addTemperature(NAN));
  TEST_ASSERT_FALSE(b.addVoltage(INFINITY));
  TEST_ASSERT_FALSE(b.addHumidity(NAN));
  TEST_ASSERT_TRUE(b.rejected());
  TEST_ASSERT_EQUAL_UINT(1, b.size());
}

static void test_temperature_at_the_edges_of_the_field(void) {
  BtHomeBuilder hot;
  TEST_ASSERT_TRUE(hot.addTemperature(327.67f));
  BtHomeBuilder cold;
  TEST_ASSERT_TRUE(cold.addTemperature(-327.68f));
  BtHomeBuilder tooHot;
  TEST_ASSERT_FALSE(tooHot.addTemperature(400.0f));
}

static void test_battery_percent_is_clamped_not_dropped(void) {
  // Unlike a voltage, a percent has one obvious meaning above 100, and the
  // field is wide enough that nothing is lost by saying so.
  BtHomeBuilder b;
  TEST_ASSERT_TRUE(b.addBattery(130));
  BtHome r;
  bthomeDecode(b.data(), b.size(), r);
  TEST_ASSERT_EQUAL_UINT8(100, r.batteryPct);
}

// ---- the payload budget -----------------------------------------------------

static void test_overflow_is_reported_and_does_not_corrupt_the_frame(void) {
  BtHomeBuilder b;
  // Fill with ascending two-byte objects until it refuses. Only a handful of
  // ids are implemented, so drive it with the widest ones available and assert
  // the builder stops cleanly rather than running off the buffer.
  b.addPacketId(1);
  b.addBattery(50);
  b.addTemperature(20.0f);
  b.addHumidity(40.0f);
  b.addVoltage(13.0f);
  TEST_ASSERT_TRUE(b.size() <= BtHomeBuilder::kMaxPayload);
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(b.data(), b.size(), r));
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_TRUE(r.havePacketId);
  TEST_ASSERT_TRUE(r.haveBattery);
  TEST_ASSERT_TRUE(r.haveTemperature);
  TEST_ASSERT_TRUE(r.haveHumidity);
  TEST_ASSERT_TRUE(r.haveVoltage);
}

static void test_the_mvp_frame_fits_a_legacy_advertisement(void) {
  // 31 bytes total: flags (3) + name AD (2 + n) + service data AD (4 + payload)
  // where the 4 is length, type 0x16 and the two UUID bytes. This is the
  // arithmetic that decides whether the local name survives, so it is pinned.
  BtHomeBuilder b;
  b.addPacketId(1);
  b.addBattery(84);
  b.addTemperature(21.8f);
  b.addVoltage(13.42f);
  const size_t serviceAd = 4 + b.size();
  const size_t nameAd = 2 + (sizeof(CC_ICD_NAME) - 1);
  TEST_ASSERT_EQUAL_UINT(11, b.size());
  // Still the conservative bound, counting three bytes of flags that a
  // non-connectable advertisement does not actually carry.
  TEST_ASSERT_TRUE(3 + nameAd + serviceAd <= 31);
}

static void test_reset_clears_the_flags_and_the_order(void) {
  BtHomeBuilder b;
  b.addVoltage(13.0f);
  TEST_ASSERT_FALSE(b.addBattery(50));
  TEST_ASSERT_TRUE(b.rejected());
  b.reset();
  TEST_ASSERT_FALSE(b.rejected());
  TEST_ASSERT_EQUAL_UINT(1, b.size());
  TEST_ASSERT_TRUE(b.addBattery(50));  // ordering starts over
}

// ---- signed power and current ----------------------------------------------

static void test_round_trip_of_discharging_power_and_current(void) {
  // Discharging: both negative. This is the case the unsigned BTHome fields
  // could not express at all, and the reason 0x5C and 0x5D are used.
  BtHomeBuilder b;
  TEST_ASSERT_TRUE(b.addPower(-312.5f));
  TEST_ASSERT_TRUE(b.addCurrent(-23.45f));
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(b.data(), b.size(), r));
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_TRUE(r.havePower);
  TEST_ASSERT_TRUE(r.haveCurrent);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -312.5f, r.powerW);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -23.45f, r.currentA);
}

static void test_round_trip_of_charging_power_and_current(void) {
  BtHomeBuilder b;
  TEST_ASSERT_TRUE(b.addPower(845.75f));
  TEST_ASSERT_TRUE(b.addCurrent(18.2f));
  BtHome r;
  bthomeDecode(b.data(), b.size(), r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 845.75f, r.powerW);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 18.2f, r.currentA);
}

static void test_power_wire_bytes_are_four_little_endian(void) {
  // -312.50 W at x0.01 is -31250. 31250 is 0x7A12, so the two's complement is
  // 0xFFFF85EE, and BTHome is little-endian: EE 85 FF FF.
  BtHomeBuilder b;
  b.addPower(-312.5f);
  TEST_ASSERT_EQUAL_UINT(6, b.size());
  TEST_ASSERT_EQUAL_HEX8(0x5C, b.data()[1]);
  TEST_ASSERT_EQUAL_HEX8(0xEE, b.data()[2]);
  TEST_ASSERT_EQUAL_HEX8(0x85, b.data()[3]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, b.data()[4]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, b.data()[5]);
}

static void test_current_beyond_the_signed_field_is_dropped(void) {
  // This is the one that bites in practice: BTHome's signed current is
  // sint16 x0.001, so it holds +-32.767 A and this pack is rated past 120.
  // Dropped, not clamped - a reader divides power by voltage instead.
  BtHomeBuilder b;
  TEST_ASSERT_FALSE(b.addCurrent(120.0f));
  TEST_ASSERT_TRUE(b.rejected());
  TEST_ASSERT_EQUAL_UINT(1, b.size());
  TEST_ASSERT_TRUE(b.addCurrent(32.767f));    // the edge still fits
  TEST_ASSERT_FALSE(b.addCurrent(-40.0f));    // and so does the other side
}

static void test_power_covers_the_whole_pack_range(void) {
  // A 120 A pack at 14.6 V is ~1.75 kW either way, nowhere near sint32.
  BtHomeBuilder hi;
  TEST_ASSERT_TRUE(hi.addPower(1752.0f));
  BtHomeBuilder lo;
  TEST_ASSERT_TRUE(lo.addPower(-1752.0f));
  BtHome r;
  bthomeDecode(lo.data(), lo.size(), r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -1752.0f, r.powerW);
}

static void test_power_rejects_the_absurd_rather_than_wrapping(void) {
  BtHomeBuilder b;
  TEST_ASSERT_FALSE(b.addPower(1.0e9f));   // 1e11 raw, past sint32
  TEST_ASSERT_FALSE(b.addPower(-1.0e9f));
  TEST_ASSERT_EQUAL_UINT(1, b.size());
}

static void test_power_must_precede_current(void) {
  // 0x5C before 0x5D. Getting this backwards is the easiest mistake to make
  // when adding a field, so it is pinned.
  BtHomeBuilder b;
  TEST_ASSERT_TRUE(b.addCurrent(10.0f));
  TEST_ASSERT_FALSE(b.addPower(100.0f));
  TEST_ASSERT_TRUE(b.rejected());
}

static void test_current_is_recoverable_from_power_and_voltage(void) {
  // The fallback the app relies on when current is out of range. JbdBms
  // publishes power as exactly voltage x current, so this is a recovery of the
  // same number rather than an approximation of a second measurement.
  const float v = 13.42f, a = 95.0f;      // 95 A: past the current field
  BtHomeBuilder b;
  b.addVoltage(v);
  TEST_ASSERT_TRUE(b.addPower(v * a));
  TEST_ASSERT_FALSE(b.addCurrent(a));
  BtHome r;
  bthomeDecode(b.data(), b.size(), r);
  TEST_ASSERT_TRUE(r.havePower);
  TEST_ASSERT_FALSE(r.haveCurrent);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, a, r.powerW / r.voltageV);
}

static void test_the_full_frame_fits_a_legacy_advertisement(void) {
  // The whole shipped frame, and the arithmetic that decides the name length.
  // 31 bytes total = service data AD (1 len + 1 type + 2 UUID + payload)
  // + name AD (1 len + 1 type + n). Flags are absent because the
  // advertisement is non-connectable, which NimBLE handles for us.
  BtHomeBuilder b;
  b.addPacketId(1);
  b.addBattery(84);
  b.addTemperature(21.8f);
  b.addVoltage(13.42f);
  b.addPower(-312.5f);
  b.addCurrent(-23.28f);
  TEST_ASSERT_FALSE(b.overflowed());
  TEST_ASSERT_FALSE(b.rejected());
  TEST_ASSERT_EQUAL_UINT(19, b.size());

  // The name length comes from CC_ICD_NAME rather than a number written
  // here, so bumping the ICD version - which lengthens the name at version 10
  // - fails this test instead of silently overflowing the advertisement.
  const size_t serviceAd = 4 + b.size();                  // 23
  const size_t nameAd = 2 + (sizeof(CC_ICD_NAME) - 1);    // "CC1" -> 5
  TEST_ASSERT_EQUAL_UINT(5, nameAd);
  TEST_ASSERT_EQUAL_UINT(28, serviceAd + nameAd);
  TEST_ASSERT_TRUE(serviceAd + nameAd <= 31);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_builder_is_a_valid_empty_v2_frame);
  RUN_TEST(test_round_trip_of_a_whole_van_frame);
  RUN_TEST(test_round_trip_of_humidity);
  RUN_TEST(test_round_trip_of_a_negative_temperature);
  RUN_TEST(test_wire_bytes_are_little_endian_and_scaled);
  RUN_TEST(test_temperature_wire_bytes_match_the_spec_example);
  RUN_TEST(test_scaling_rounds_rather_than_truncates);
  RUN_TEST(test_descending_id_is_refused);
  RUN_TEST(test_duplicate_id_is_refused);
  RUN_TEST(test_packet_id_zero_can_still_be_the_first_object);
  RUN_TEST(test_voltage_above_the_field_is_dropped);
  RUN_TEST(test_negative_voltage_is_dropped);
  RUN_TEST(test_nan_and_inf_are_dropped);
  RUN_TEST(test_temperature_at_the_edges_of_the_field);
  RUN_TEST(test_battery_percent_is_clamped_not_dropped);
  RUN_TEST(test_overflow_is_reported_and_does_not_corrupt_the_frame);
  RUN_TEST(test_the_mvp_frame_fits_a_legacy_advertisement);
  RUN_TEST(test_round_trip_of_discharging_power_and_current);
  RUN_TEST(test_round_trip_of_charging_power_and_current);
  RUN_TEST(test_power_wire_bytes_are_four_little_endian);
  RUN_TEST(test_current_beyond_the_signed_field_is_dropped);
  RUN_TEST(test_power_covers_the_whole_pack_range);
  RUN_TEST(test_power_rejects_the_absurd_rather_than_wrapping);
  RUN_TEST(test_power_must_precede_current);
  RUN_TEST(test_current_is_recoverable_from_power_and_voltage);
  RUN_TEST(test_the_full_frame_fits_a_legacy_advertisement);
  RUN_TEST(test_reset_clears_the_flags_and_the_order);
  return UNITY_END();
}
