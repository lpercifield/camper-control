#pragma once
#include <Arduino.h>
#include <Wire.h>

// Minimal PCA9535 driver. The Indicator hangs the LCD chip-select, LCD reset,
// touch reset and (on LoRa models) the radio control lines off this expander,
// so we only need output writes and a sane default direction mask.
class PCA9535 {
 public:
  PCA9535(TwoWire& wire, uint8_t addr) : wire_(wire), addr_(addr) {}

  bool begin();
  bool setDirection(uint8_t pin, bool isInput);
  bool write(uint8_t pin, bool high);
  bool read(uint8_t pin, bool& value);
  bool ok() const { return ok_; }

 private:
  bool writeReg16(uint8_t reg, uint16_t value);
  bool readReg16(uint8_t reg, uint16_t& value);

  TwoWire& wire_;
  uint8_t addr_;
  uint16_t dirCache_ = 0xFFFF;  // 1 = input, matches power-on default
  uint16_t outCache_ = 0xFFFF;
  bool ok_ = false;
};
