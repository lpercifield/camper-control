#include "ui/ui.h"

#include <lvgl.h>
#include <bms2_options.h>

#include "bsp/display.h"
#include "config.h"
#include "core/alarms.h"
#include "core/integration.h"
#include "core/registry.h"
#include "integrations/bms_jbd.h"
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

  g_headerStatus = makeLabel(g_header, "starting", &lv_font_montserrat_16, colMuted());
  lv_obj_align(g_headerStatus, LV_ALIGN_RIGHT_MID, -14, 0);
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

// Lights / Water / Climate: nothing is wired up yet, so these pages list any
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
  buildDomainPage(scr, Domain::Climate);
  buildNav(scr);

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
    default:
      refreshDomainPage(g_current);
      break;
  }
}

}  // namespace ui
}  // namespace cc
