#pragma once
#include <Arduino.h>

#include "config.h"

// -----------------------------------------------------------------------------
// The crew's choice of temperature unit, in one place.
//
// Every temperature in the system is *stored* in Celsius - that is what BTHome
// and the SwitchBot report, what the BMS reports, and what the alarm thresholds
// are written in. The unit is a property of the display, not of the reading, so
// the conversion happens at the last possible moment: in format().
//
// This file holds the live value. `Settings` persists it and restores it at
// boot; nothing else may keep a copy, or two temperatures on the same screen
// will eventually disagree.
// -----------------------------------------------------------------------------

namespace cc {

enum class TempUnit : uint8_t { Celsius = 0, Fahrenheit = 1 };

TempUnit tempUnit();

// Changes what every temperature on screen reads as. Does **not** persist -
// call Settings::setTempUnit() for that, which does both.
void setTempUnit(TempUnit u);

// A stored Celsius reading in whatever unit is currently selected.
float tempForDisplay(float celsius);

// "°C" or "°F". Never null.
const char* tempUnitSuffix();

// A whole formatted temperature, e.g. "21.4 °C" or "70.5 °F". Returns buf, and
// never null - see decisions/0005 for why this is snprintf and not LVGL's
// formatter.
const char* formatTemp(float celsius, char* buf, size_t len,
                       uint8_t decimals = 1);

}  // namespace cc
