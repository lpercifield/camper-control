#pragma once
#include <Arduino.h>
#include <Preferences.h>

#include "config.h"

namespace cc {

// Everything the crew can change without a reflash, persisted in NVS.
//
// Threading: NVS writes touch flash, and flash writes disable the cache. All
// writing happens on the main loop. The BMS runs on its own task (see
// docs/decisions/0008) and must never write here directly - it hands an address
// over with rememberBmsMacFromTask(), and loop() commits it.
class Settings {
 public:
  static constexpr size_t kMacLen = 18;  // "aa:bb:cc:dd:ee:ff" plus NUL

  static Settings& instance();

  void begin();
  // Commits anything handed over from another task. Call from the main loop.
  void loop();

  // ---- BMS ----
  // The address of the last BMS we successfully talked to. Empty until one
  // connects. Takes precedence over CFG_BMS_MAC, so the compile-time constant
  // becomes a seed rather than a commitment.
  const char* bmsMac() const { return bmsMac_; }
  bool hasBmsMac() const { return bmsMac_[0] != '\0'; }
  void clearBmsMac();
  void rememberBmsMacFromTask(const char* mac);

  // ---- Display ----
  uint8_t brightness() const { return brightness_; }
  // persist=false while a slider is being dragged; the caller commits once on
  // release rather than writing flash on every pixel of travel.
  void setBrightness(uint8_t level, bool persist);

  // 0 means never dim. Anything else is milliseconds of inactivity.
  uint32_t screenTimeoutMs() const { return timeoutMs_; }
  void setScreenTimeoutMs(uint32_t ms);

 private:
  Settings() = default;
  void commitBrightness();

  Preferences prefs_;
  bool open_ = false;

  char bmsMac_[kMacLen] = {0};
  uint8_t brightness_ = CFG_BACKLIGHT_DEFAULT;
  uint32_t timeoutMs_ = CFG_BACKLIGHT_IDLE_MS;

  // Handover slot for the BMS task.
  volatile bool pendingMac_ = false;
  char pendingMacBuf_[kMacLen] = {0};
};

}  // namespace cc
