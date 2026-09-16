// SwitchBot W3400010 advertisement decoding.
//
// The test vector is the raw frame posted in SwitchBotAPI-BLE issue 26, which
// is the closest thing to a reference this format has. Everything here is
// pinned against reverse-engineered documentation rather than a specification,
// so these tests say "this is what we believe and why", and the real device is
// the final arbiter - see docs/ROADMAP.md.

#include <string.h>

#include <unity.h>

#include "core/switchbot.h"

using namespace cc;

// From issue 26: manufacturer data 6909 <6-byte address> 36 03 02 96 37 00.
// 0x02 -> 0.2, 0x96 -> 22 with the sign bit set, 0x37 -> 55%.
static const uint8_t kFrame[] = {0x69, 0x09,                          // company
                                 0xA4, 0xC1, 0x38, 0xE0, 0xAF, 0x18,  // address
                                 0x36, 0x03, 0x02, 0x96, 0x37, 0x00};

void setUp() {}
void tearDown() {}

static void test_decodes_the_reference_frame(void) {
  SwitchBotReading r;
  TEST_ASSERT_TRUE(switchbotDecodeManufacturer(kFrame, sizeof(kFrame), r));
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_TRUE(r.haveTemp);
  TEST_ASSERT_TRUE(r.haveHum);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.2f, r.temperatureC);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.0f, r.humidityPct);
}

static void test_rejects_another_vendor(void) {
  uint8_t other[sizeof(kFrame)];
  memcpy(other, kFrame, sizeof(kFrame));
  other[0] = 0x4C;  // Apple
  other[1] = 0x00;
  SwitchBotReading r;
  TEST_ASSERT_FALSE(switchbotDecodeManufacturer(other, sizeof(other), r));
  TEST_ASSERT_FALSE(r.valid);
}

// The company id is little-endian. Reading it the other way round would accept
// 0x6909, which belongs to somebody else entirely.
static void test_company_id_is_little_endian(void) {
  uint8_t swapped[sizeof(kFrame)];
  memcpy(swapped, kFrame, sizeof(kFrame));
  swapped[0] = 0x09;
  swapped[1] = 0x69;
  SwitchBotReading r;
  TEST_ASSERT_FALSE(switchbotDecodeManufacturer(swapped, sizeof(swapped), r));
}

// The sign bit is SET for positive, which is backwards from the usual
// convention and the single easiest thing in this format to invert. A van in
// February depends on it.
static void test_negative_temperature(void) {
  uint8_t cold[sizeof(kFrame)];
  memcpy(cold, kFrame, sizeof(kFrame));
  cold[10] = 0x05;  // 0.5
  cold[11] = 0x0C;  // 12, sign bit CLEAR -> negative
  SwitchBotReading r;
  TEST_ASSERT_TRUE(switchbotDecodeManufacturer(cold, sizeof(cold), r));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -12.5f, r.temperatureC);
}

static void test_zero_and_boundary_temperatures(void) {
  uint8_t f[sizeof(kFrame)];
  memcpy(f, kFrame, sizeof(kFrame));

  f[10] = 0x00;
  f[11] = 0x80;  // 0, positive
  SwitchBotReading r;
  switchbotDecodeManufacturer(f, sizeof(f), r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.temperatureC);

  f[10] = 0x09;
  f[11] = 0xFF;  // 127.9, positive - past the sensor's range, but must not wrap
  switchbotDecodeManufacturer(f, sizeof(f), r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 127.9f, r.temperatureC);
}

// The high bits of the humidity byte are not ours; masking them off is what
// keeps 0xB7 from reading as 183%.
static void test_humidity_masks_the_top_bit(void) {
  uint8_t f[sizeof(kFrame)];
  memcpy(f, kFrame, sizeof(kFrame));
  f[12] = 0xB7;  // 0x37 with the top bit set
  SwitchBotReading r;
  switchbotDecodeManufacturer(f, sizeof(f), r);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.0f, r.humidityPct);
}

static void test_rejects_short_frames(void) {
  SwitchBotReading r;
  TEST_ASSERT_FALSE(switchbotDecodeManufacturer(kFrame, 12, r));
  TEST_ASSERT_FALSE(switchbotDecodeManufacturer(nullptr, sizeof(kFrame), r));
  TEST_ASSERT_TRUE(switchbotDecodeManufacturer(kFrame, 13, r));  // exactly enough
}

// ---- service data: battery --------------------------------------------------

static void test_battery_from_service_data(void) {
  const uint8_t sd[] = {0x77, 0x00, 0x64};  // 'w', ?, 100%
  uint8_t batt = 0;
  TEST_ASSERT_TRUE(switchbotDecodeService(sd, sizeof(sd), batt));
  TEST_ASSERT_EQUAL_UINT8(100, batt);
}

static void test_battery_masks_the_top_bit(void) {
  const uint8_t sd[] = {0x77, 0x00, 0xE4};  // 0x64 with the top bit set
  uint8_t batt = 0;
  TEST_ASSERT_TRUE(switchbotDecodeService(sd, sizeof(sd), batt));
  TEST_ASSERT_EQUAL_UINT8(100, batt);
}

// Other SwitchBot products advertise under the same UUID with a different type
// byte. Reading a battery out of one of those would be a plausible wrong number.
static void test_rejects_a_different_switchbot_product(void) {
  const uint8_t bot[] = {0x48, 0x00, 0x64};  // 'H', the original Bot
  uint8_t batt = 0;
  TEST_ASSERT_FALSE(switchbotDecodeService(bot, sizeof(bot), batt));
}

static void test_rejects_impossible_battery(void) {
  const uint8_t sd[] = {0x77, 0x00, 0x65};  // 101%
  uint8_t batt = 0;
  TEST_ASSERT_FALSE(switchbotDecodeService(sd, sizeof(sd), batt));
}

static void test_rejects_short_service_data(void) {
  const uint8_t sd[] = {0x77, 0x00};
  uint8_t batt = 0;
  TEST_ASSERT_FALSE(switchbotDecodeService(sd, sizeof(sd), batt));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_decodes_the_reference_frame);
  RUN_TEST(test_rejects_another_vendor);
  RUN_TEST(test_company_id_is_little_endian);
  RUN_TEST(test_negative_temperature);
  RUN_TEST(test_zero_and_boundary_temperatures);
  RUN_TEST(test_humidity_masks_the_top_bit);
  RUN_TEST(test_rejects_short_frames);
  RUN_TEST(test_battery_from_service_data);
  RUN_TEST(test_battery_masks_the_top_bit);
  RUN_TEST(test_rejects_a_different_switchbot_product);
  RUN_TEST(test_rejects_impossible_battery);
  RUN_TEST(test_rejects_short_service_data);
  return UNITY_END();
}
