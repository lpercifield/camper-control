#pragma once
#include <Arduino.h>
#include "bsp/pca9535.h"

namespace bsp {

// Brings up I2C, the IO expander, the ST7701 panel, the touch controller and
// LVGL. Returns false if the panel or LVGL could not be initialised.
bool displayBegin();

// Pump LVGL and handle backlight dimming. Call every pass of loop().
void displayLoop();

void setBacklight(uint8_t level);   // 0-255
void wakeBacklight();               // full brightness, restart the idle timer
bool backlightDimmed();

PCA9535& expander();

}  // namespace bsp
