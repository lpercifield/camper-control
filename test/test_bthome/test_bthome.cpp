// BTHome v2 decoding.
//
// The wire format a temperature sensor speaks. Getting a byte offset wrong here
// does not crash anything - it puts a plausible, wrong number on the screen,
// which is the worst failure mode available. Hence the boundary cases.

#include <unity.h>

#include "core/bthome.h"

using namespace cc;

// Device info byte: BTHome v2 (version 2 in bits 5-7), unencrypted.
static constexpr uint8_t kV2 = 0x40;

void setUp() {}
void tearDown() {}

// ---- header -----------------------------------------------------------------

static void test_rejects_null_and_empty(void) {
  BtHome r;
  TEST_ASSERT_FALSE(bthomeDecode(nullptr, 4, r));
  const uint8_t empty[1] = {kV2};
  TEST_ASSERT_FALSE(bthomeDecode(empty, 0, r));
}

static void test_rejects_encrypted(void) {
  // Bit 0 set means encrypted. We hold no key, so this must not be guessed at.
  const uint8_t adv[] = {static_cast<uint8_t>(kV2 | 0x01), 0x02, 0xC4, 0x09};
  BtHome r;
  TEST_ASSERT_FALSE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_FALSE(r.valid);
}

static void test_rejects_other_versions(void) {
  const uint8_t v1[] = {0x20, 0x02, 0xC4, 0x09};  // version 1
  const uint8_t v3[] = {0x60, 0x02, 0xC4, 0x09};  // version 3
  BtHome r;
  TEST_ASSERT_FALSE(bthomeDecode(v1, sizeof(v1), r));
  TEST_ASSERT_FALSE(bthomeDecode(v3, sizeof(v3), r));
}

static void test_header_only_is_valid_but_carries_nothing(void) {
  const uint8_t adv[] = {kV2};
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_FALSE(r.haveTemperature);
  TEST_ASSERT_FALSE(r.haveHumidity);
}

// ---- the values we actually read --------------------------------------------

// The worked example from the BTHome documentation: 0x09C4 = 2500 -> 25.00 C.
static void test_temperature_from_the_spec_example(void) {
  const uint8_t adv[] = {kV2, 0x02, 0xC4, 0x09};
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_TRUE(r.haveTemperature);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.00f, r.temperatureC);
}

// And the humidity example: 0x13BF = 5055 -> 50.55 %.
static void test_humidity_from_the_spec_example(void) {
  const uint8_t adv[] = {kV2, 0x03, 0xBF, 0x13};
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_TRUE(r.haveHumidity);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.55f, r.humidityPct);
}

// A van in a field in February. sint16, so this is the case a uint16 decoder
// would render as +655 C.
static void test_negative_temperature(void) {
  // -12.34 C -> -1234 -> 0xFB2E little-endian.
  const uint8_t adv[] = {kV2, 0x02, 0x2E, 0xFB};
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -12.34f, r.temperatureC);
}

static void test_temperature_is_little_endian(void) {
  // Byte-swapped, this would read 0xC409 = -15351 -> -153.51 C. The sign of
  // the failure is the point: a swap is obvious, not subtle.
  const uint8_t adv[] = {kV2, 0x02, 0xC4, 0x09};
  BtHome r;
  bthomeDecode(adv, sizeof(adv), r);
  TEST_ASSERT_TRUE(r.temperatureC > 0.0f);
}

static void test_full_sensor_packet(void) {
  // What a pvvx or Shelly device actually sends: ascending object ids.
  const uint8_t adv[] = {kV2,
                         0x00, 0x2A,               // packet id 42
                         0x01, 0x54,               // battery 84 %
                         0x02, 0xC4, 0x09,         // 25.00 C
                         0x03, 0xBF, 0x13,         // 50.55 %
                         0x0C, 0x02, 0x0C};        // 3.074 V
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_EQUAL_UINT8(42, r.packetId);
  TEST_ASSERT_EQUAL_UINT8(84, r.batteryPct);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.00f, r.temperatureC);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.55f, r.humidityPct);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.074f, r.voltageV);
}

static void test_coarse_variants(void) {
  // 0x45 is temperature at x0.1, 0x2E humidity as a whole percent. Shelly BLU
  // uses these, so both spellings have to work.
  const uint8_t adv[] = {kV2, 0x2E, 0x37, 0x45, 0x0D, 0x01};
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_TRUE(r.complete);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 55.0f, r.humidityPct);   // 0x37 = 55
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 26.9f, r.temperatureC);  // 0x010D = 269
}

static void test_first_of_a_repeated_id_wins(void) {
  // BTHome allows two of the same sensor. One screen row, so take the first
  // and be consistent about it rather than silently showing the last.
  const uint8_t adv[] = {kV2, 0x02, 0xC4, 0x09, 0x02, 0x2E, 0xFB};
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.00f, r.temperatureC);
}

