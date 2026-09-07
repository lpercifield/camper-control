#pragma once
#include <lvgl.h>

// A dark palette: this thing lives in a van and gets looked at after dark.
namespace cc {
namespace ui {

inline lv_color_t colBg()       { return lv_color_hex(0x0E1216); }
inline lv_color_t colCard()     { return lv_color_hex(0x1A2027); }
inline lv_color_t colCardEdge() { return lv_color_hex(0x2A333D); }
inline lv_color_t colText()     { return lv_color_hex(0xE6EDF3); }
inline lv_color_t colMuted()    { return lv_color_hex(0x8B98A5); }
inline lv_color_t colAccent()   { return lv_color_hex(0x35C46A); }
inline lv_color_t colWarn()     { return lv_color_hex(0xE8A13A); }
inline lv_color_t colCrit()     { return lv_color_hex(0xE2483D); }
inline lv_color_t colCharge()   { return lv_color_hex(0x3FA7F0); }

constexpr int kHeaderH = 44;
constexpr int kNavH = 64;
constexpr int kScreenW = 480;
constexpr int kScreenH = 480;
constexpr int kContentH = kScreenH - kHeaderH - kNavH;

}  // namespace ui
}  // namespace cc
