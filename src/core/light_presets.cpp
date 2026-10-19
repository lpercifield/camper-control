#include "core/light_presets.h"

namespace cc {

// Measured against the bench strip rather than taken from a colour chart: an
// APA102 is RGB, so "white" is a mix and the mix that looks white on a strip
// is not 255/255/255. Warm white is the fixture's own boot colour, which is
// what the integration's default was before this table existed.
//
// Red is deliberately a pure channel. It is the night-vision colour and the
// one case where any green at all defeats the point.
const LightPreset kLightPresets[kLightPresetCount] = {
    {"Warm",  255, 200, 140},
    {"Cool",  255, 255, 255},
    {"Red",   255,   0,   0},
    {"Amber", 255, 120,   0},
    {"Green",   0, 255,  60},
    {"Blue",    0,  80, 255},
    {"Pink",  255,   0, 140},
    {"Teal",    0, 255, 200},
};

int lightPresetIndex(uint8_t r, uint8_t g, uint8_t b) {
  for (size_t i = 0; i < kLightPresetCount; i++) {
    const LightPreset& p = kLightPresets[i];
    if (p.r == r && p.g == g && p.b == b) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace cc
