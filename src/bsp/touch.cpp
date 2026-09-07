#include "bsp/touch.h"
#include "board_indicator_d1.h"
#include "config.h"

bool Touch::begin(TwoWire& wire, PCA9535& expander) {
  wire_ = &wire;

  // Pulse the controller's reset line through the expander, and make sure the
  // interrupt pin stays an input - driving it fights the touch IC.
  expander.setDirection(EXP_TOUCH_INT, true);
  expander.setDirection(EXP_TOUCH_RST, false);
  expander.write(EXP_TOUCH_RST, false);
  delay(20);
  expander.write(EXP_TOUCH_RST, true);
  delay(120);  // FT6336 needs ~100 ms before it answers

  // The FT6336 datasheet asks for ~100 ms after reset, but a single probe at
  // exactly that moment is a coin flip on a cold boot. Retry for a second
  // before declaring it absent.
  for (int attempt = 0; attempt < 10; attempt++) {
    wire_->beginTransmission(TOUCH_I2C_ADDR);
    present_ = (wire_->endTransmission() == 0);
    if (present_) {
      if (attempt) log_w("touch answered on attempt %d", attempt + 1);
      break;
    }
    delay(100);
  }

  if (!present_) {
    // The interrupt line idles high and is pulled low on contact, so a line
    // stuck low points at wiring or a pin-map guess rather than a dead chip.
    bool intLevel = false;
    const bool intRead = expander.read(EXP_TOUCH_INT, intLevel);
    log_e("touch controller not found at 0x%02X (expander %s, INT %s)",
          TOUCH_I2C_ADDR, expander.ok() ? "ok" : "FAILED",
          intRead ? (intLevel ? "high" : "LOW") : "unreadable");
  }
  return present_;
}

bool Touch::read(int16_t& x, int16_t& y) {
  if (!present_ || !wire_) return false;

  // Registers 0x02..0x06: touch count, then the first point's coordinates.
  wire_->beginTransmission(TOUCH_I2C_ADDR);
  wire_->write(0x02);
  if (wire_->endTransmission(false) != 0) return false;
  if (wire_->requestFrom((int)TOUCH_I2C_ADDR, 5) != 5) return false;

  uint8_t points = wire_->read() & 0x0F;
  uint8_t xh = wire_->read();
  uint8_t xl = wire_->read();
  uint8_t yh = wire_->read();
  uint8_t yl = wire_->read();
  if (points == 0 || points > 2) return false;

  int16_t rx = (int16_t)(((xh & 0x0F) << 8) | xl);
  int16_t ry = (int16_t)(((yh & 0x0F) << 8) | yl);

#if CFG_TOUCH_MIRROR_X
  rx = (LCD_H_RES - 1) - rx;
#endif
#if CFG_TOUCH_MIRROR_Y
  ry = (LCD_V_RES - 1) - ry;
#endif

  if (rx < 0) rx = 0;
  if (ry < 0) ry = 0;
  if (rx >= LCD_H_RES) rx = LCD_H_RES - 1;
  if (ry >= LCD_V_RES) ry = LCD_V_RES - 1;

  x = rx;
  y = ry;
  return true;
}
