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

// Seed values only. Both are configurable at runtime from the Settings page and
// persisted in NVS, so these are what a factory-fresh board starts with.
#define CFG_BACKLIGHT_DEFAULT 200  // 0-255
#define CFG_BACKLIGHT_IDLE_MS 120000UL  // 0 would mean never sleep

// ---- Battery / BMS ----------------------------------------------------------
// A seed, not a commitment. The address of the last BMS that connected is saved
// in NVS and takes precedence over this, so in practice you never need to set
// it - let the firmware find the battery once and it will remember. Forgetting
// it on the Settings page returns to attaching to the first JBD BMS heard.
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

// ---- Climate ----------------------------------------------------------------
// Which unit temperatures are *displayed* in. A seed only: the Settings page
// changes it at runtime and NVS remembers it. Readings are always stored in
// Celsius and converted on the way to the screen - see src/core/units.h.
#define CFG_TEMP_UNIT_FAHRENHEIT 1

// Trend history kept per sensor for the Climate page's graph. Each sample is
// two int16s, so the static cost is SAMPLES x 4 bytes x 8 sensor slots - 2.8 KB
// at these values, covering an hour and a half at one-minute resolution.
// Internal RAM is the tight resource on this board; see docs/ARCHITECTURE.md
// before making the window much longer.
// 144 samples ten minutes apart is 24 hours, which is what the detail page
// graphs. The log file on flash keeps the full per-minute record; this ring is
// only what the chart can actually draw - 144 points across ~350 pixels.
#define CFG_HISTORY_SAMPLES 144
#define CFG_HISTORY_INTERVAL_MS 600000UL

// ---- Logging ----------------------------------------------------------------
// Sensor readings are written to the internal flash filesystem, one file per
// local day. NOT the microSD card: that slot belongs to the RP2040 and the
// ESP32-S3 has no path to it - see docs/HARDWARE.md.
//
// **OFF, and it must stay off on this board.** Writing or reading the flash
// filesystem while the RGB panel is refreshing panics the ESP32-S3 within
// seconds - reliably, not as a rare race. The panel's VSYNC interrupt handler
// is IRAM-safe but calls a GDMA control function that is not, so it runs with
// the cache disabled during any flash operation. Measured and decoded
// 2026-09-17; see docs/decisions/0014 and docs/TROUBLESHOOTING.md.
//
// The code behind this flag works and is covered by `pio test -e native`. It is
// kept because the same format and rotation serve the RP2040 route, which is
// the only way to durable storage on this board - the microSD slot belongs to
// that chip too. Turning this on without fixing the cache hazard boot-loops the
// device.
#define CFG_LOG_TO_FLASH 0

// One row per sensor per minute, ~26 bytes. Eight sensors is ~300 KB/day
// against a 1.5 MB partition, so two weeks only fits at a handful of sensors -
// rotation drops the oldest day when either limit is hit, whichever comes
// first.
// Rows are written once per wall-clock minute, gated on the clock's own minute
// rather than on elapsed time, so they stay aligned to the wall and a duplicate
// is impossible. That cadence is not tunable and there is deliberately no knob
// pretending otherwise.
#define CFG_LOG_KEEP_DAYS 14
// Stop writing and rotate harder below this much free space, so the filesystem
// never fills completely - LittleFS needs room to garbage-collect.
#define CFG_LOG_MIN_FREE_BYTES 65536UL

// ---- Phone broadcast --------------------------------------------------------
// The van advertises its own state as a BTHome v2 service-data frame, so a
// phone can read the pack without joining anything. Wi-Fi cannot do this job:
// a phone holds one Wi-Fi association and wireless CarPlay / Android Auto is
// already using it, so a SoftAP means the van's readings or the car's screen.
// See docs/decisions/0016 and docs/ICD.md.
//
// Advertising only - **never connectable**. That is what keeps this off the
// three-connection ceiling the BMS and the lighting plan are already dividing
// up, and it means the van has no inbound BLE surface at all.
#define CFG_BLE_BROADCAST 1

// The advertised name is **not a knob** and is deliberately not defined here.
// It carries the protocol version the app checks against, so it belongs to the
// interface rather than to local taste: `CC_ICD_NAME` in `include/icd.h`,
// which builds it from `CC_ICD_VERSION`. See `docs/ICD.md`.

// How often the payload is rebuilt from the registry. This is only how fresh
// the numbers are - it is *not* a performance knob. Measured 2026-10-18:
// refreshing every 10 s instead of every 2 s left the loop rate identical
// (20,856 against 20,849 loops/sec), so the radio's ~9% is the price of
// advertising at all, exactly as Wi-Fi's cost turned out to be the price of
// the radio existing. Pick this for freshness and nothing else.
#define CFG_BLE_BROADCAST_REFRESH_MS 2000UL

// Advertising interval in milliseconds, converted to BLE's 0.625 ms units at
// the call. Slower is cheaper on a radio that is also scanning continuously and
// holding the BMS connection.
#define CFG_BLE_BROADCAST_INTERVAL_MS 1000UL

// ---- Housekeeping -----------------------------------------------------------
#define CFG_STALE_AFTER_MS 15000UL  // a value older than this reads as stale
