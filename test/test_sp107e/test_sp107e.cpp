// SP107E frame building and state decoding.
//
// Every expected byte here was measured against a real controller on
// 2026-10-18 (`decisions/0022`), so these tests pin the wire format against
// hardware rather than against the documentation - which was wrong about this
// device in two places.

#include <unity.h>

#include "core/sp107e.h"

using namespace cc;

void setUp() {}
void tearDown() {}

// A real frame as the device sends it: 15 bytes, byte 0 a checksum, byte 1 the
// frame index.
static const uint8_t kFrame1On[15] = {0x00, 0x01, 0x01, 0x05, 0x16, 0x01, 0x1E,
                                      0xB5, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF,
                                      0x01};
static const uint8_t kFrame1Off[15] = {0x7E, 0x01, 0x00, 0x00, 0x00, 0x20, 0x10,
                                       0xB5, 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF,
                                       0x01};
static const uint8_t kFrame2[15] = {0x00, 0x02, 0xFF, 0xC8, 0x8C, 0xFF, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF,
                                    0xFF};

// ---- the commands -----------------------------------------------------------

static void test_on_and_off_are_the_measured_bytes(void) {
  // decisions/0012 said off was 0xBB and it was right; 0xAA for on came off
  // the bench. The SP110E's 0xAB does nothing on this device.
  TEST_ASSERT_EQUAL_HEX8(0xAA, sp107eOn().b[3]);
  TEST_ASSERT_EQUAL_HEX8(0xBB, sp107eOff().b[3]);
  TEST_ASSERT_EQUAL_HEX8(0x01, sp107eQuery().b[3]);
}

