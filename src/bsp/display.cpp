#include "bsp/display.h"

#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <Wire.h>
#include <lvgl.h>

#include "board_indicator_d1.h"
#include "bsp/touch.h"
#include "config.h"
#include "core/settings.h"

namespace bsp {
namespace {

PCA9535 g_expander(Wire, PCA9535_I2C_ADDR);
Touch g_touch;

// ---- Panel command bus ------------------------------------------------------
// The ST7701's chip-select is not on a GPIO - it hangs off the IO expander. So
// we take Arduino_GFX's bit-banged SPI (9-bit mode, no DC pin) and override the
// transaction hooks to drive CS over I2C. Only the init sequence uses this bus;
// pixels go out over the RGB interface.
class IndicatorPanelBus : public Arduino_SWSPI {
 public:
  IndicatorPanelBus()
      : Arduino_SWSPI(GFX_NOT_DEFINED /* dc: 9-bit mode */,
                      GFX_NOT_DEFINED /* cs handled below */, LCD_SPI_SCK,
                      LCD_SPI_MOSI, GFX_NOT_DEFINED /* miso */) {}

  bool begin(int32_t speed = GFX_NOT_DEFINED, int8_t dataMode = GFX_NOT_DEFINED) override {
    g_expander.setDirection(EXP_LCD_CS, false);
    g_expander.write(EXP_LCD_CS, true);
    return Arduino_SWSPI::begin(speed, dataMode);
  }

  void beginWrite() override {
    g_expander.write(EXP_LCD_CS, false);
    Arduino_SWSPI::beginWrite();
  }

  void endWrite() override {
    Arduino_SWSPI::endWrite();
    g_expander.write(EXP_LCD_CS, true);
  }
};

Arduino_DataBus* g_bus = nullptr;
Arduino_ESP32RGBPanel* g_rgbpanel = nullptr;
Arduino_RGB_Display* g_gfx = nullptr;

// LVGL draw buffers. Partial rendering: two slices of the screen, kept in
// internal RAM because PSRAM reads would throttle the flush.
constexpr uint32_t kBufLines = 40;
constexpr uint32_t kBufPixels = LCD_H_RES * kBufLines;
uint16_t* g_buf1 = nullptr;
uint16_t* g_buf2 = nullptr;

lv_display_t* g_disp = nullptr;
lv_indev_t* g_indev = nullptr;

uint32_t g_lastActivityMs = 0;
uint8_t g_backlightLevel = CFG_BACKLIGHT_DEFAULT;
bool g_screenOff = false;

uint32_t lvglTick() { return millis(); }

// TEMPORARY bring-up counters. If flushes stay at 0 the fault is upstream in
// LVGL; if they climb while the panel stays dark it is the GFX/panel path.
volatile uint32_t g_flushCount = 0;
volatile uint32_t g_loopCount = 0;

void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  g_flushCount++;
  uint16_t* pixels = reinterpret_cast<uint16_t*>(px_map);
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;

#if CFG_DISPLAY_ROTATE_180
  // The init sequence Arduino_GFX uses for this panel comes up 180 degrees off.
  // Reversing the pixel run and remapping the origin is cheaper and more
  // predictable than patching the vendor init table.
  for (int32_t i = 0, j = (w * h) - 1; i < j; ++i, --j) {
    uint16_t t = pixels[i];
    pixels[i] = pixels[j];
    pixels[j] = t;
  }
  const int32_t x = (LCD_H_RES - 1) - area->x2;
  const int32_t y = (LCD_V_RES - 1) - area->y2;
#else
  const int32_t x = area->x1;
  const int32_t y = area->y1;
#endif

  g_gfx->draw16bitRGBBitmap(x, y, pixels, w, h);
  lv_display_flush_ready(disp);
}

void touchReadCb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
  // Waking a dark screen is not a click. Without this, the tap that turns the
  // backlight on also lands on whatever happened to be under the finger - and
  // since the screen was off, the crew could not have known what that was.
  static bool swallowUntilRelease = false;

  int16_t x = 0, y = 0;
  if (!g_touch.read(x, y)) {
    swallowUntilRelease = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  // Test before waking: wakeBacklight() clears the flag we are asking about.
  if (g_screenOff) swallowUntilRelease = true;
  wakeBacklight();

  if (swallowUntilRelease) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  data->point.x = x;
  data->point.y = y;
  data->state = LV_INDEV_STATE_PRESSED;
}

// Bring-up aid: what is actually answering on the bus. "Touch not found" is a
// much easier problem when you can see whether the expander is alone out there.
void scanI2C(const char* when) {
  char found[96] = {0};
  size_t off = 0;
  int n = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    n++;
    if (off + 6 < sizeof(found)) {
      off += snprintf(found + off, sizeof(found) - off, "0x%02X ", addr);
    }
  }
  log_i("i2c scan (%s): %d device(s) %s", when, n, n ? found : "- bus empty");
}

