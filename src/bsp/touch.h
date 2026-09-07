#pragma once
#include <Arduino.h>
#include "bsp/pca9535.h"

// FT6336 capacitive touch on the shared I2C bus. Reset is on the IO expander.
// Deliberately tiny: we only ever need the first contact point.
class Touch {
 public:
  bool begin(TwoWire& wire, PCA9535& expander);
  // Returns true while a finger is down; x/y are panel coordinates 0..479.
  bool read(int16_t& x, int16_t& y);
  bool present() const { return present_; }

 private:
  TwoWire* wire_ = nullptr;
  bool present_ = false;
};
