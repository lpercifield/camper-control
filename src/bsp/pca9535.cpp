#include "bsp/pca9535.h"

// Register map
static constexpr uint8_t REG_INPUT  = 0x00;
static constexpr uint8_t REG_OUTPUT = 0x02;
static constexpr uint8_t REG_CONFIG = 0x06;

bool PCA9535::writeReg16(uint8_t reg, uint16_t value) {
  wire_.beginTransmission(addr_);
  wire_.write(reg);
  wire_.write((uint8_t)(value & 0xFF));
  wire_.write((uint8_t)(value >> 8));
  return wire_.endTransmission() == 0;
}

bool PCA9535::readReg16(uint8_t reg, uint16_t& value) {
  wire_.beginTransmission(addr_);
  wire_.write(reg);
  if (wire_.endTransmission(false) != 0) return false;
  if (wire_.requestFrom((int)addr_, 2) != 2) return false;
  uint8_t lo = wire_.read();
  uint8_t hi = wire_.read();
  value = (uint16_t)lo | ((uint16_t)hi << 8);
  return true;
}

bool PCA9535::begin() {
  wire_.beginTransmission(addr_);
  ok_ = (wire_.endTransmission() == 0);
  if (!ok_) {
    log_e("PCA9535 not responding at 0x%02X", addr_);
    return false;
  }
  // Start with every pin high (chip selects idle) and everything an input, then
  // let callers claim the outputs they need.
  readReg16(REG_CONFIG, dirCache_);
  readReg16(REG_OUTPUT, outCache_);
  return true;
}

bool PCA9535::setDirection(uint8_t pin, bool isInput) {
  if (pin > 15) return false;
  uint16_t next = isInput ? (dirCache_ | (1u << pin)) : (dirCache_ & ~(1u << pin));
  if (next == dirCache_) return true;
  if (!writeReg16(REG_CONFIG, next)) return false;
  dirCache_ = next;
  return true;
}

bool PCA9535::write(uint8_t pin, bool high) {
  if (pin > 15) return false;
  uint16_t next = high ? (outCache_ | (1u << pin)) : (outCache_ & ~(1u << pin));
  if (next == outCache_) return true;
  if (!writeReg16(REG_OUTPUT, next)) return false;
  outCache_ = next;
  return true;
}

bool PCA9535::read(uint8_t pin, bool& value) {
  if (pin > 15) return false;
  uint16_t in = 0;
  if (!readReg16(REG_INPUT, in)) return false;
  value = (in >> pin) & 1u;
  return true;
}