static void test_commands_without_data_zero_their_data_bytes(void) {
  // The device ignores them; zeroing keeps a capture readable.
  Sp107eFrame f = sp107eOn();
  TEST_ASSERT_EQUAL_HEX8(0x00, f.b[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.b[1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.b[2]);
}

static void test_colour_is_rgb_then_the_command(void) {
  Sp107eFrame f = sp107eColour(0xFF, 0xC8, 0x8C);
  TEST_ASSERT_EQUAL_HEX8(0xFF, f.b[0]);
  TEST_ASSERT_EQUAL_HEX8(0xC8, f.b[1]);
  TEST_ASSERT_EQUAL_HEX8(0x8C, f.b[2]);
  TEST_ASSERT_EQUAL_HEX8(0x0C, f.b[3]);
}

// ---- brightness, which is scaling and not a command -------------------------

static void test_full_brightness_is_the_colour_unchanged(void) {
  Sp107eFrame f = sp107eColourScaled(0xFF, 0x80, 0x40, 0xFF);
  TEST_ASSERT_EQUAL_HEX8(0xFF, f.b[0]);
  TEST_ASSERT_EQUAL_HEX8(0x80, f.b[1]);
  TEST_ASSERT_EQUAL_HEX8(0x40, f.b[2]);
}

static void test_zero_brightness_is_black(void) {
  Sp107eFrame f = sp107eColourScaled(0xFF, 0xFF, 0xFF, 0x00);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.b[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.b[1]);
  TEST_ASSERT_EQUAL_HEX8(0x00, f.b[2]);
  TEST_ASSERT_EQUAL_HEX8(0x0C, f.b[3]);
}

static void test_half_brightness_keeps_the_hue(void) {
  // 255,200,140 at half should stay in proportion rather than collapsing
  // towards whichever channel divides evenly.
  Sp107eFrame f = sp107eColourScaled(0xFF, 0xC8, 0x8C, 0x80);
  TEST_ASSERT_EQUAL_HEX8(128, f.b[0]);  // 255*128/255 = 128
  TEST_ASSERT_EQUAL_HEX8(100, f.b[1]);  // 200*128/255 = 100.4 -> 100
  TEST_ASSERT_EQUAL_HEX8(70, f.b[2]);   // 140*128/255 = 70.3  -> 70
}

static void test_a_lit_channel_never_scales_to_black(void) {
  // The failure this guards: a light reported ON whose colour rounded to
  // 0,0,0 looks broken rather than dim.
  Sp107eFrame f = sp107eColourScaled(0x01, 0x00, 0x00, 0x01);
  TEST_ASSERT_EQUAL_HEX8(1, f.b[0]);
  TEST_ASSERT_EQUAL_HEX8(0, f.b[1]);  // a channel that was off stays off
  TEST_ASSERT_EQUAL_HEX8(0, f.b[2]);
}

static void test_black_stays_black_at_any_brightness(void) {
  Sp107eFrame f = sp107eColourScaled(0x00, 0x00, 0x00, 0xFF);
  TEST_ASSERT_EQUAL_HEX8(0, f.b[0]);
  TEST_ASSERT_EQUAL_HEX8(0, f.b[1]);
  TEST_ASSERT_EQUAL_HEX8(0, f.b[2]);
}

// ---- the readback -----------------------------------------------------------

static void test_frame_one_gives_on_off_segments_and_leds(void) {
  Sp107eState s;
  TEST_ASSERT_TRUE(sp107eFeed(kFrame1On, sizeof(kFrame1On), s));
  TEST_ASSERT_TRUE(s.haveOnOff);
  TEST_ASSERT_FALSE(s.haveColour);
  TEST_ASSERT_TRUE(s.on);
  TEST_ASSERT_EQUAL_UINT8(0x01, s.segments);
  TEST_ASSERT_EQUAL_UINT8(0x1E, s.leds);  // 30, the bench strip
}

static void test_frame_one_reports_off(void) {
  Sp107eState s;
  sp107eFeed(kFrame1Off, sizeof(kFrame1Off), s);
  TEST_ASSERT_FALSE(s.on);
  TEST_ASSERT_EQUAL_UINT8(0x20, s.segments);
  TEST_ASSERT_EQUAL_UINT8(0x10, s.leds);
}

static void test_frame_two_gives_the_colour(void) {
  Sp107eState s;
  TEST_ASSERT_TRUE(sp107eFeed(kFrame2, sizeof(kFrame2), s));
  TEST_ASSERT_TRUE(s.haveColour);
  TEST_ASSERT_EQUAL_HEX8(0xFF, s.r);
  TEST_ASSERT_EQUAL_HEX8(0xC8, s.g);
  TEST_ASSERT_EQUAL_HEX8(0x8C, s.b);
}

static void test_the_two_frames_merge_rather_than_replace(void) {
  // The frames arrive as separate notifications. Replacing would blank the
  // colour every time the on/off frame landed - the same mistake the sensor
  // path had to fix.
  Sp107eState s;
  sp107eFeed(kFrame2, sizeof(kFrame2), s);
  sp107eFeed(kFrame1On, sizeof(kFrame1On), s);
  TEST_ASSERT_TRUE(s.complete());
  TEST_ASSERT_TRUE(s.on);
  TEST_ASSERT_EQUAL_HEX8(0xFF, s.r);
  TEST_ASSERT_EQUAL_HEX8(0xC8, s.g);
  TEST_ASSERT_EQUAL_HEX8(0x8C, s.b);
}

static void test_complete_needs_both_frames(void) {
  Sp107eState s;
  TEST_ASSERT_FALSE(s.complete());
  sp107eFeed(kFrame1On, sizeof(kFrame1On), s);
  TEST_ASSERT_FALSE(s.complete());
  sp107eFeed(kFrame2, sizeof(kFrame2), s);
  TEST_ASSERT_TRUE(s.complete());
}

static void test_any_non_zero_on_byte_means_on(void) {
  uint8_t f[15] = {0};
  f[1] = 0x01;
  f[2] = 0x02;  // not the 1 the device sends, but certainly not off
  Sp107eState s;
  sp107eFeed(f, sizeof(f), s);
  TEST_ASSERT_TRUE(s.on);
}

static void test_unknown_frame_index_is_refused(void) {
  uint8_t f[15] = {0};
  f[1] = 0x07;
  Sp107eState s;
  TEST_ASSERT_FALSE(sp107eFeed(f, sizeof(f), s));
  TEST_ASSERT_FALSE(s.haveOnOff);
  TEST_ASSERT_FALSE(s.haveColour);
}

static void test_short_and_null_input_are_refused(void) {
  Sp107eState s;
  TEST_ASSERT_FALSE(sp107eFeed(nullptr, 15, s));
  const uint8_t truncated[4] = {0x00, 0x01, 0x01, 0x00};
  TEST_ASSERT_FALSE(sp107eFeed(truncated, sizeof(truncated), s));
  TEST_ASSERT_FALSE(s.haveOnOff);
}

static void test_a_refused_frame_leaves_earlier_state_alone(void) {
  Sp107eState s;
  sp107eFeed(kFrame2, sizeof(kFrame2), s);
  uint8_t bad[15] = {0};
  bad[1] = 0x09;
  sp107eFeed(bad, sizeof(bad), s);
  TEST_ASSERT_TRUE(s.haveColour);
  TEST_ASSERT_EQUAL_HEX8(0xFF, s.r);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_on_and_off_are_the_measured_bytes);
  RUN_TEST(test_commands_without_data_zero_their_data_bytes);
  RUN_TEST(test_colour_is_rgb_then_the_command);
  RUN_TEST(test_full_brightness_is_the_colour_unchanged);
  RUN_TEST(test_zero_brightness_is_black);
  RUN_TEST(test_half_brightness_keeps_the_hue);
  RUN_TEST(test_a_lit_channel_never_scales_to_black);
  RUN_TEST(test_black_stays_black_at_any_brightness);
  RUN_TEST(test_frame_one_gives_on_off_segments_and_leds);
  RUN_TEST(test_frame_one_reports_off);
  RUN_TEST(test_frame_two_gives_the_colour);
  RUN_TEST(test_the_two_frames_merge_rather_than_replace);
  RUN_TEST(test_complete_needs_both_frames);
  RUN_TEST(test_any_non_zero_on_byte_means_on);
  RUN_TEST(test_unknown_frame_index_is_refused);
  RUN_TEST(test_short_and_null_input_are_refused);
  RUN_TEST(test_a_refused_frame_leaves_earlier_state_alone);
  return UNITY_END();
}
