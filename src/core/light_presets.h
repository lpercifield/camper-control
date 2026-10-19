#pragma once
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// The colours a light can be set to from the panel.
//
// **A fixed palette rather than a colour picker**, and that is a constraint as
// much as a choice. LVGL 9.2 has no colour wheel - the widget was dropped
// after v8 - and the two ways to build one both run into walls this project
// has already hit: a canvas big enough to pick from costs more RAM than the
// board has spare, and an arc-based hue ring allocates its mask at draw time
// inside `lv_draw_sw_mask_radius_init`, which is the boot loop in
// `TROUBLESHOOTING.md` with no file of ours in the stack trace.
//
// It is also the better control for the job. The thing a person does at 3am is
// pick red so they can find the kettle without waking anybody, and that is one
// tap here against a hue they have to hunt for on a wheel.
//
// Pure data and a lookup, so it lives in core and is tested on the host.
// Brightness is **not** here: it is a separate axis, applied by scaling
// whichever of these is chosen. See `core/sp107e.h`.
// -----------------------------------------------------------------------------

namespace cc {

struct LightPreset {
  const char* name;  // what the swatch is labelled, short enough for one
  uint8_t r, g, b;
};

// Eight, which is two rows of four on a 480 px screen. Ordered so the two
// whites come first: they are what the light is for, and the colours are the
// exception.
constexpr size_t kLightPresetCount = 8;
extern const LightPreset kLightPresets[kLightPresetCount];

// The preset that is exactly this colour, or -1 for a colour that is not one
// of them - which a fixture set from the vendor app will be. The UI draws that
// as "no swatch selected" rather than guessing at the nearest, because a
// nearest-match would light up a swatch that would change the colour if it
// were tapped.
int lightPresetIndex(uint8_t r, uint8_t g, uint8_t b);

// The colour a fixture starts at before anybody has chosen: warm white, which
// is kLightPresets[0]. Named so the integration does not have to hardcode a
// triple that then disagrees with the palette.
constexpr size_t kLightPresetDefault = 0;

}  // namespace cc
