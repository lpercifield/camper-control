// The light colour palette, and the lookup that decides which swatch is lit.
//
// Pure data and one loop, so both run on a laptop. The reason this is tested
// at all is the lookup's contract: it is **exact match or nothing**, and a
// nearest-match would light a swatch that would change the colour if it were
// tapped.

#include <string.h>
#include <unity.h>

#include "core/light_presets.h"

using namespace cc;

void setUp() {}
void tearDown() {}

// ---- the table --------------------------------------------------------------

static void test_every_preset_has_a_name(void) {
  for (size_t i = 0; i < kLightPresetCount; i++) {
    TEST_ASSERT_NOT_NULL(kLightPresets[i].name);
    TEST_ASSERT_TRUE(kLightPresets[i].name[0] != '\0');
    // Short enough to fit a swatch. Five characters is what the 104 px button
    // holds at montserrat_16.
    TEST_ASSERT_TRUE(strlen(kLightPresets[i].name) <= 5);
  }
}

// A preset that is black would be indistinguishable from off, and the switch
// is what turns a light off.
static void test_no_preset_is_black(void) {
  for (size_t i = 0; i < kLightPresetCount; i++) {
    const LightPreset& p = kLightPresets[i];
    TEST_ASSERT_TRUE(p.r != 0 || p.g != 0 || p.b != 0);
  }
}

// Two swatches the same colour would both light up for one reading, and one of
// them would be dead weight on a pool that has already boot-looped this board.
static void test_presets_are_all_different(void) {
  for (size_t i = 0; i < kLightPresetCount; i++) {
    for (size_t j = i + 1; j < kLightPresetCount; j++) {
      const bool same = kLightPresets[i].r == kLightPresets[j].r &&
                        kLightPresets[i].g == kLightPresets[j].g &&
                        kLightPresets[i].b == kLightPresets[j].b;
      TEST_ASSERT_FALSE(same);
    }
  }
}

// The night-vision colour has to be a pure channel. Any green at all defeats
// the point of it, so this is pinned rather than left to taste.
static void test_red_is_a_pure_channel(void) {
  const int i = lightPresetIndex(255, 0, 0);
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_STRING("Red", kLightPresets[i].name);
  TEST_ASSERT_EQUAL_UINT8(0, kLightPresets[i].g);
  TEST_ASSERT_EQUAL_UINT8(0, kLightPresets[i].b);
}

static void test_the_default_is_warm_white(void) {
  TEST_ASSERT_TRUE(kLightPresetDefault < kLightPresetCount);
  const LightPreset& p = kLightPresets[kLightPresetDefault];
  TEST_ASSERT_EQUAL_STRING("Warm", p.name);
  // Warm means the red channel leads and blue trails. A "warm" white that was
  // actually blue would be a silent product bug.
  TEST_ASSERT_TRUE(p.r > p.g);
  TEST_ASSERT_TRUE(p.g > p.b);
}

// ---- the lookup -------------------------------------------------------------

static void test_lookup_finds_every_preset(void) {
  for (size_t i = 0; i < kLightPresetCount; i++) {
    const LightPreset& p = kLightPresets[i];
    TEST_ASSERT_EQUAL_INT((int)i, lightPresetIndex(p.r, p.g, p.b));
  }
}

static void test_lookup_is_exact_not_nearest(void) {
  const LightPreset& warm = kLightPresets[kLightPresetDefault];
  // One off on one channel is not that preset. A nearest-match would return 0
  // here and the UI would claim warm white is lit.
  TEST_ASSERT_EQUAL_INT(-1,
                        lightPresetIndex(warm.r, warm.g, (uint8_t)(warm.b + 1)));
}

static void test_an_unknown_colour_is_no_preset(void) {
  TEST_ASSERT_EQUAL_INT(-1, lightPresetIndex(0, 0, 0));
  TEST_ASSERT_EQUAL_INT(-1, lightPresetIndex(17, 33, 71));
}

// A dimmed preset is not that preset: the device is told a scaled colour, so
// this is what stops a half-brightness warm white reading as a different
// swatch being selected. sp107eColourMatches is the right question there, and
// test_sp107e.cpp covers it.
static void test_a_scaled_preset_is_not_a_preset(void) {
  const LightPreset& warm = kLightPresets[kLightPresetDefault];
  TEST_ASSERT_EQUAL_INT(-1, lightPresetIndex(warm.r / 2, warm.g / 2, warm.b / 2));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_preset_has_a_name);
  RUN_TEST(test_no_preset_is_black);
  RUN_TEST(test_presets_are_all_different);
  RUN_TEST(test_red_is_a_pure_channel);
  RUN_TEST(test_the_default_is_warm_white);
  RUN_TEST(test_lookup_finds_every_preset);
  RUN_TEST(test_lookup_is_exact_not_nearest);
  RUN_TEST(test_an_unknown_colour_is_no_preset);
  RUN_TEST(test_a_scaled_preset_is_not_a_preset);
  return UNITY_END();
}
