#pragma once
#include <Arduino.h>
#include <Preferences.h>

#include "config.h"
#include "core/clock.h"
#include "core/units.h"

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

  // ---- BLE sensors ----
  // Per-sensor display name, keyed by MAC, so a sensor can be called "Fridge"
  // rather than "ATC_1a2b3c". Empty or absent means "use whatever the sensor
  // advertises". Main loop only, like every other write here.
  static constexpr size_t kSensorNameLen = 24;
  bool sensorName(const char* mac, char* out, size_t len);
  void setSensorName(const char* mac, const char* name);
  void clearSensorName(const char* mac);

  // ---- Lighting ----
  // Which fixture sits in which slot, so "Light 2" is the same lamp after a
  // power cut. Without this, slots fill in whatever order the scanner happens
  // to hear the fixtures and the names move between lamps on every boot -
  // which is worse than not naming them at all, because the crew would learn
  // the wrong one. Same role CFG_BMS_MAC's saved counterpart plays.
  //
  // Main loop only, like every other write here. `Lighting` discovers on its
  // own task and hands the address over in its `loop()`.
  const char* lightMac(size_t slot) const;
  bool hasLightMac(size_t slot) const;
  void setLightMac(size_t slot, const char* mac);
  // Re-pair everything. For a fixture that has been swapped or moved between
  // rooms, where the slot order is now a lie the crew cannot otherwise fix.
  void clearLightMacs();

  // ---- Display ----
  uint8_t brightness() const { return brightness_; }
  // persist=false while a slider is being dragged; the caller commits once on
  // release rather than writing flash on every pixel of travel.
  void setBrightness(uint8_t level, bool persist);

  // 0 means never dim. Anything else is milliseconds of inactivity.
  uint32_t screenTimeoutMs() const { return timeoutMs_; }
  void setScreenTimeoutMs(uint32_t ms);

  // ---- Units ----
  // There is deliberately no getter here. The live value lives in
  // core/units.h, which is where everything - including the laptop tests -
  // reads it; this only persists the choice and restores it at begin(). A
  // second copy is how two temperatures on one screen end up disagreeing.
  void setTempUnit(TempUnit u);

  // ---- Clock ----
  // There is no RTC on this board (docs/HARDWARE.md), so the time comes from a
  // person and this is what carries it across a reboot. The restored value is
  // marked `Clock::approximate()` until someone confirms it.
  //
  // Once an hour, not more. Every one of these is a flash write with the RGB
  // panel refreshing, which `decisions/0014` establishes is a hazard rather
  // than a safe operation - so the rate is the smallest one that still spares
  // the crew re-entering the time after every power cut. A cut costs up to an
  // hour of accuracy, which is why the restored value announces itself as
  // approximate rather than being trusted.
  static constexpr uint32_t kClockSaveMs = 3600000UL;  // 1 hour
  void setClock(Epoch e);

 private:
  Settings() = default;
  void commitBrightness();

  Preferences prefs_;
  bool open_ = false;

  char bmsMac_[kMacLen] = {0};
  char lightMac_[CFG_LIGHTING_MAX_FIXTURES][kMacLen] = {{0}};
  uint8_t brightness_ = CFG_BACKLIGHT_DEFAULT;
  uint32_t timeoutMs_ = CFG_BACKLIGHT_IDLE_MS;

  void saveClock();

  uint32_t lastClockSaveMs_ = 0;
  // loop() runs ~22,000 times a second and millis() is a 64-bit division, so
  // the hourly check is gated on a counter rather than paying for the clock
  // read every pass. Same reason Hub takes one timestamp per boundary - see
  // ARCHITECTURE.md, where adding a second integration cost 12% of loop rate
  // almost entirely in instrumentation.
  uint16_t tick_ = 0;

  // Handover slot for the BMS task.
  volatile bool pendingMac_ = false;
  char pendingMacBuf_[kMacLen] = {0};
};

}  // namespace cc