// ---- the failure modes that would put a wrong number on screen ---------------

static void test_unknown_id_stops_without_guessing(void) {
  // 0x04 (pressure, 3 bytes) is not in the size table. The parser must keep
  // what it already decoded and stop dead - not skip a guessed width and carry
  // on parsing from a misaligned offset.
  //
  // The battery object after the pressure is the discriminator, and it is here
  // deliberately: an earlier version of this test asserted only that the
  // temperature survived, which stays true even when the parser guesses wrong.
  // The test passed against a decoder that skipped three bytes and kept going.
  const uint8_t adv[] = {kV2,
                         0x02, 0xC4, 0x09,        // 25.00 C, before the unknown
                         0x04, 0x13, 0x8A, 0x01,  // pressure: cannot be sized
                         0x01, 0x54};             // battery: must NOT be read
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FALSE(r.complete);
  TEST_ASSERT_TRUE(r.haveTemperature);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.00f, r.temperatureC);
  TEST_ASSERT_FALSE(r.haveBattery);
}

static void test_unknown_id_first_yields_nothing(void) {
  // Unknown id before anything readable: the payload is unparseable from byte
  // one, and a decoder that guesses would report a battery of 2%.
  const uint8_t adv[] = {kV2, 0x04, 0x13, 0x8A, 0x01, 0x02, 0xC4, 0x09};
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FALSE(r.complete);
  TEST_ASSERT_FALSE(r.haveBattery);
  TEST_ASSERT_FALSE(r.haveTemperature);
  TEST_ASSERT_FALSE(r.haveHumidity);
}

static void test_truncated_object_is_not_read(void) {
  // Temperature announced, one byte delivered. Reading the missing byte would
  // be an overrun; the value must simply not appear.
  const uint8_t adv[] = {kV2, 0x02, 0xC4};
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_FALSE(r.complete);
  TEST_ASSERT_FALSE(r.haveTemperature);
}

static void test_trailing_id_with_no_payload(void) {
  const uint8_t adv[] = {kV2, 0x01, 0x54, 0x02};
  BtHome r;
  TEST_ASSERT_TRUE(bthomeDecode(adv, sizeof(adv), r));
  TEST_ASSERT_TRUE(r.haveBattery);
  TEST_ASSERT_FALSE(r.haveTemperature);
  TEST_ASSERT_FALSE(r.complete);
}

static void test_out_is_cleared_on_every_call(void) {
  // A struct reused across advertisements must not keep the last one's values,
  // or a sensor that drops a field shows a stale reading as if it were live.
  const uint8_t good[] = {kV2, 0x02, 0xC4, 0x09};
  BtHome r;
  bthomeDecode(good, sizeof(good), r);
  TEST_ASSERT_TRUE(r.haveTemperature);

  const uint8_t encrypted[] = {static_cast<uint8_t>(kV2 | 0x01), 0x02, 0xC4, 0x09};
  bthomeDecode(encrypted, sizeof(encrypted), r);
  TEST_ASSERT_FALSE(r.haveTemperature);
  TEST_ASSERT_FALSE(r.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, r.temperatureC);
}

static void test_extremes(void) {
  const uint8_t hot[] = {kV2, 0x02, 0xFF, 0x7F};   // 32767 -> 327.67 C
  const uint8_t cold[] = {kV2, 0x02, 0x00, 0x80};  // -32768 -> -327.68 C
  BtHome r;
  bthomeDecode(hot, sizeof(hot), r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 327.67f, r.temperatureC);
  bthomeDecode(cold, sizeof(cold), r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -327.68f, r.temperatureC);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_rejects_null_and_empty);
  RUN_TEST(test_rejects_encrypted);
  RUN_TEST(test_rejects_other_versions);
  RUN_TEST(test_header_only_is_valid_but_carries_nothing);
  RUN_TEST(test_temperature_from_the_spec_example);
  RUN_TEST(test_humidity_from_the_spec_example);
  RUN_TEST(test_negative_temperature);
  RUN_TEST(test_temperature_is_little_endian);
  RUN_TEST(test_full_sensor_packet);
  RUN_TEST(test_coarse_variants);
  RUN_TEST(test_first_of_a_repeated_id_wins);
  RUN_TEST(test_unknown_id_stops_without_guessing);
  RUN_TEST(test_unknown_id_first_yields_nothing);
  RUN_TEST(test_truncated_object_is_not_read);
  RUN_TEST(test_trailing_id_with_no_payload);
  RUN_TEST(test_out_is_cleared_on_every_call);
  RUN_TEST(test_extremes);
  return UNITY_END();
}
