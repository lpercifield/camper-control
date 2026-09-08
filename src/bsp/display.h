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
void wakeBacklight();               // configured brightness, restart the idle timer
// True while the screen timeout has switched the backlight off. The next touch
// wakes it and is swallowed rather than delivered as a click.
bool screenOff();

PCA9535& expander();

}  // namespace bsp
