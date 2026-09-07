#pragma once
// -----------------------------------------------------------------------------
// Camper Control - user-tunable settings.
// Everything you are likely to change while commissioning the van lives here.
// -----------------------------------------------------------------------------

// ---- Display ----------------------------------------------------------------
// The ST7701 init sequence Arduino_GFX ships for this panel comes up 180 degrees
// off in the Indicator's enclosure. Set to 0 if your unit looks upside down.
#define CFG_DISPLAY_ROTATE_180 1

// Touch axes as wired in the D1. Flip these if touches land mirrored.
#define CFG_TOUCH_MIRROR_X 1
#define CFG_TOUCH_MIRROR_Y 1

#define CFG_BACKLIGHT_DEFAULT 200  // 0-255
#define CFG_BACKLIGHT_DIM     30   // level after the idle timeout
#define CFG_BACKLIGHT_IDLE_MS 120000UL

// ---- Battery / BMS ----------------------------------------------------------
// Leave empty to connect to the first JBD/Overkill BMS found. In a campground
// full of other people's batteries, pin it to your own MAC address, e.g.
//   #define CFG_BMS_MAC "a4:c1:38:11:22:33"
#define CFG_BMS_MAC ""

// Nominal pack voltage, used only to turn amp-hours into watt-hours on screen.
#define CFG_PACK_NOMINAL_V 12.9f

// Alarm thresholds are read from the BMS itself at connect time. These are the
// fallbacks used until that first read succeeds.
#define CFG_FALLBACK_PACK_OVER_V   15.0f
#define CFG_FALLBACK_PACK_UNDER_V  9.6f
#define CFG_FALLBACK_CELL_OVER_V   3.750f
#define CFG_FALLBACK_CELL_UNDER_V  2.400f
#define CFG_FALLBACK_CHG_OVER_A    120.0f
#define CFG_FALLBACK_DIS_OVER_A    120.0f

// How far below the BMS's own cutoff we raise our warning (ABYC pre-alarm).
#define CFG_ALARM_MARGIN_V 0.1f

// ---- Housekeeping -----------------------------------------------------------
#define CFG_STALE_AFTER_MS 15000UL  // a value older than this reads as stale
