#include "ui/ui.h"

#include <lvgl.h>
#include <bms2_options.h>

#include "bsp/display.h"
#include "config.h"
#include "core/alarms.h"
#include "core/integration.h"
#include "core/registry.h"
#include "core/clock.h"
#include "core/datalog.h"
#include "core/settings.h"
#include "core/units.h"
#include "integrations/bms_jbd.h"
#include "integrations/env_sensors.h"
#include "ui/theme.h"

namespace cc {
namespace ui {
namespace {

constexpr uint32_t kTickMs = 250;
constexpr int kMaxCellBars = BMS_MAX_CELLS;

// Row 3 of the power page: three equal data blocks - cells, capacity, energy.
// makeCard() pads by 8, so the drawable width inside one block is kRow3W - 16.
constexpr int kCardPad = 8;
constexpr int kRow3Y = 180;
constexpr int kRow3H = 88;
constexpr int kRow3W = 146;
constexpr int kRow3X[3] = {4, 158, 312};
constexpr int kCellInnerW = kRow3W - 2 * kCardPad;
constexpr int kCellBarY = 19;
constexpr int kCellBarH = 34;
constexpr int kCellLabelY = 55;
// The status strip takes what is left, down to the bottom of the content area.
constexpr int kStatusY = kRow3Y + kRow3H + 8;
constexpr int kStatusH = kContentH - 2 * kCardPad - kStatusY;

Domain g_current = Domain::Power;
uint32_t g_lastTickMs = 0;

lv_obj_t* g_header = nullptr;
lv_obj_t* g_headerTitle = nullptr;
lv_obj_t* g_headerStatus = nullptr;

lv_obj_t* g_pages[(int)Domain::COUNT] = {nullptr};
lv_obj_t* g_navButtons[4] = {nullptr};

// Settings is an overlay rather than a fifth domain: it is not an accessory,
// and the nav bar is about what the van can do.
lv_obj_t* g_settings = nullptr;
lv_obj_t* g_setBmsValue = nullptr;
lv_obj_t* g_setBrightValue = nullptr;
lv_obj_t* g_timeoutButtons[5] = {nullptr};
lv_obj_t* g_unitButtons[2] = {nullptr};

// 0 means always on. Kept in step with kTimeoutLabels below.
const uint32_t kTimeoutMs[5] = {30000UL, 60000UL, 120000UL, 300000UL, 0UL};
const char* kTimeoutLabels[5] = {"30s", "1m", "2m", "5m", "On"};

// Power page widgets
lv_obj_t* g_socArc = nullptr;
lv_obj_t* g_socLabel = nullptr;
lv_obj_t* g_tileValue[4] = {nullptr};
// Row 3: cells, capacity, energy - three standard data blocks side by side.
lv_obj_t* g_capValue = nullptr;
lv_obj_t* g_energyValue = nullptr;
lv_obj_t* g_cellBar[kMaxCellBars] = {nullptr};
lv_obj_t* g_cellLabel[kMaxCellBars] = {nullptr};
lv_obj_t* g_cellSummary = nullptr;
lv_obj_t* g_statusLabel = nullptr;
lv_obj_t* g_fetLabel = nullptr;

// Placeholder pages keep a label we can update as integrations appear.
lv_obj_t* g_emptyLabel[(int)Domain::COUNT] = {nullptr};
lv_obj_t* g_entityList[(int)Domain::COUNT] = {nullptr};

// Climate: one tile per sensor rather than one row per reading, so a sensor
// reads as a thing in a place rather than three unrelated numbers - and so the
// temperature is legible from across the van rather than from arm's length.
// Two columns of 220 leave 8 px of gutter and 8 more for the scrollbar.
constexpr int kTileW = 220;
constexpr int kTileH = 110;
constexpr int kTileGap = 8;
lv_obj_t* g_climateGrid = nullptr;
lv_obj_t* g_climateEmpty = nullptr;

// Sensor detail overlay: the readings large, and the trend behind them.
lv_obj_t* g_detail = nullptr;
lv_obj_t* g_detailTitle = nullptr;
lv_obj_t* g_detailTemp = nullptr;
lv_obj_t* g_detailHum = nullptr;
lv_obj_t* g_detailSub = nullptr;
lv_obj_t* g_detailChart = nullptr;
lv_chart_series_t* g_detailTempSeries = nullptr;
lv_chart_series_t* g_detailHumSeries = nullptr;
lv_obj_t* g_detailTempHi = nullptr;
lv_obj_t* g_detailTempLo = nullptr;
lv_obj_t* g_detailHumHi = nullptr;
lv_obj_t* g_detailHumLo = nullptr;
lv_obj_t* g_detailSpan = nullptr;
lv_obj_t* g_detailLegend = nullptr;
char g_detailMac[18] = {0};

// Both series are drawn against 0..1000 and scaled here rather than by LVGL,
// so temperature and humidity each fill the plot on their own scale and the
// real extremes go on the axis labels beside it. LVGL's own axis ticks are
// integers, which is the wrong tool for a range of 20.4 to 23.1.
constexpr int32_t kChartSpan = 1000;

// One text-entry overlay, above everything including Settings, shared by
// everything that needs the keyboard. It used to be the rename box alone; the
// clock needed the same thing and a second lv_keyboard costs 3.5 KB of a pool
// that has already boot-looped this board once (decisions/0013). The Wi-Fi work
// in ROADMAP.md is the third caller waiting for it.
lv_obj_t* g_entry = nullptr;
lv_obj_t* g_entryTitle = nullptr;
lv_obj_t* g_entrySubtitle = nullptr;
lv_obj_t* g_entryInput = nullptr;
// What to do with the text on save. Null means the overlay is closed.
void (*g_entryOnSave)(const char*) = nullptr;
char g_renameMac[18] = {0};

// Settings: the clock row.
lv_obj_t* g_setClockValue = nullptr;
lv_obj_t* g_setClockHint = nullptr;

// The clock setter: five columns of up/down steppers, above everything.
// A keyboard was the first attempt and it was the wrong tool - setting a clock
// is picking five numbers, not typing a sentence, and a 16-character date on a
// touch keyboard is a fiddle nobody wants at the side of a road in the dark.
lv_obj_t* g_clockSet = nullptr;
lv_obj_t* g_clockPreview = nullptr;
lv_obj_t* g_clockFieldValue[(int)DateField::COUNT] = {nullptr};
// One button matrix for the row of up arrows and one for the row of down
// arrows. Two LVGL objects rather than the twenty that ten lv_buttons and
// their labels cost - which mattered: the first version of this screen left
// the LVGL pool at 90% used, and 0013 is the record of what happens next.
lv_obj_t* g_clockUpRow = nullptr;
lv_obj_t* g_clockDownRow = nullptr;
DateTime g_clockEdit;

// Climate: why the log is or is not running.
lv_obj_t* g_climateStatus = nullptr;

// Detail: today's high and low, from the per-minute log rather than the graph.
lv_obj_t* g_detailTempToday = nullptr;
lv_obj_t* g_detailHumToday = nullptr;

const Domain kNavDomains[4] = {Domain::Power, Domain::Lighting, Domain::Water,
                               Domain::Climate};

// ---- small helpers ----------------------------------------------------------

lv_obj_t* makeCard(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, colCard(), 0);
  lv_obj_set_style_border_color(c, colCardEdge(), 0);
  lv_obj_set_style_border_width(c, 1, 0);
  lv_obj_set_style_radius(c, 10, 0);
  lv_obj_set_style_pad_all(c, 8, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, const lv_font_t* font,
                    lv_color_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, color, 0);
  return l;
}

// Defined with the clock setter further down, next to the overlay it opens;
// buildSettings() needs the name up here.
void setClockCb(lv_event_t* e);

void refreshSettings() {
  if (!g_settings) return;
  Settings& st = Settings::instance();

  lv_label_set_text(g_setBmsValue,
                    st.hasBmsMac() ? st.bmsMac() : "none saved");

  char buf[16];
  snprintf(buf, sizeof(buf), "%d%%", (int)((st.brightness() * 100 + 127) / 255));
  lv_label_set_text(g_setBrightValue, buf);

  for (int i = 0; i < 5; i++) {
    const bool active = kTimeoutMs[i] == st.screenTimeoutMs();
    lv_obj_set_style_bg_color(g_timeoutButtons[i], active ? colAccent() : colBg(), 0);
    lv_obj_t* lab = lv_obj_get_child(g_timeoutButtons[i], 0);
    lv_obj_set_style_text_color(lab, active ? lv_color_black() : colMuted(), 0);
  }

  for (int i = 0; i < 2; i++) {
    const bool active = (TempUnit)i == tempUnit();
    lv_obj_set_style_bg_color(g_unitButtons[i], active ? colAccent() : colBg(), 0);
    lv_obj_t* lab = lv_obj_get_child(g_unitButtons[i], 0);
    lv_obj_set_style_text_color(lab, active ? lv_color_black() : colMuted(), 0);
  }

  Clock& clk = Clock::instance();
  char stamp[24];
  lv_label_set_text(g_setClockValue,
                    clk.isSet() ? Clock::formatStamp(clk.now(), stamp, sizeof(stamp))
                                : "never set");
  // Three states, and the middle one is the one worth saying out loud: a time
  // restored from flash is stale by however long the power was off, and the
  // log is stamping rows with it meanwhile.
  if (!clk.isSet()) {
    lv_label_set_text(g_setClockHint, "the log needs this");
    lv_obj_set_style_text_color(g_setClockHint, colWarn(), 0);
    lv_obj_set_style_text_color(g_setClockValue, colMuted(), 0);
  } else if (clk.approximate()) {
    lv_label_set_text(g_setClockHint, "restored - may be wrong");
    lv_obj_set_style_text_color(g_setClockHint, colWarn(), 0);
    lv_obj_set_style_text_color(g_setClockValue, colText(), 0);
  } else {
    lv_label_set_text(g_setClockHint, "");
    lv_obj_set_style_text_color(g_setClockValue, colText(), 0);
  }
}

void showSettings(bool visible) {
  if (!g_settings) return;
  if (visible) {
    refreshSettings();
    lv_obj_remove_flag(g_settings, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_settings, LV_OBJ_FLAG_HIDDEN);
  }
  bsp::wakeBacklight();
}

void gearEventCb(lv_event_t* /*e*/) { showSettings(true); }
void closeSettingsCb(lv_event_t* /*e*/) { showSettings(false); }

void brightnessCb(lv_event_t* e) {
  lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
  const uint8_t level = (uint8_t)lv_slider_get_value(slider);
  const bool released = lv_event_get_code(e) == LV_EVENT_RELEASED;
  // Apply on every drag so it can be judged by eye; only write flash on
  // release, or a single sweep of the slider would be hundreds of NVS writes.
  Settings::instance().setBrightness(level, released);
  bsp::setBacklight(level);
  bsp::wakeBacklight();
  refreshSettings();
}

void timeoutCb(lv_event_t* e) {
  const intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  Settings::instance().setScreenTimeoutMs(kTimeoutMs[idx]);
  bsp::wakeBacklight();
  refreshSettings();
}

void tempUnitCb(lv_event_t* e) {
  const intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  // Settings both applies and persists; the live value it writes is what every
  // format() on every page reads, so nothing else has to be told.
  Settings::instance().setTempUnit((TempUnit)idx);
  bsp::wakeBacklight();
  refreshSettings();
}

void clearBmsCb(lv_event_t* /*e*/) {
  Settings::instance().clearBmsMac();
  bsp::wakeBacklight();
  refreshSettings();
}

void navEventCb(lv_event_t* e) {
  intptr_t idx = (intptr_t)lv_event_get_user_data(e);
  showDomain(kNavDomains[idx]);
  bsp::wakeBacklight();
}

void headerEventCb(lv_event_t* /*e*/) {
  // Tapping the banner silences the audible part of an alarm without clearing
  // the condition - the banner stays until the fault actually goes away.
  if (Alarms::instance().any()) Alarms::instance().silence();
  bsp::wakeBacklight();
}

// ---- construction -----------------------------------------------------------

void buildHeader(lv_obj_t* scr) {
  g_header = lv_obj_create(scr);
  lv_obj_set_pos(g_header, 0, 0);
  lv_obj_set_size(g_header, kScreenW, kHeaderH);
  lv_obj_set_style_bg_color(g_header, colCard(), 0);
  lv_obj_set_style_border_width(g_header, 0, 0);
  lv_obj_set_style_radius(g_header, 0, 0);
  lv_obj_set_style_pad_all(g_header, 0, 0);
  lv_obj_remove_flag(g_header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_header, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_header, headerEventCb, LV_EVENT_CLICKED, nullptr);

  g_headerTitle = makeLabel(g_header, "Power", &lv_font_montserrat_20, colText());
  lv_obj_align(g_headerTitle, LV_ALIGN_LEFT_MID, 14, 0);

  // The gear sits at the right edge; the link status shifts left to clear it.
  lv_obj_t* gear = lv_button_create(g_header);
  lv_obj_set_size(gear, 40, 34);
  lv_obj_align(gear, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_set_style_radius(gear, 8, 0);
  lv_obj_set_style_bg_color(gear, colBg(), 0);
  lv_obj_add_event_cb(gear, gearEventCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* gearLabel = makeLabel(gear, LV_SYMBOL_SETTINGS, &lv_font_montserrat_20,
                                 colMuted());
  lv_obj_center(gearLabel);

  g_headerStatus = makeLabel(g_header, "starting", &lv_font_montserrat_16, colMuted());
  lv_obj_align(g_headerStatus, LV_ALIGN_RIGHT_MID, -56, 0);
}

// ---- settings overlay -------------------------------------------------------
// Full screen, above the pages and the nav bar, hidden until the gear is
// tapped. Rows are the same card idiom as the rest of the UI.
void buildSettings(lv_obj_t* scr) {
  g_settings = lv_obj_create(scr);
  lv_obj_set_pos(g_settings, 0, 0);
  lv_obj_set_size(g_settings, kScreenW, kScreenH);
  lv_obj_set_style_bg_color(g_settings, colBg(), 0);
  lv_obj_set_style_border_width(g_settings, 0, 0);
  lv_obj_set_style_radius(g_settings, 0, 0);
  lv_obj_set_style_pad_all(g_settings, 0, 0);
  lv_obj_remove_flag(g_settings, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_settings, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* bar = lv_obj_create(g_settings);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, kScreenW, kHeaderH);
  lv_obj_set_style_bg_color(bar, colCard(), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = makeLabel(bar, "Settings", &lv_font_montserrat_20, colText());
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 14, 0);

  lv_obj_t* close = lv_button_create(bar);
  lv_obj_set_size(close, 40, 34);
  lv_obj_align(close, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_set_style_radius(close, 8, 0);
  lv_obj_set_style_bg_color(close, colBg(), 0);
  lv_obj_add_event_cb(close, closeSettingsCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* closeLabel = makeLabel(close, LV_SYMBOL_CLOSE, &lv_font_montserrat_20,
                                   colMuted());
  lv_obj_center(closeLabel);

  // Everything below the bar lives in one scrolling column. It used to be a
  // stack of absolute offsets, which was fine at four rows and stopped being
  // fine at five - and the Wi-Fi work in ROADMAP.md wants more still.
  lv_obj_t* body = lv_obj_create(g_settings);
  lv_obj_set_pos(body, 0, kHeaderH);
  lv_obj_set_size(body, kScreenW, kScreenH - kHeaderH);
  lv_obj_set_style_bg_color(body, colBg(), 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_radius(body, 0, 0);
  lv_obj_set_style_pad_all(body, 12, 0);
  lv_obj_set_style_pad_row(body, 12, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);

  const int w = kScreenW - 32;  // 12 of padding each side, 8 for the scrollbar

  // ---- brightness ----
  lv_obj_t* bright = makeCard(body, 0, 0, w, 96);
  lv_obj_t* brightName = makeLabel(bright, "Brightness", &lv_font_montserrat_16,
                                   colMuted());
  lv_obj_align(brightName, LV_ALIGN_TOP_LEFT, 0, 0);
  g_setBrightValue = makeLabel(bright, "--", &lv_font_montserrat_16, colText());
  lv_obj_align(g_setBrightValue, LV_ALIGN_TOP_RIGHT, 0, 0);

  lv_obj_t* slider = lv_slider_create(bright);
  lv_obj_set_size(slider, w - 32, 14);
  lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -4);
  // Never all the way off: a black screen with no touch feedback looks broken.
  lv_slider_set_range(slider, 20, 255);
  lv_slider_set_value(slider, Settings::instance().brightness(), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, colCardEdge(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, colAccent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, colAccent(), LV_PART_KNOB);
  lv_obj_add_event_cb(slider, brightnessCb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(slider, brightnessCb, LV_EVENT_RELEASED, nullptr);

  // ---- screen timeout ----
  lv_obj_t* timeout = makeCard(body, 0, 0, w, 96);
  lv_obj_t* timeoutName = makeLabel(timeout, "Screen timeout", &lv_font_montserrat_16,
                                    colMuted());
  lv_obj_align(timeoutName, LV_ALIGN_TOP_LEFT, 0, 0);

  const int bw = (w - 32 - 4 * 6) / 5;
  for (int i = 0; i < 5; i++) {
    lv_obj_t* b = lv_button_create(timeout);
    lv_obj_set_size(b, bw, 40);
    lv_obj_set_pos(b, i * (bw + 6), 30);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_add_event_cb(b, timeoutCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* lab = makeLabel(b, kTimeoutLabels[i], &lv_font_montserrat_16, colMuted());
    lv_obj_center(lab);
    g_timeoutButtons[i] = b;
  }

  // ---- temperature unit ----
  // One switch for the whole van. Readings stay in Celsius underneath; this
  // only decides what they are drawn as. See core/units.h.
  lv_obj_t* units = makeCard(body, 0, 0, w, 96);
  lv_obj_t* unitsName = makeLabel(units, "Temperature", &lv_font_montserrat_16,
                                  colMuted());
  lv_obj_align(unitsName, LV_ALIGN_TOP_LEFT, 0, 0);

  static const char* kUnitLabels[2] = {"\xC2\xB0" "C", "\xC2\xB0" "F"};
  const int uw = (w - 32 - 6) / 2;
  for (int i = 0; i < 2; i++) {
    lv_obj_t* b = lv_button_create(units);
    lv_obj_set_size(b, uw, 40);
    lv_obj_set_pos(b, i * (uw + 6), 30);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_add_event_cb(b, tempUnitCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* lab = makeLabel(b, kUnitLabels[i], &lv_font_montserrat_20, colMuted());
    lv_obj_center(lab);
    g_unitButtons[i] = b;
  }

  // ---- clock ----
  // No RTC, no NTP: this is the only way the device learns the time, and the
  // daily log file and the daily high and low both depend on it.
  lv_obj_t* clockCard = makeCard(body, 0, 0, w, 96);
  lv_obj_t* clockName = makeLabel(clockCard, "Clock", &lv_font_montserrat_16,
                                  colMuted());
  lv_obj_align(clockName, LV_ALIGN_TOP_LEFT, 0, 0);
  g_setClockHint = makeLabel(clockCard, "", &lv_font_montserrat_14, colMuted());
  lv_obj_align(g_setClockHint, LV_ALIGN_TOP_RIGHT, 0, 2);
  g_setClockValue = makeLabel(clockCard, "never set", &lv_font_montserrat_20,
                              colText());
  lv_obj_align(g_setClockValue, LV_ALIGN_LEFT_MID, 0, 6);

  lv_obj_t* setClock = lv_button_create(clockCard);
  lv_obj_set_size(setClock, 92, 40);
  lv_obj_align(setClock, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_radius(setClock, 8, 0);
  lv_obj_set_style_bg_color(setClock, colCardEdge(), 0);
  lv_obj_add_event_cb(setClock, setClockCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(makeLabel(setClock, "Set", &lv_font_montserrat_16, colText()));

  // ---- BMS ----
  lv_obj_t* bmsCard = makeCard(body, 0, 0, w, 96);
  lv_obj_t* bmsName = makeLabel(bmsCard, "Battery monitor", &lv_font_montserrat_16,
                                colMuted());
  lv_obj_align(bmsName, LV_ALIGN_TOP_LEFT, 0, 0);
  g_setBmsValue = makeLabel(bmsCard, "none saved", &lv_font_montserrat_20, colText());
  lv_obj_align(g_setBmsValue, LV_ALIGN_LEFT_MID, 0, 6);

  lv_obj_t* clear = lv_button_create(bmsCard);
  lv_obj_set_size(clear, 92, 40);
  lv_obj_align(clear, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_radius(clear, 8, 0);
  lv_obj_set_style_bg_color(clear, colCardEdge(), 0);
  lv_obj_add_event_cb(clear, clearBmsCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* clearLabel = makeLabel(clear, "Forget", &lv_font_montserrat_16, colText());
  lv_obj_center(clearLabel);

  // ---- wifi placeholder ----
  lv_obj_t* wifi = makeCard(body, 0, 0, w, 72);
  lv_obj_t* wifiName = makeLabel(wifi, "Wi-Fi backhaul", &lv_font_montserrat_16,
                                 colMuted());
  lv_obj_align(wifiName, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t* wifiState = makeLabel(wifi, "not configured yet",
                                  &lv_font_montserrat_16, colMuted());
  lv_obj_align(wifiState, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void buildNav(lv_obj_t* scr) {
  lv_obj_t* nav = lv_obj_create(scr);
  lv_obj_set_pos(nav, 0, kScreenH - kNavH);
  lv_obj_set_size(nav, kScreenW, kNavH);
  lv_obj_set_style_bg_color(nav, colCard(), 0);
  lv_obj_set_style_border_width(nav, 0, 0);
  lv_obj_set_style_radius(nav, 0, 0);
  lv_obj_set_style_pad_all(nav, 6, 0);
  lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

  const int w = (kScreenW - 12) / 4;
  for (int i = 0; i < 4; i++) {
    lv_obj_t* b = lv_button_create(nav);
    lv_obj_set_pos(b, i * w, 0);
    lv_obj_set_size(b, w - 6, kNavH - 12);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_bg_color(b, colBg(), 0);
    lv_obj_add_event_cb(b, navEventCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    lv_obj_t* l = makeLabel(b, domainName(kNavDomains[i]), &lv_font_montserrat_16,
                            colMuted());
    lv_obj_center(l);
    g_navButtons[i] = b;
  }
}

lv_obj_t* makePage(lv_obj_t* scr) {
  lv_obj_t* p = lv_obj_create(scr);
  lv_obj_set_pos(p, 0, kHeaderH);
  lv_obj_set_size(p, kScreenW, kContentH);
  lv_obj_set_style_bg_color(p, colBg(), 0);
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_style_radius(p, 0, 0);
  lv_obj_set_style_pad_all(p, 8, 0);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  return p;
}

void buildPowerPage(lv_obj_t* scr) {
  lv_obj_t* p = makePage(scr);
  g_pages[(int)Domain::Power] = p;

  // State of charge
  g_socArc = lv_arc_create(p);
  lv_obj_set_size(g_socArc, 168, 168);
  lv_obj_set_pos(g_socArc, 4, 4);
  lv_arc_set_rotation(g_socArc, 135);
  lv_arc_set_bg_angles(g_socArc, 0, 270);
  lv_arc_set_range(g_socArc, 0, 100);
  lv_arc_set_value(g_socArc, 0);
  lv_obj_remove_style(g_socArc, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(g_socArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(g_socArc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(g_socArc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g_socArc, colCardEdge(), LV_PART_MAIN);
  lv_obj_set_style_arc_color(g_socArc, colAccent(), LV_PART_INDICATOR);

  g_socLabel = makeLabel(p, "--", &lv_font_montserrat_48, colText());
  lv_obj_align_to(g_socLabel, g_socArc, LV_ALIGN_CENTER, 0, -10);

  // Four readouts to the right of the arc
  static const char* kTileNames[4] = {"Volts", "Amps", "Watts", "Battery"};
  for (int i = 0; i < 4; i++) {
    const int col = i % 2, row = i / 2;
    lv_obj_t* card = makeCard(p, 184 + col * 142, 4 + row * 86, 134, 78);
    lv_obj_t* name = makeLabel(card, kTileNames[i], &lv_font_montserrat_14, colMuted());
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);
    g_tileValue[i] = makeLabel(card, "--", &lv_font_montserrat_28, colText());
    lv_obj_align(g_tileValue[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
  }

  // Cell bars
  // Row 3 is three equal blocks across the content width: cells, capacity,
  // energy. kRow3* are shared with refreshPowerPage(), which re-lays the bars
  // whenever the pack's cell count changes.
  lv_obj_t* cellCard = makeCard(p, kRow3X[0], kRow3Y, kRow3W, kRow3H);
  lv_obj_t* cellTitle = makeLabel(cellCard, "Cells", &lv_font_montserrat_14, colMuted());
  lv_obj_align(cellTitle, LV_ALIGN_TOP_LEFT, 0, 0);
  g_cellSummary = makeLabel(cellCard, "", &lv_font_montserrat_14, colMuted());
  lv_obj_align(g_cellSummary, LV_ALIGN_TOP_RIGHT, 0, 0);

  // Bars are positioned and sized in refreshPowerPage() once the real cell
  // count is known; a 4S pack gets wide bars, a 16S pack gets thin ones.
  for (int i = 0; i < kMaxCellBars; i++) {
    lv_obj_t* bar = lv_bar_create(cellCard);
    lv_obj_set_size(bar, 8, kCellBarH);
    lv_obj_set_pos(bar, 0, kCellBarY);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, colBg(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, colAccent(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    g_cellBar[i] = bar;

    lv_obj_t* lab = makeLabel(cellCard, "", &lv_font_montserrat_14, colMuted());
    lv_obj_set_pos(lab, 0, kCellLabelY);
    lv_obj_add_flag(lab, LV_OBJ_FLAG_HIDDEN);
    g_cellLabel[i] = lab;
  }

  // Capacity and energy, lifted out from under the SOC arc into blocks of
  // their own so they read like the other measurements rather than a caption.
  static const char* kCapNames[2] = {"Capacity", "Energy"};
  lv_obj_t** kCapValues[2] = {&g_capValue, &g_energyValue};
  for (int i = 0; i < 2; i++) {
    lv_obj_t* card = makeCard(p, kRow3X[i + 1], kRow3Y, kRow3W, kRow3H);
    lv_obj_t* name = makeLabel(card, kCapNames[i], &lv_font_montserrat_14, colMuted());
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);
    *kCapValues[i] = makeLabel(card, "--", &lv_font_montserrat_28, colText());
    lv_obj_align(*kCapValues[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
  }

  // Status strip
  lv_obj_t* statusCard = makeCard(p, 4, kStatusY, 456, kStatusH);
  g_statusLabel = makeLabel(statusCard, "Looking for BMS", &lv_font_montserrat_20,
                            colText());
  lv_obj_align(g_statusLabel, LV_ALIGN_LEFT_MID, 0, 0);
  g_fetLabel = makeLabel(statusCard, "", &lv_font_montserrat_16, colMuted());
  lv_obj_align(g_fetLabel, LV_ALIGN_RIGHT_MID, 0, 0);
}

// ---- text entry overlay -----------------------------------------------------
// One keyboard, several callers. `showTextEntry` takes what to ask and what to
// do with the answer; the overlay knows nothing about sensors or clocks.

void closeEntry() {
  if (!g_entry) return;
  lv_obj_add_flag(g_entry, LV_OBJ_FLAG_HIDDEN);
  g_entryOnSave = nullptr;
  // Deliberately does NOT clear g_renameMac. It is set by showRename() and
  // read by renameSaved(), which runs *after* this - clearing it here made
  // every rename silently do nothing, because the callback found an empty
  // address and returned. A stale value is harmless: only renameSaved reads
  // it, and only as the rename callback.
  bsp::wakeBacklight();
}

void entryCancelCb(lv_event_t* /*e*/) { closeEntry(); }

void entrySaveCb(lv_event_t* /*e*/) {
  // Copy the callback out before closing: closeEntry() clears it, and the
  // callback is free to open the overlay again.
  void (*fn)(const char*) = g_entryOnSave;
  const char* text = lv_textarea_get_text(g_entryInput);
  char copy[64];
  snprintf(copy, sizeof(copy), "%s", text ? text : "");
  closeEntry();
  if (fn) fn(copy);
}

void showTextEntry(const char* title, const char* subtitle, const char* initial,
                   const char* placeholder, uint32_t maxLen,
                   void (*onSave)(const char*)) {
  if (!g_entry) return;
  lv_label_set_text(g_entryTitle, title ? title : "");
  lv_label_set_text(g_entrySubtitle, subtitle ? subtitle : "");
  lv_textarea_set_max_length(g_entryInput, maxLen);
  lv_textarea_set_placeholder_text(g_entryInput, placeholder ? placeholder : "");
  lv_textarea_set_text(g_entryInput, initial ? initial : "");
  g_entryOnSave = onSave;
  lv_obj_remove_flag(g_entry, LV_OBJ_FLAG_HIDDEN);
  bsp::wakeBacklight();
}

void buildEntry(lv_obj_t* scr) {
  g_entry = lv_obj_create(scr);
  lv_obj_set_pos(g_entry, 0, 0);
  lv_obj_set_size(g_entry, kScreenW, kScreenH);
  lv_obj_set_style_bg_color(g_entry, colBg(), 0);
  lv_obj_set_style_border_width(g_entry, 0, 0);
  lv_obj_set_style_radius(g_entry, 0, 0);
  lv_obj_set_style_pad_all(g_entry, 0, 0);
  lv_obj_remove_flag(g_entry, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_entry, LV_OBJ_FLAG_HIDDEN);

  g_entryTitle = makeLabel(g_entry, "", &lv_font_montserrat_20, colText());
  lv_obj_align(g_entryTitle, LV_ALIGN_TOP_LEFT, 14, 12);

  g_entrySubtitle = makeLabel(g_entry, "", &lv_font_montserrat_14, colMuted());
  lv_obj_align(g_entrySubtitle, LV_ALIGN_TOP_LEFT, 14, 38);

  g_entryInput = lv_textarea_create(g_entry);
  lv_obj_set_pos(g_entryInput, 14, 62);
  lv_obj_set_size(g_entryInput, kScreenW - 28, 52);
  lv_textarea_set_one_line(g_entryInput, true);
  lv_obj_set_style_text_font(g_entryInput, &lv_font_montserrat_20, 0);
  lv_obj_set_style_bg_color(g_entryInput, colCard(), 0);
  lv_obj_set_style_border_color(g_entryInput, colCardEdge(), 0);
  lv_obj_set_style_text_color(g_entryInput, colText(), 0);

  lv_obj_t* cancel = lv_button_create(g_entry);
  lv_obj_set_size(cancel, 130, 44);
  lv_obj_set_pos(cancel, 14, 124);
  lv_obj_set_style_radius(cancel, 8, 0);
  lv_obj_set_style_bg_color(cancel, colCard(), 0);
  lv_obj_add_event_cb(cancel, entryCancelCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(makeLabel(cancel, "Cancel", &lv_font_montserrat_16, colMuted()));

  lv_obj_t* save = lv_button_create(g_entry);
  lv_obj_set_size(save, 130, 44);
  lv_obj_set_pos(save, kScreenW - 144, 124);
  lv_obj_set_style_radius(save, 8, 0);
  lv_obj_set_style_bg_color(save, colAccent(), 0);
  lv_obj_add_event_cb(save, entrySaveCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(makeLabel(save, "Save", &lv_font_montserrat_16, lv_color_black()));

  lv_obj_t* kb = lv_keyboard_create(g_entry);
  lv_obj_set_size(kb, kScreenW, 292);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, g_entryInput);
  // The keyboard's own tick and cross are the keys a thumb lands on first, so
  // wire them to the same actions as the buttons above it.
  lv_obj_add_event_cb(kb, entrySaveCb, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(kb, entryCancelCb, LV_EVENT_CANCEL, nullptr);
}

// ---- renaming a sensor ------------------------------------------------------

void renameSaved(const char* text) {
  if (!g_renameMac[0]) return;
  // An empty box means "go back to whatever the sensor calls itself" rather
  // than a sensor with no name at all.
  envSensors().rename(g_renameMac, (text && text[0]) ? text : nullptr);
}

void showRename(const char* mac) {
  if (mac == nullptr) return;
  const EnvSensors& sensors = envSensors();
  const EnvSensor* found = nullptr;
  for (size_t i = 0; i < sensors.count(); i++) {
    const EnvSensor* s = sensors.at(i);
    if (s && strcmp(s->mac, mac) == 0) {
      found = s;
      break;
    }
  }
  if (!found) return;

  // showTextEntry clears this via closeEntry, so it is set after the overlay
  // is configured, not before.
  char sub[64];
  snprintf(sub, sizeof(sub), "%s  -  %s", found->subtitle(), found->mac);
  // Seed with the current custom name only. Pre-filling the advertised name
  // would make "clear it back to the default" impossible without knowing to
  // empty the box first.
  showTextEntry("Rename sensor", sub, found->customName, "Living room",
                Settings::kSensorNameLen - 1, renameSaved);

  strncpy(g_renameMac, mac, sizeof(g_renameMac) - 1);
  g_renameMac[sizeof(g_renameMac) - 1] = '\0';
}

// ---- setting the clock -----------------------------------------------------
// There is no RTC and no NTP on this board, so this is the only way the device
// ever learns what time it is - and the 24-hour graph and the daily high and
// low both hang off it. See core/clock.h.
//
// Five columns of up/down steppers rather than a text box. Every target is
// 86x58, which is larger than the nav bar's buttons, because this gets used
// once in a blue moon by someone who has just plugged the van in and does not
// want to fight the screen. The arithmetic behind the arrows is
// `stepDateField()` in core/clock.cpp, where it is tested.

constexpr int kStepCols = (int)DateField::COUNT;
constexpr int kStepW = 86;
constexpr int kStepGap = 6;
constexpr int kStepBtnH = 58;
constexpr int kStepX0 = (kScreenW - (kStepCols * kStepW + (kStepCols - 1) * kStepGap)) / 2;
constexpr int kStepUpY = kHeaderH + 68;
constexpr int kStepValueY = kStepUpY + kStepBtnH + 12;
constexpr int kStepDownY = kStepValueY + 44;
constexpr int kStepNameY = kStepDownY + kStepBtnH + 6;

void refreshClockSet() {
  if (!g_clockSet || !g_clockPreview) return;
  char buf[24];
  lv_label_set_text(g_clockPreview,
                    Clock::formatStamp(Clock::make(g_clockEdit), buf, sizeof(buf)));

  for (int i = 0; i < kStepCols; i++) {
    switch ((DateField)i) {
      case DateField::Year:
        snprintf(buf, sizeof(buf), "%u", (unsigned)g_clockEdit.year);
        break;
      case DateField::Month:
        snprintf(buf, sizeof(buf), "%02u", (unsigned)g_clockEdit.month);
        break;
      case DateField::Day:
        snprintf(buf, sizeof(buf), "%02u", (unsigned)g_clockEdit.day);
        break;
      case DateField::Hour:
        snprintf(buf, sizeof(buf), "%02u", (unsigned)g_clockEdit.hour);
        break;
      default:
        snprintf(buf, sizeof(buf), "%02u", (unsigned)g_clockEdit.minute);
        break;
    }
    lv_label_set_text(g_clockFieldValue[i], buf);
  }
}

// One matrix per row; the user data says which way that row steps, and the
// pressed button's index says which field.
void clockStepCb(lv_event_t* e) {
  if (!g_clockSet) return;
  lv_obj_t* row = (lv_obj_t*)lv_event_get_target(e);
  const uint32_t id = lv_buttonmatrix_get_selected_button(row);
  if (id >= (uint32_t)DateField::COUNT) return;  // LV_BUTTONMATRIX_BUTTON_NONE
  const int delta = (intptr_t)lv_event_get_user_data(e);
  stepDateField(g_clockEdit, (DateField)id, delta);
  refreshClockSet();
  bsp::wakeBacklight();
}

// Freed, not hidden - the first overlay in this project to be built on demand.
//
// Every other screen is built once at boot and kept, and `decisions/0013` set
// the LVGL pool to hold all of them at once. This screen did not fit: five
// columns of steppers left the pool at 82% used with the graph still to draw
// on top. 0013 named this as the answer when RAM got tight, and it has.
//
// It is the safe one to start with: opened perhaps once in the life of the
// van, and every pointer it caches is nulled here, synchronously, before the
// delete is even queued. The delete is *async* because this runs from inside
// the Apply button's own event callback, and freeing a widget's ancestor
// mid-dispatch is how LVGL is made to read freed memory.
void closeClockSet() {
  if (!g_clockSet) return;
  lv_obj_t* doomed = g_clockSet;

  g_clockSet = nullptr;
  g_clockPreview = nullptr;
  g_clockUpRow = nullptr;
  g_clockDownRow = nullptr;
  for (int i = 0; i < kStepCols; i++) g_clockFieldValue[i] = nullptr;

  lv_obj_delete_async(doomed);
  bsp::wakeBacklight();
}

void clockCancelCb(lv_event_t* /*e*/) { closeClockSet(); }

void clockApplyCb(lv_event_t* /*e*/) {
  Settings::instance().setClock(Clock::make(g_clockEdit));
  closeClockSet();
  refreshSettings();
}

void buildClockSet(lv_obj_t* scr);

void setClockCb(lv_event_t* /*e*/) {
  if (g_clockSet) return;  // already open
  buildClockSet(lv_screen_active());
  if (!g_clockSet) {
    log_e("clock: no room to build the setter");
    return;
  }
  Clock& clk = Clock::instance();
  if (clk.isSet()) {
    // Seed from the running clock, even when it is only the approximate one
    // restored from flash: it is usually right to the hour, so most of the
    // arrows need no pressing at all.
    g_clockEdit = Clock::breakDown(clk.now());
  } else {
    g_clockEdit = DateTime{};
    g_clockEdit.year = kMinYear;
    g_clockEdit.month = 1;
    g_clockEdit.day = 1;
    g_clockEdit.hour = 12;
    g_clockEdit.minute = 0;
  }
  refreshClockSet();
  bsp::wakeBacklight();
}

lv_obj_t* makeStepRow(lv_obj_t* parent, int y, const char* symbol, int delta) {
  // The map is static because lv_buttonmatrix keeps the pointer rather than
  // copying it. A stack array here would be a dangling read on the first draw.
  static const char* kUp[] = {LV_SYMBOL_UP, LV_SYMBOL_UP, LV_SYMBOL_UP,
                              LV_SYMBOL_UP, LV_SYMBOL_UP, ""};
  static const char* kDown[] = {LV_SYMBOL_DOWN, LV_SYMBOL_DOWN, LV_SYMBOL_DOWN,
                                LV_SYMBOL_DOWN, LV_SYMBOL_DOWN, ""};

  lv_obj_t* m = lv_buttonmatrix_create(parent);
  lv_buttonmatrix_set_map(m, delta > 0 ? kUp : kDown);
  lv_obj_set_pos(m, kStepX0, y);
  lv_obj_set_size(m, kStepCols * kStepW + (kStepCols - 1) * kStepGap, kStepBtnH);
  lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(m, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(m, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(m, kStepGap, LV_PART_MAIN);
  lv_obj_set_style_bg_color(m, colCard(), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, colAccent(), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(m, colText(), LV_PART_ITEMS);
  lv_obj_set_style_text_font(m, &lv_font_montserrat_20, LV_PART_ITEMS);
  lv_obj_set_style_radius(m, 10, LV_PART_ITEMS);
  lv_obj_set_style_border_width(m, 0, LV_PART_ITEMS);
  // VALUE_CHANGED fires on press; the repeat is what makes holding an arrow
  // run 2026 up to 2031 instead of asking for five separate taps.
  lv_obj_add_event_cb(m, clockStepCb, LV_EVENT_VALUE_CHANGED,
                      (void*)(intptr_t)delta);
  lv_obj_add_event_cb(m, clockStepCb, LV_EVENT_LONG_PRESSED_REPEAT,
                      (void*)(intptr_t)delta);
  (void)symbol;
  return m;
}

void buildClockSet(lv_obj_t* scr) {
  g_clockSet = lv_obj_create(scr);
  lv_obj_set_pos(g_clockSet, 0, 0);
  lv_obj_set_size(g_clockSet, kScreenW, kScreenH);
  lv_obj_set_style_bg_color(g_clockSet, colBg(), 0);
  lv_obj_set_style_border_width(g_clockSet, 0, 0);
  lv_obj_set_style_radius(g_clockSet, 0, 0);
  lv_obj_set_style_pad_all(g_clockSet, 0, 0);
  lv_obj_remove_flag(g_clockSet, LV_OBJ_FLAG_SCROLLABLE);
  // No HIDDEN flag: this is built at the moment it is wanted and deleted when
  // it is dismissed, so it is never sitting around invisible.

  lv_obj_t* bar = lv_obj_create(g_clockSet);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, kScreenW, kHeaderH);
  lv_obj_set_style_bg_color(bar, colCard(), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = makeLabel(bar, "Set the clock", &lv_font_montserrat_20, colText());
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 14, 0);

  lv_obj_t* close = lv_button_create(bar);
  lv_obj_set_size(close, 40, 34);
  lv_obj_align(close, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_set_style_radius(close, 8, 0);
  lv_obj_set_style_bg_color(close, colBg(), 0);
  lv_obj_add_event_cb(close, clockCancelCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(makeLabel(close, LV_SYMBOL_CLOSE, &lv_font_montserrat_20, colMuted()));

  // The whole date and time in one line, so what the arrows are building is
  // legible without reading five separate boxes.
  g_clockPreview = makeLabel(g_clockSet, "", &lv_font_montserrat_28, colAccent());
  lv_obj_align(g_clockPreview, LV_ALIGN_TOP_MID, 0, kHeaderH + 14);

  g_clockUpRow = makeStepRow(g_clockSet, kStepUpY, LV_SYMBOL_UP, +1);
  g_clockDownRow = makeStepRow(g_clockSet, kStepDownY, LV_SYMBOL_DOWN, -1);

  for (int i = 0; i < kStepCols; i++) {
    const int x = kStepX0 + i * (kStepW + kStepGap);
    const DateField f = (DateField)i;

    g_clockFieldValue[i] = makeLabel(g_clockSet, "--", &lv_font_montserrat_28,
                                     colText());
    lv_obj_set_width(g_clockFieldValue[i], kStepW);
    lv_obj_set_style_text_align(g_clockFieldValue[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(g_clockFieldValue[i], x, kStepValueY);

    lv_obj_t* name = makeLabel(g_clockSet, dateFieldName(f),
                               &lv_font_montserrat_14, colMuted());
    lv_obj_set_width(name, kStepW);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(name, x, kStepNameY);
  }

  lv_obj_t* apply = lv_button_create(g_clockSet);
  lv_obj_set_size(apply, 260, 64);
  lv_obj_align(apply, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_radius(apply, 10, 0);
  lv_obj_set_style_bg_color(apply, colAccent(), 0);
  lv_obj_add_event_cb(apply, clockApplyCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(makeLabel(apply, "Set clock", &lv_font_montserrat_20,
                          lv_color_black()));
}

// ---- sensor detail ----------------------------------------------------------
// What a tile opens: the two readings large, and an hour and a half of trend
// behind them. The graph is the reason the history ring exists - a number tells
// you the fridge is at 6 degrees, a line tells you it is on its way up.

constexpr int kDetailCardX = 12;
constexpr int kDetailCardW = kScreenW - 24;
constexpr int kChartX = 46;          // room for the temperature axis labels
constexpr int kChartY = 26;
constexpr int kChartW = kDetailCardW - 16 - 2 * kChartX;
constexpr int kChartH = 148;
// The readings card grew a "today" line under each value, so everything below
// it moved down. Laid out from the top rather than by magic numbers: 44 bar,
// 12 gap, 112 readings, 6 gap, 18 provenance line, 6 gap, then the chart card
// down to the Rename button at kScreenH - 56.
constexpr int kNowCardY = kHeaderH + 12;
constexpr int kNowCardH = 112;
constexpr int kSubY = kNowCardY + kNowCardH + 6;
constexpr int kChartCardY = kSubY + 24;
constexpr int kChartCardH = kScreenH - 64 - kChartCardY;

// Maps one sample onto the chart's 0..kChartSpan, given the extremes actually
// present. A flat series would divide by zero, so it is pinned to the middle.
int32_t chartScale(float v, float lo, float hi) {
  const float span = hi - lo;
  if (span < 0.05f) return kChartSpan / 2;
  float f = (v - lo) / span;
  if (f < 0.0f) f = 0.0f;
  if (f > 1.0f) f = 1.0f;
  return (int32_t)(f * kChartSpan);
}

const EnvSensor* findSensor(const char* mac, size_t* indexOut) {
  if (mac == nullptr || mac[0] == '\0') return nullptr;
  const EnvSensors& sensors = envSensors();
  for (size_t i = 0; i < sensors.count(); i++) {
    const EnvSensor* s = sensors.at(i);
    if (s && strcmp(s->mac, mac) == 0) {
      if (indexOut) *indexOut = i;
      return s;
    }
  }
  return nullptr;
}

void refreshDetail() {
  if (!g_detail || lv_obj_has_flag(g_detail, LV_OBJ_FLAG_HIDDEN)) return;

  size_t idx = 0;
  const EnvSensor* s = findSensor(g_detailMac, &idx);
  if (!s) return;  // the slot was reused out from under us; leave the last draw
  const History* h = envSensors().historyAt(idx);
  const DayExtremes* day = envSensors().extremesAt(idx);

  const bool stale = s->stale();
  char buf[48];

  lv_label_set_text(g_detailTitle, s->displayName());

  if (s->haveTemp) {
    formatTemp(s->tempC, buf, sizeof(buf));
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  lv_label_set_text(g_detailTemp, buf);
  lv_obj_set_style_text_color(g_detailTemp, stale ? colMuted() : colText(), 0);

  if (s->haveHum) {
    snprintf(buf, sizeof(buf), "%.0f %%", s->humPct);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  lv_label_set_text(g_detailHum, buf);
  lv_obj_set_style_text_color(g_detailHum, stale ? colMuted() : colText(), 0);

  // The line under the readings is the provenance: what the sensor calls
  // itself, where it is, how it is doing and whether any of this is current.
  const uint32_t age = s->lastHeardMs ? (millis() - s->lastHeardMs) / 1000 : 0;
  int n = snprintf(buf, sizeof(buf), "%s  -  %s", s->subtitle(), s->mac);
  if (s->haveBatt && n > 0 && (size_t)n < sizeof(buf)) {
    n += snprintf(buf + n, sizeof(buf) - n, "   batt %u%%", (unsigned)s->battPct);
  }
  if (stale && n > 0 && (size_t)n < sizeof(buf)) {
    snprintf(buf + n, sizeof(buf) - n, "   %lus ago", (unsigned long)age);
  }
  lv_label_set_text(g_detailSub, buf);

  // Today's high and low, from the per-minute log rather than the ten-minute
  // graph ring - see core/datalog.h. Blank rather than zeroes before the first
  // reading of the day lands: "0.0 / 0.0" reads like a measurement.
  if (day && day->haveTemp) {
    char lo[16], hi[16];
    formatTemp(day->tMinC, lo, sizeof(lo));
    formatTemp(day->tMaxC, hi, sizeof(hi));
    snprintf(buf, sizeof(buf), "today  %s  to  %s", lo, hi);
    lv_label_set_text(g_detailTempToday, buf);
  } else {
    lv_label_set_text(g_detailTempToday, Clock::instance().isSet() ? "today  --" : "");
  }
  if (day && day->haveHum) {
    snprintf(buf, sizeof(buf), "today  %.0f to %.0f %%", day->hMin, day->hMax);
    lv_label_set_text(g_detailHumToday, buf);
  } else {
    lv_label_set_text(g_detailHumToday, "");
  }

  snprintf(buf, sizeof(buf), "Temperature (%s)   /   Humidity (%%)",
           tempUnitSuffix());
  lv_label_set_text(g_detailLegend, buf);

  // ---- the graph ----
  // Both series are normalised here rather than by LVGL so each fills the plot
  // on its own scale; the real extremes go on the labels beside it.
  float tLo = 0.0f, tHi = 0.0f, hLo = 0.0f, hHi = 0.0f;
  const bool haveT = h && h->tempRangeC(tLo, tHi);
  const bool haveH = h && h->humRange(hLo, hHi);

  const size_t count = h ? h->count() : 0;
  // Newest sample at the right edge, so the line grows leftwards into an empty
  // chart rather than stretching to fill one.
  const size_t offset = History::kSamples - count;

  for (size_t i = 0; i < History::kSamples; i++) {
    int32_t tv = LV_CHART_POINT_NONE;
    int32_t hv = LV_CHART_POINT_NONE;
    if (i >= offset) {
      const size_t si = i - offset;
      const int16_t t = h->tempAt(si);
      const int16_t hu = h->humAt(si);
      if (t != History::kNoValue && haveT) {
        tv = chartScale(t / 10.0f, tLo, tHi);
      }
      if (hu != History::kNoValue && haveH) {
        hv = chartScale((float)hu, hLo, hHi);
      }
    }
    lv_chart_set_value_by_id(g_detailChart, g_detailTempSeries, i, tv);
    lv_chart_set_value_by_id(g_detailChart, g_detailHumSeries, i, hv);
  }
  lv_chart_refresh(g_detailChart);

  if (haveT) {
    formatTemp(tHi, buf, sizeof(buf));
    lv_label_set_text(g_detailTempHi, buf);
    formatTemp(tLo, buf, sizeof(buf));
    lv_label_set_text(g_detailTempLo, buf);
  } else {
    lv_label_set_text(g_detailTempHi, "");
    lv_label_set_text(g_detailTempLo, "");
  }
  if (haveH) {
    snprintf(buf, sizeof(buf), "%.0f %%", hHi);
    lv_label_set_text(g_detailHumHi, buf);
    snprintf(buf, sizeof(buf), "%.0f %%", hLo);
    lv_label_set_text(g_detailHumLo, buf);
  } else {
    lv_label_set_text(g_detailHumHi, "");
    lv_label_set_text(g_detailHumLo, "");
  }

  // Say how much history is on screen rather than implying a fixed window: for
  // the first hour and a half the line is shorter than the chart.
  if (count < 2) {
    lv_label_set_text(g_detailSpan, "collecting - no trend yet");
  } else {
    const uint32_t mins = h->spanMs() / 60000UL;
    // Say how much history is actually on screen rather than claiming a fixed
    // window: for the first day after a boot the line is shorter than the
    // chart, and a "24 h" label over six hours of data would be a lie.
    char when[24] = {0};
    if (Clock::instance().isSet()) {
      char at[8];
      snprintf(when, sizeof(when), "  to %s",
               Clock::formatTime(Clock::instance().now(), at, sizeof(at)));
    }
    if (mins >= 60) {
      snprintf(buf, sizeof(buf), "last %luh %lum%s", (unsigned long)(mins / 60),
               (unsigned long)(mins % 60), when);
    } else {
      snprintf(buf, sizeof(buf), "last %lum%s", (unsigned long)mins, when);
    }
    lv_label_set_text(g_detailSpan, buf);
  }
}

void showDetail(const char* mac) {
  if (!g_detail || mac == nullptr) return;
  if (!findSensor(mac, nullptr)) return;

  strncpy(g_detailMac, mac, sizeof(g_detailMac) - 1);
  g_detailMac[sizeof(g_detailMac) - 1] = '\0';

  lv_obj_remove_flag(g_detail, LV_OBJ_FLAG_HIDDEN);
  refreshDetail();
  bsp::wakeBacklight();
}

void closeDetailCb(lv_event_t* /*e*/) {
  lv_obj_add_flag(g_detail, LV_OBJ_FLAG_HIDDEN);
  g_detailMac[0] = '\0';
  bsp::wakeBacklight();
}

void detailRenameCb(lv_event_t* /*e*/) { showRename(g_detailMac); }

void sensorTileCb(lv_event_t* e) {
  const char* mac = static_cast<const char*>(lv_event_get_user_data(e));
  showDetail(mac);
}

void buildDetail(lv_obj_t* scr) {
  g_detail = lv_obj_create(scr);
  lv_obj_set_pos(g_detail, 0, 0);
  lv_obj_set_size(g_detail, kScreenW, kScreenH);
  lv_obj_set_style_bg_color(g_detail, colBg(), 0);
  lv_obj_set_style_border_width(g_detail, 0, 0);
  lv_obj_set_style_radius(g_detail, 0, 0);
  lv_obj_set_style_pad_all(g_detail, 0, 0);
  lv_obj_remove_flag(g_detail, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_detail, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* bar = lv_obj_create(g_detail);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_size(bar, kScreenW, kHeaderH);
  lv_obj_set_style_bg_color(bar, colCard(), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  g_detailTitle = makeLabel(bar, "", &lv_font_montserrat_20, colText());
  lv_obj_align(g_detailTitle, LV_ALIGN_LEFT_MID, 14, 0);
  lv_obj_set_width(g_detailTitle, kScreenW - 80);
  lv_label_set_long_mode(g_detailTitle, LV_LABEL_LONG_DOT);

  lv_obj_t* close = lv_button_create(bar);
  lv_obj_set_size(close, 40, 34);
  lv_obj_align(close, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_set_style_radius(close, 8, 0);
  lv_obj_set_style_bg_color(close, colBg(), 0);
  lv_obj_add_event_cb(close, closeDetailCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(makeLabel(close, LV_SYMBOL_CLOSE, &lv_font_montserrat_20, colMuted()));

  // ---- the two readings, large, with today's range under each ----
  lv_obj_t* now = makeCard(g_detail, kDetailCardX, kNowCardY, kDetailCardW,
                           kNowCardH);
  lv_obj_t* tName = makeLabel(now, "Temperature", &lv_font_montserrat_14, colMuted());
  lv_obj_align(tName, LV_ALIGN_TOP_LEFT, 0, 0);
  g_detailTemp = makeLabel(now, "--", &lv_font_montserrat_48, colText());
  lv_obj_align(g_detailTemp, LV_ALIGN_TOP_LEFT, 0, 18);
  g_detailTempToday = makeLabel(now, "", &lv_font_montserrat_14, colMuted());
  lv_obj_align(g_detailTempToday, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t* hName = makeLabel(now, "Humidity", &lv_font_montserrat_14, colMuted());
  lv_obj_align(hName, LV_ALIGN_TOP_RIGHT, 0, 0);
  g_detailHum = makeLabel(now, "--", &lv_font_montserrat_28, colText());
  lv_obj_align(g_detailHum, LV_ALIGN_TOP_RIGHT, 0, 24);
  g_detailHumToday = makeLabel(now, "", &lv_font_montserrat_14, colMuted());
  lv_obj_align(g_detailHumToday, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  g_detailSub = makeLabel(g_detail, "", &lv_font_montserrat_14, colMuted());
  lv_obj_align(g_detailSub, LV_ALIGN_TOP_LEFT, kDetailCardX + 8, kSubY);

  // ---- the trend ----
  lv_obj_t* card = makeCard(g_detail, kDetailCardX, kChartCardY, kDetailCardW,
                            kChartCardH);
  g_detailLegend = makeLabel(card, "", &lv_font_montserrat_14, colMuted());
  lv_obj_align(g_detailLegend, LV_ALIGN_TOP_LEFT, 0, 0);

  g_detailChart = lv_chart_create(card);
  lv_obj_set_pos(g_detailChart, kChartX, kChartY);
  lv_obj_set_size(g_detailChart, kChartW, kChartH);
  lv_chart_set_type(g_detailChart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(g_detailChart, History::kSamples);
  lv_chart_set_range(g_detailChart, LV_CHART_AXIS_PRIMARY_Y, 0, kChartSpan);
  lv_chart_set_range(g_detailChart, LV_CHART_AXIS_SECONDARY_Y, 0, kChartSpan);
  lv_chart_set_div_line_count(g_detailChart, 3, 4);
  lv_obj_set_style_bg_color(g_detailChart, colBg(), 0);
  lv_obj_set_style_border_width(g_detailChart, 0, 0);
  lv_obj_set_style_radius(g_detailChart, 6, 0);
  lv_obj_set_style_pad_all(g_detailChart, 4, 0);
  lv_obj_set_style_line_color(g_detailChart, colCardEdge(), LV_PART_MAIN);
  lv_obj_remove_flag(g_detailChart, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(g_detailChart, LV_OBJ_FLAG_CLICKABLE);
  // 90 points across 348 pixels: a marker per sample would be a solid band, so
  // the line is the whole of it.
  lv_obj_set_style_width(g_detailChart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(g_detailChart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(g_detailChart, 3, LV_PART_ITEMS);

  g_detailTempSeries =
      lv_chart_add_series(g_detailChart, colAccent(), LV_CHART_AXIS_PRIMARY_Y);
  g_detailHumSeries =
      lv_chart_add_series(g_detailChart, colCharge(), LV_CHART_AXIS_SECONDARY_Y);

  // The axis labels are ours rather than LVGL's: its ticks are integers, which
  // cannot say "20.4" - and two independently scaled series need two scales.
  g_detailTempHi = makeLabel(card, "", &lv_font_montserrat_14, colAccent());
  lv_obj_set_pos(g_detailTempHi, 0, kChartY - 2);
  g_detailTempLo = makeLabel(card, "", &lv_font_montserrat_14, colAccent());
  lv_obj_set_pos(g_detailTempLo, 0, kChartY + kChartH - 16);

  g_detailHumHi = makeLabel(card, "", &lv_font_montserrat_14, colCharge());
  lv_obj_align(g_detailHumHi, LV_ALIGN_TOP_RIGHT, 0, kChartY - 2);
  g_detailHumLo = makeLabel(card, "", &lv_font_montserrat_14, colCharge());
  lv_obj_align(g_detailHumLo, LV_ALIGN_TOP_RIGHT, 0, kChartY + kChartH - 16);

  g_detailSpan = makeLabel(card, "", &lv_font_montserrat_14, colMuted());
  lv_obj_align(g_detailSpan, LV_ALIGN_BOTTOM_MID, 0, 0);

  // Renaming lives here rather than on the tile: a tap on a tile should show
  // you the sensor, not offer to edit it.
  lv_obj_t* rename = lv_button_create(g_detail);
  lv_obj_set_size(rename, 150, 44);
  lv_obj_set_pos(rename, kDetailCardX, kScreenH - 56);
  lv_obj_set_style_radius(rename, 8, 0);
  lv_obj_set_style_bg_color(rename, colCard(), 0);
  lv_obj_add_event_cb(rename, detailRenameCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_center(makeLabel(rename, "Rename", &lv_font_montserrat_16, colText()));
}

// ---- climate page -----------------------------------------------------------
// One tile per sensor, two across: the name, the temperature big enough to read
// from the other end of the van, humidity and battery underneath. Tapping one
// opens its detail and graph.

void buildClimatePage(lv_obj_t* scr) {
  lv_obj_t* p = makePage(scr);
  g_pages[(int)Domain::Climate] = p;

  g_climateGrid = lv_obj_create(p);
  lv_obj_set_pos(g_climateGrid, 4, 4);
  // 20px shorter than the page, to leave the status line below it room.
  lv_obj_set_size(g_climateGrid, 456, kContentH - 44);
  lv_obj_set_style_bg_color(g_climateGrid, colBg(), 0);
  lv_obj_set_style_border_width(g_climateGrid, 0, 0);
  lv_obj_set_style_pad_all(g_climateGrid, 0, 0);
  lv_obj_set_style_pad_column(g_climateGrid, kTileGap, 0);
  lv_obj_set_style_pad_row(g_climateGrid, kTileGap, 0);
  // Eight sensors is four rows and three fit, so the grid scrolls.
  lv_obj_set_flex_flow(g_climateGrid, LV_FLEX_FLOW_ROW_WRAP);

  g_climateEmpty = makeLabel(
      p, "No sensors heard yet.\n\nAny BTHome v2 broadcaster\nin range appears here.",
      &lv_font_montserrat_16, colMuted());
  lv_obj_set_style_text_align(g_climateEmpty, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(g_climateEmpty);

  // Logging has two ways of quietly doing nothing - an unset clock and a
  // filesystem that would not mount - and both look exactly like a feature
  // that was never built unless the page says so.
  g_climateStatus = makeLabel(p, "", &lv_font_montserrat_14, colMuted());
  lv_obj_align(g_climateStatus, LV_ALIGN_BOTTOM_LEFT, 4, -2);
}

void refreshClimatePage() {
  if (!g_climateGrid) return;
  EnvSensors& sensors = envSensors();
  const size_t n = sensors.count();

  if (n == 0) {
    lv_obj_remove_flag(g_climateEmpty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_climateGrid, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(g_climateEmpty, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g_climateGrid, LV_OBJ_FLAG_HIDDEN);

  // Tiles are created once and then only their text changes. The click handler
  // gets a pointer to the slot's own mac buffer, which is stable for the life
  // of the table - the slot array never moves.
  while (lv_obj_get_child_count(g_climateGrid) < n) {
    const size_t idx = lv_obj_get_child_count(g_climateGrid);
    lv_obj_t* tile = makeCard(g_climateGrid, 0, 0, kTileW, kTileH);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    const EnvSensor* slot = sensors.at(idx);
    lv_obj_add_event_cb(tile, sensorTileCb, LV_EVENT_CLICKED,
                        const_cast<char*>(slot->mac));

    // A sensor called "Under the bed" must not push the layout around, so the
    // name is width-limited and ellipsised rather than left to wrap.
    lv_obj_t* name = makeLabel(tile, "", &lv_font_montserrat_16, colText());
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_width(name, kTileW - 16);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    lv_obj_t* temp = makeLabel(tile, "", &lv_font_montserrat_28, colText());
    lv_obj_align(temp, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_obj_t* hum = makeLabel(tile, "", &lv_font_montserrat_20, colMuted());
    lv_obj_align(hum, LV_ALIGN_TOP_RIGHT, 0, 34);
    lv_obj_t* sub = makeLabel(tile, "", &lv_font_montserrat_14, colMuted());
    lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  }

  char buf[48];
  for (size_t i = 0; i < n; i++) {
    const EnvSensor* s = sensors.at(i);
    if (!s) continue;
    lv_obj_t* tile = lv_obj_get_child(g_climateGrid, i);
    const bool stale = s->stale();

    lv_label_set_text(lv_obj_get_child(tile, 0), s->displayName());
    lv_obj_set_style_text_color(lv_obj_get_child(tile, 0),
                                stale ? colMuted() : colText(), 0);

    if (s->haveTemp) {
      formatTemp(s->tempC, buf, sizeof(buf));
    } else {
      snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(lv_obj_get_child(tile, 1), buf);
    lv_obj_set_style_text_color(lv_obj_get_child(tile, 1),
                                stale ? colMuted() : colText(), 0);

    if (s->haveHum) {
      snprintf(buf, sizeof(buf), "%.0f %%", s->humPct);
    } else {
      snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(lv_obj_get_child(tile, 2), buf);

    if (s->haveBatt) {
      snprintf(buf, sizeof(buf), "batt %u%%", (unsigned)s->battPct);
    } else {
      snprintf(buf, sizeof(buf), "%s", s->subtitle());
    }
    lv_label_set_text(lv_obj_get_child(tile, 3), buf);
    // A flat battery is the one thing here worth colouring: a sensor that dies
    // in February reads exactly like one that is simply cold.
    lv_obj_set_style_text_color(lv_obj_get_child(tile, 3),
                                (s->haveBatt && s->battPct <= 20) ? colWarn()
                                                                  : colMuted(),
                                0);
  }

  refreshDetail();

  // An unset clock is the one thing here the crew can fix, so it is the only
  // one coloured as a prompt.
  const bool needsClock = !Clock::instance().isSet();
  lv_label_set_text(g_climateStatus, sensors.logStatus());
  lv_obj_set_style_text_color(g_climateStatus,
                              needsClock ? colWarn() : colMuted(), 0);
}

// Lights / Water: nothing is wired up yet, so these pages list any
// entities that exist in the domain and otherwise say plainly that the domain
// is waiting for an integration.
void buildDomainPage(lv_obj_t* scr, Domain d) {
  lv_obj_t* p = makePage(scr);
  g_pages[(int)d] = p;

  lv_obj_t* list = lv_obj_create(p);
  lv_obj_set_pos(list, 4, 4);
  lv_obj_set_size(list, 456, kContentH - 24);
  lv_obj_set_style_bg_color(list, colBg(), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  g_entityList[(int)d] = list;

  char msg[128];
  snprintf(msg, sizeof(msg), "No %s devices yet.\n\nAdd an integration that\nregisters entities in this domain.",
           domainName(d));
  g_emptyLabel[(int)d] = makeLabel(p, msg, &lv_font_montserrat_16, colMuted());
  lv_obj_set_style_text_align(g_emptyLabel[(int)d], LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(g_emptyLabel[(int)d]);
}

// ---- refresh ----------------------------------------------------------------

void refreshHeader() {
  Alarms& al = Alarms::instance();
  const Alarm* worst = al.worstAlarm();

  if (worst) {
    lv_color_t c = (worst->severity == Severity::Critical) ? colCrit() : colWarn();
    lv_obj_set_style_bg_color(g_header, c, 0);
    lv_label_set_text(g_headerTitle, worst->text);
    lv_obj_set_style_text_color(g_headerTitle, lv_color_black(), 0);
    lv_label_set_text(g_headerStatus, al.silenced() ? "silenced" : "tap to silence");
    lv_obj_set_style_text_color(g_headerStatus, lv_color_black(), 0);
    return;
  }

  lv_obj_set_style_bg_color(g_header, colCard(), 0);
  lv_obj_set_style_text_color(g_headerTitle, colText(), 0);
  lv_obj_set_style_text_color(g_headerStatus, colMuted(), 0);
  lv_label_set_text(g_headerTitle, domainName(g_current));

  // Right side: the worst link state across every integration.
  Hub& hub = Hub::instance();
  const char* text = "no integrations";
  for (size_t i = 0; i < hub.size(); i++) {
    Integration* in = hub.at(i);
    if (in->link() != LinkState::Online) {
      text = in->statusText();
      break;
    }
    text = "connected";
  }
  lv_label_set_text(g_headerStatus, text);
}

void refreshPowerPage() {
  JbdBms& b = bms();
  Registry& reg = Registry::instance();
  char buf[40];

  auto* soc = static_cast<NumericEntity*>(reg.find("battery.soc"));
  auto* v = static_cast<NumericEntity*>(reg.find("battery.voltage"));
  auto* a = static_cast<NumericEntity*>(reg.find("battery.current"));
  auto* w = static_cast<NumericEntity*>(reg.find("battery.power"));
  auto* ah = static_cast<NumericEntity*>(reg.find("battery.remaining"));
  auto* t = static_cast<NumericEntity*>(reg.find("battery.temperature"));
  auto* cf = static_cast<BinaryEntity*>(reg.find("battery.charge_fet"));
  auto* df = static_cast<BinaryEntity*>(reg.find("battery.discharge_fet"));

  // If the BMS integration failed to start, its entities were never registered.
  if (!soc || !v || !a || !w || !ah || !t || !cf || !df) {
    lv_label_set_text(g_statusLabel, "BMS integration not available");
    return;
  }

  const bool live = b.dataValid() && !soc->stale();

  if (live) {
    lv_arc_set_value(g_socArc, (int32_t)soc->value());
    lv_label_set_text_fmt(g_socLabel, "%d%%", (int)soc->value());
    lv_obj_set_style_text_color(g_socLabel, colText(), 0);

    // Capacity comes straight off the entity; energy is derived, so it is the
    // one value here without a NumericEntity of its own. lv_label_set_text_fmt
    // goes through LVGL's vsnprintf, which has no %f unless LV_USE_FLOAT is on
    // - and that flag also retypes lv_value_precise_t - so format with snprintf.
    lv_label_set_text(g_capValue, ah->format(buf, sizeof(buf)));
    snprintf(buf, sizeof(buf), "%.0f Wh", ah->value() * CFG_PACK_NOMINAL_V);
    lv_label_set_text(g_energyValue, buf);

    // Both labels are centred on the arc, but lv_obj_align_to() resolves to
    // fixed coordinates when called and does not follow the object as its text
    // grows. Re-align now that the real strings are in place, or "100%" sits
    // where "--" used to.
    lv_obj_align_to(g_socLabel, g_socArc, LV_ALIGN_CENTER, 0, -10);

    // Colour the ring by how much is left, not by whether we are charging.
    lv_color_t ring = colAccent();
    if (soc->value() < 20.0f) {
      ring = colCrit();
    } else if (soc->value() < 40.0f) {
      ring = colWarn();
    }
    lv_obj_set_style_arc_color(g_socArc, ring, LV_PART_INDICATOR);

    lv_label_set_text(g_tileValue[0], v->format(buf, sizeof(buf)));
    lv_label_set_text(g_tileValue[1], a->format(buf, sizeof(buf)));
    lv_label_set_text(g_tileValue[2], w->format(buf, sizeof(buf)));
    lv_label_set_text(g_tileValue[3], t->format(buf, sizeof(buf)));
    lv_obj_set_style_text_color(g_tileValue[1],
                                a->value() > 0.05f ? colCharge() : colText(), 0);
  } else {
    lv_arc_set_value(g_socArc, 0);
    lv_label_set_text(g_socLabel, "--");
    lv_obj_set_style_text_color(g_socLabel, colMuted(), 0);
    for (int i = 0; i < 4; i++) lv_label_set_text(g_tileValue[i], "--");
    lv_label_set_text(g_capValue, "--");
    lv_label_set_text(g_energyValue, "--");
  }

  // Cells
  const CellData& cells = b.cells();
  const float lo = b.cellUnderV();
  const float hi = b.cellOverV();

  // The block is a fixed width, so bar width follows the pack: a 4S pack gets
  // fat bars, a 16S pack thin ones. Only recompute when the count changes -
  // this runs at 4 Hz and moving 32 objects every time would be wasteful.
  static uint8_t laidOutFor = 0;
  const uint8_t n = live ? cells.count : 0;
  if (n != laidOutFor && n > 0) {
    laidOutFor = n;
    const int gap = (n > 8) ? 2 : 3;
    int bw = (kCellInnerW - (n - 1) * gap) / n;
    if (bw < 3) bw = 3;
    if (bw > 30) bw = 30;
    const int span = n * bw + (n - 1) * gap;
    const int x0 = (kCellInnerW - span) / 2;  // centre the group in the block
    for (uint8_t i = 0; i < n; i++) {
      const int x = x0 + i * (bw + gap);
      lv_obj_set_size(g_cellBar[i], bw, kCellBarH);
      lv_obj_set_pos(g_cellBar[i], x, kCellBarY);
      lv_obj_set_pos(g_cellLabel[i], x, kCellLabelY);
      // Past about eight cells there is no room for a legible index.
      lv_label_set_text(g_cellLabel[i], "");
    }
  }
  const bool showCellNums = (n > 0 && n <= 8);
  for (int i = 0; i < kMaxCellBars; i++) {
    const bool used = live && i < cells.count && cells.volts[i] > 0.0f;
    if (!used) {
      lv_obj_add_flag(g_cellBar[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_cellLabel[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(g_cellBar[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g_cellLabel[i], LV_OBJ_FLAG_HIDDEN);

    float frac = (cells.volts[i] - lo) / ((hi - lo) > 0.001f ? (hi - lo) : 1.0f);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    lv_bar_set_value(g_cellBar[i], (int32_t)(frac * 1000.0f), LV_ANIM_OFF);

    lv_color_t c = colAccent();
    if (cells.volts[i] >= hi || cells.volts[i] <= lo) {
      c = colCrit();
    } else if (cells.balancing[i]) {
      c = colCharge();
    }
    lv_obj_set_style_bg_color(g_cellBar[i], c, LV_PART_INDICATOR);
    if (showCellNums) lv_label_set_text_fmt(g_cellLabel[i], "%d", i + 1);
  }

  if (live && cells.count > 0) {
    // The block is 146 px wide, so the old "min x max y spread z" line no
    // longer fits. Spread is the number worth watching; show it in millivolts
    // beside the block's title and leave min/max to the cell bars themselves.
    snprintf(buf, sizeof(buf), "%d mV", (int)(cells.delta * 1000.0f + 0.5f));
    lv_label_set_text(g_cellSummary, buf);
  } else {
    lv_label_set_text(g_cellSummary, "");
  }

  lv_label_set_text(g_statusLabel, b.statusLine());
  lv_obj_set_style_text_color(g_statusLabel,
                              Alarms::instance().any() ? colCrit() : colText(), 0);
  if (live) {
    lv_label_set_text_fmt(g_fetLabel, "CHG %s   DSG %s", cf->value() ? "on" : "off",
                          df->value() ? "on" : "off");
  } else {
    lv_label_set_text(g_fetLabel, "");
  }
}

void refreshDomainPage(Domain d) {
  lv_obj_t* list = g_entityList[(int)d];
  Registry& reg = Registry::instance();
  std::vector<Entity*> items = reg.inDomain(d);

  const bool empty = items.empty();
  if (empty) {
    lv_obj_remove_flag(g_emptyLabel[(int)d], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(g_emptyLabel[(int)d], LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_HIDDEN);

  // Rows are created once, then only their text changes.
  while (lv_obj_get_child_count(list) < items.size()) {
    lv_obj_t* row = makeCard(list, 0, 0, 456, 56);
    lv_obj_set_style_margin_bottom(row, 6, 0);
    lv_obj_t* name = makeLabel(row, "", &lv_font_montserrat_16, colText());
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t* value = makeLabel(row, "", &lv_font_montserrat_20, colText());
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);
  }

  char buf[40];
  for (size_t i = 0; i < items.size(); i++) {
    lv_obj_t* row = lv_obj_get_child(list, i);
    lv_obj_t* name = lv_obj_get_child(row, 0);
    lv_obj_t* value = lv_obj_get_child(row, 1);
    lv_label_set_text(name, items[i]->name());
    lv_label_set_text(value, items[i]->format(buf, sizeof(buf)));
    lv_obj_set_style_text_color(value, items[i]->stale() ? colMuted() : colText(), 0);
  }
}

}  // namespace

void showDomain(Domain d) {
  g_current = d;
  for (int i = 0; i < (int)Domain::COUNT; i++) {
    if (!g_pages[i]) continue;
    if (i == (int)d) {
      lv_obj_remove_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  for (int i = 0; i < 4; i++) {
    const bool active = kNavDomains[i] == d;
    lv_obj_set_style_bg_color(g_navButtons[i], active ? colAccent() : colBg(), 0);
    lv_obj_t* lab = lv_obj_get_child(g_navButtons[i], 0);
    lv_obj_set_style_text_color(lab, active ? lv_color_black() : colMuted(), 0);
  }
  refreshHeader();
}

void begin() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, colBg(), 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildHeader(scr);
  buildPowerPage(scr);
  buildDomainPage(scr, Domain::Lighting);
  buildDomainPage(scr, Domain::Water);
  buildClimatePage(scr);
  buildNav(scr);
  buildDetail(scr);    // above the pages and the nav bar
  buildSettings(scr);  // and above that, so the gear still wins
  buildEntry(scr);     // and the shared keyboard above even that
  // The clock setter is deliberately absent: it is built when the gear's Set
  // button is tapped and freed when it closes. See closeClockSet().

  showDomain(Domain::Power);
}

void tick() {
  if (millis() - g_lastTickMs < kTickMs) return;
  g_lastTickMs = millis();

  refreshHeader();
  switch (g_current) {
    case Domain::Power:
      refreshPowerPage();
      break;
    case Domain::Climate:
      refreshClimatePage();
      break;
    default:
      refreshDomainPage(g_current);
      break;
  }
}

}  // namespace ui
}  // namespace cc
