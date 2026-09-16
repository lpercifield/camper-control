#pragma once
#include <BleScanner.h>

#include "core/bthome.h"
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
  // Rename by address. Persists. Main loop only.
  void rename(const char* mac, const char* newName);

 private:
  EnvSensor* findOrBind(const char* mac);

  EnvSensor slots_[kMaxEnvSensors];
  size_t bound_ = 0;
  uint32_t tick_ = 0;
};

EnvSensors& envSensors();

}  // namespace cc
