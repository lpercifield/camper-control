#include "core/units.h"

namespace cc {
namespace {

// Seeded from config.h, overwritten by Settings::begin() with whatever is in
// NVS. A plain global rather than a member of Settings because settings.cpp
// needs Preferences, which is the board - and this has to be readable from
// src/core/, which runs on a laptop. See docs/ARCHITECTURE.md.
TempUnit g_unit = CFG_TEMP_UNIT_FAHRENHEIT ? TempUnit::Fahrenheit
                                           : TempUnit::Celsius;

}  // namespace

TempUnit tempUnit() { return g_unit; }

void setTempUnit(TempUnit u) { g_unit = u; }

float tempForDisplay(float celsius) {
  return g_unit == TempUnit::Fahrenheit ? celsius * 1.8f + 32.0f : celsius;
}

const char* tempUnitSuffix() {
  // UTF-8. The built-in Montserrat fonts are generated with -r 0x20-0x7F,0xB0,
  // so U+00B0 is one of the three glyphs outside ASCII they carry.
  return g_unit == TempUnit::Fahrenheit ? "\xC2\xB0" "F" : "\xC2\xB0" "C";
}

const char* formatTemp(float celsius, char* buf, size_t len, uint8_t decimals) {
  if (!buf || len == 0) return "";
  snprintf(buf, len, "%.*f %s", decimals, tempForDisplay(celsius),
           tempUnitSuffix());
  return buf;
}

}  // namespace cc
