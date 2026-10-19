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

// ---- diagnostics ------------------------------------------------------------
// Cumulative since boot: passes of displayLoop(), and LVGL flushes. The rate
// is the useful figure, not the total, so read the delta over a window - which
// is what the heartbeat log and the System screen both do. The healthy numbers
// for this board are in docs/TROUBLESHOOTING.md.
uint32_t loopCount();
uint32_t flushCount();

// What is answering on the I2C bus right now. Logs the result against `when`,
// and with `out` also writes it as "0x20 0x48", or "none". Returns the count.
//
// **112 bus transactions per call.** It belongs at boot and on an explicit
// ask, never in a refresh - `Hub` warns at 50 ms and this would earn it.
int scanI2C(const char* when, char* out = nullptr, size_t len = 0);

}  // namespace bsp