uint16_t* allocBuffer(uint32_t pixels) {
  uint16_t* p = static_cast<uint16_t*>(
      heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!p) {
    log_w("draw buffer falling back to PSRAM");
    p = static_cast<uint16_t*>(heap_caps_malloc(pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  }
  return p;
}

}  // namespace

PCA9535& expander() { return g_expander; }

void setBacklight(uint8_t level) {
  g_backlightLevel = level;
  ledcWrite(LCD_PIN_BL, level);
}

void wakeBacklight() {
  g_lastActivityMs = millis();
  if (g_screenOff) {
    g_screenOff = false;
    setBacklight(cc::Settings::instance().brightness());
  }
}

bool screenOff() { return g_screenOff; }

bool displayBegin() {
  Wire.begin(I2C_PIN_SDA, I2C_PIN_SCL, I2C_FREQ_HZ);
  scanI2C("boot");

  if (!g_expander.begin()) {
    log_e("IO expander init failed - the panel cannot be selected");
    return false;
  }
  // Take the panel out of reset before talking to it.
  g_expander.setDirection(EXP_LCD_RST, false);
  g_expander.write(EXP_LCD_RST, false);
  delay(20);
  g_expander.write(EXP_LCD_RST, true);
  delay(120);

  g_bus = new IndicatorPanelBus();
  g_rgbpanel = new Arduino_ESP32RGBPanel(
      LCD_PIN_DE, LCD_PIN_VSYNC, LCD_PIN_HSYNC, LCD_PIN_PCLK,
      LCD_PIN_R0, LCD_PIN_R1, LCD_PIN_R2, LCD_PIN_R3, LCD_PIN_R4,
      LCD_PIN_G0, LCD_PIN_G1, LCD_PIN_G2, LCD_PIN_G3, LCD_PIN_G4, LCD_PIN_G5,
      LCD_PIN_B0, LCD_PIN_B1, LCD_PIN_B2, LCD_PIN_B3, LCD_PIN_B4,
      LCD_HSYNC_POLARITY, LCD_HSYNC_FRONT_PORCH, LCD_HSYNC_PULSE_WIDTH,
      LCD_HSYNC_BACK_PORCH,
      LCD_VSYNC_POLARITY, LCD_VSYNC_FRONT_PORCH, LCD_VSYNC_PULSE_WIDTH,
      LCD_VSYNC_BACK_PORCH);

  g_gfx = new Arduino_RGB_Display(LCD_H_RES, LCD_V_RES, g_rgbpanel, 0 /* rotation */,
                                  true /* auto flush */, g_bus, GFX_NOT_DEFINED /* rst */,
                                  st7701_type1_init_operations,
                                  sizeof(st7701_type1_init_operations));

  if (!g_gfx->begin()) {
    log_e("panel init failed");
    return false;
  }
  g_gfx->fillScreen(BLACK);

  // Arduino core 3.x drives LEDC by pin rather than by channel.
  ledcAttach(LCD_PIN_BL, LCD_BL_FREQ_HZ, LCD_BL_RES_BITS);
  setBacklight(cc::Settings::instance().brightness());

  if (!g_touch.begin(Wire, g_expander)) {
    // Second scan: if the controller is not answering, this says whether it is
    // absent from the bus entirely or merely at a different address.
    scanI2C("after touch reset");
  }

  // ---- LVGL ----
  lv_init();
  lv_tick_set_cb(lvglTick);

  g_buf1 = allocBuffer(kBufPixels);
  g_buf2 = allocBuffer(kBufPixels);
  if (!g_buf1) {
    log_e("no memory for LVGL draw buffer");
    return false;
  }

  g_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
  lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(g_disp, flushCb);
  lv_display_set_buffers(g_disp, g_buf1, g_buf2, kBufPixels * sizeof(uint16_t),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  g_indev = lv_indev_create();
  lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(g_indev, touchReadCb);

  g_lastActivityMs = millis();
  log_i("display up: %dx%d, touch %s", LCD_H_RES, LCD_V_RES,
        g_touch.present() ? "ok" : "MISSING");
  return true;
}

void displayLoop() {
  g_loopCount++;
  lv_timer_handler();

  // TEMPORARY bring-up heartbeat.
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat > 2000) {
    lastBeat = millis();
    lv_obj_t* scr = lv_screen_active();
    log_i("hb: loops=%u flushes=%u scr=%p children=%u heap=%u",
          (unsigned)g_loopCount, (unsigned)g_flushCount, (void*)scr,
          scr ? (unsigned)lv_obj_get_child_count(scr) : 0u,
          (unsigned)ESP.getFreeHeap());
  }

  // A timeout of zero means the crew asked for always-on. Otherwise the
  // backlight goes fully off rather than down to a glow: this thing lives in a
  // van and a dim panel at 2am is still a light source.
  const uint32_t timeout = cc::Settings::instance().screenTimeoutMs();
  if (timeout != 0 && !g_screenOff && (millis() - g_lastActivityMs) > timeout) {
    g_screenOff = true;
    setBacklight(0);
  }
}

}  // namespace bsp
