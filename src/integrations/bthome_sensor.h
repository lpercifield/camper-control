#pragma once
#include <BleScanner.h>

#include <string>

#include "core/bthome.h"
#include "core/entity.h"
#include "core/integration.h"

namespace cc {

// A BTHome v2 temperature and humidity sensor, heard rather than connected to.
//
// It never opens a connection - the BMS holds the only slot (`decisions/0003`)
// - so this is a listener on the shared scanner and nothing more. Two of these
// are expected eventually, indoor and outdoor, which is why the entity ids come
// from the config rather than being baked in.
struct BtHomeSensorConfig {
  const char* name;      // integration name, e.g. "Indoor sensor"
  const char* tempId;    // "climate.indoor_temp"
  const char* tempName;  // "Indoor temp"
  const char* humId;
  const char* humName;
  const char* battId;
  const char* battName;
  // Lowercase MAC to pin to. Empty means **bring-up mode**: take any BTHome
  // advertiser at all. That is what makes it possible to test against a phone,
  // whose advertising address rotates and so cannot be pinned. Set a real
  // address once the sensor is a sensor.
  const char* mac;
};

class BtHomeSensor : public Integration, public BleAdvertisementListener {
 public:
  explicit BtHomeSensor(const BtHomeSensorConfig& cfg);

  const char* name() const override { return cfg_.name; }
  bool begin() override;
  void loop() override;

  void onAdvertisement(const NimBLEAdvertisedDevice* device) override;

  // Address we are actually listening to, or "" if nothing has been heard.
  const char* sourceAddress() const { return source_.c_str(); }

 private:
  // Written on the NimBLE host task, read on the main loop. Kept deliberately
  // small and published by loop(), so entity writes stay on the main loop the
  // way the rest of the system expects (`ARCHITECTURE.md`).
  struct Reading {
    float tempC = 0.0f;
    float humPct = 0.0f;
    uint8_t battPct = 0;
    bool haveTemp = false;
    bool haveHum = false;
    bool haveBatt = false;
  };

  BtHomeSensorConfig cfg_;
  Reading pending_;
  volatile bool havePending_ = false;
  std::string source_;
  std::string boundMac_;
  uint32_t lastHeardMs_ = 0;
  uint32_t tick_ = 0;
  bool logged_ = false;
  float lastTemp_ = 0.0f;
  float lastHum_ = 0.0f;
  uint8_t lastBatt_ = 0;

  NumericEntity temp_;
  NumericEntity hum_;
  NumericEntity batt_;
};

// The one sensor wired up today. Indoor, unpinned, so it will attach to
// whatever BTHome advertiser it hears - including a phone.
BtHomeSensor& indoorSensor();

}  // namespace cc
