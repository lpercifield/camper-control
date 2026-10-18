#pragma once
#include <BleScanner.h>

#include "core/bthome.h"
#include "core/clock.h"
#include "core/datalog.h"
#include "core/history.h"
#include "core/switchbot.h"
#include "core/integration.h"
#include "core/settings.h"

namespace cc {

// A fixed table, no allocation - the same idiom as the alarm table. Eight is
// more sensors than a van has rooms, and the least recently heard slot is
// reused when a ninth turns up.
constexpr size_t kMaxEnvSensors = 8;

// One discovered BTHome broadcaster. Written on the NimBLE host task, read by
// the UI on the main loop.
struct EnvSensor {
  char mac[18] = {0};                            // "aa:bb:cc:dd:ee:ff"
  char advName[Settings::kSensorNameLen] = {0};  // whatever it advertises
  char customName[Settings::kSensorNameLen] = {0};

  float tempC = 0.0f;
  float humPct = 0.0f;
  uint8_t battPct = 0;
  bool haveTemp = false;
  bool haveHum = false;
  bool haveBatt = false;

  uint32_t lastHeardMs = 0;
  bool bound = false;
  // Set when a slot binds; the main loop reads the stored name out of NVS,
  // because flash must not be touched from the BLE task (`decisions/0009`).
  volatile bool needsNameLoad = false;

  // What the Climate page puts on the top line: the crew's name if they gave
  // one, else whatever the sensor calls itself, else its address.
  const char* displayName() const;
  // The bottom line: the advertised name, or the address when it has none.
  const char* subtitle() const;
  bool stale() const;
};

// Every broadcasting environment sensor in earshot: BTHome v2 today, plus the
// SwitchBot W3400010, whose format is its own. It never connects - the BMS holds the
// only slot - so this is a listener on the shared scanner and nothing more.
//
// It publishes no entities. Like the per-cell data on the Power page, this is
// an array of like things that the entity model deliberately does not carry, so
// the Climate page asks this integration directly. See `ARCHITECTURE.md`.
class EnvSensors : public Integration, public BleAdvertisementListener {
 public:
  const char* name() const override { return "BTHome sensors"; }
  bool begin() override;
  void loop() override;

  void onAdvertisement(const NimBLEAdvertisedDevice* device) override;

  size_t count() const { return bound_; }
  const EnvSensor* at(size_t i) const;
  // The trend behind slot i, for the Climate page's graph. Never null for a
  // bound slot.
  const History* historyAt(size_t i) const;
  // Today's high and low for slot i, from the per-minute log stream rather
  // than from the ten-minute graph ring. Never null for a bound slot.
  const DayExtremes* extremesAt(size_t i) const;

  // One line for the Climate page saying what the daily highs are doing, and
  // why, when the answer is "nothing" - an unset clock means there is no
  // midnight to reset at, which otherwise looks like a broken sensor.
  const char* logStatus() const;
  // Rename by address. Persists. Main loop only.
  void rename(const char* mac, const char* newName);

 private:
  EnvSensor* findOrBind(const char* mac);

  // ---- the day log ----
  // All of this runs on the main loop, because it writes flash. See
  // bsp/storage.h.
  void serviceLog(uint32_t nowMs);
  // Folds one minute's reading into today's high and low. Always runs; costs
  // nothing and touches no hardware.
  void observeMinute(size_t slot, uint16_t minute);
  // Rolls over at local midnight. Resets the extremes, and opens the day's
  // file when CFG_LOG_TO_FLASH is on - which on this board it is not.
  void startDay(uint32_t dayIndex);
  // Closes yesterday, rotates, opens today, and rebuilds the day's high and
  // low by replaying the file if we are resuming a day already in progress.
  void openDay(uint32_t dayIndex);
  void rotate(uint32_t today);
  void replayDay(const char* path);
  void writeRow(size_t slot, uint16_t minute, bool haveTemp, bool haveHum);

  EnvSensor slots_[kMaxEnvSensors];
  // Deliberately outside EnvSensor. The slots are written by the BLE task and
  // a rebind wipes one wholesale; the history is sampled and read entirely on
  // the main loop, so keeping it here gives it exactly one context. loop()
  // clears the entry when it sees a slot has just been bound.
  History history_[kMaxEnvSensors];
  // Today's extremes, alongside the history and for the same reason: main loop
  // only, and cleared when a slot becomes a different sensor.
  DayExtremes extremes_[kMaxEnvSensors];
  // The key each slot's header has been written under in the open file. Empty
  // means "not announced yet"; a mismatch means the slot was reused mid-day and
  // needs a new header line.
  char loggedKey_[kMaxEnvSensors][kSensorKeyLen] = {{0}};

  size_t bound_ = 0;
  uint32_t tick_ = 0;

  bool dayStarted_ = false;
  bool logOpen_ = false;
  bool logTried_ = false;
  uint32_t logDay_ = 0;
  uint16_t lastLoggedMinute_ = 0xFFFF;
  uint32_t lastFlushMs_ = 0;
  const char* logStatus_ = "set the clock for daily highs";
};

EnvSensors& envSensors();

}  // namespace cc
