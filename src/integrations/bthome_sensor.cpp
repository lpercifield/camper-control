#include "integrations/bthome_sensor.h"

#include "config.h"
#include "core/registry.h"

namespace cc {

BtHomeSensor::BtHomeSensor(const BtHomeSensorConfig& cfg)
    : cfg_(cfg),
      temp_(cfg.tempId, cfg.tempName, Domain::Climate, "C", 1),
      hum_(cfg.humId, cfg.humName, Domain::Climate, "%", 0),
      batt_(cfg.battId, cfg.battName, Domain::Climate, "%", 0) {
  if (cfg_.mac != nullptr) {
    for (const char* p = cfg_.mac; *p; ++p) boundMac_.push_back(tolower(*p));
  }
}

bool BtHomeSensor::begin() {
  Registry::instance().add(&temp_);
  Registry::instance().add(&hum_);
  Registry::instance().add(&batt_);

  // The scanner is started by the BLE client, which NimBLEDevice::init() lives
  // inside. Registering is safe either way - BleScanner keeps listeners across
  // its own begin() - but nothing will arrive unless something starts BLE.
  BleScanner::instance().addListener(this);

  setLink(LinkState::Searching, boundMac_.empty() ? "any BTHome" : boundMac_.c_str());
  return true;
}

void BtHomeSensor::onAdvertisement(const NimBLEAdvertisedDevice* device) {
  if (!device->haveServiceData()) return;

  // Cheapest possible rejection first. This runs on the BLE host task for
  // *every* advertisement from every device in range, so anything that
  // allocates has to wait until we know the packet is ours. An earlier version
  // built two std::strings per advertisement to compare the address before
  // even looking at the UUID, and cost the main loop 13% of its rate.
  static const NimBLEUUID kUuid(kBtHomeServiceUuid);
  const int count = static_cast<int>(device->getServiceDataCount());
  int idx = -1;
  for (int i = 0; i < count; i++) {
    if (device->getServiceDataUUID(i) == kUuid) {
      idx = i;
      break;
    }
  }
  if (idx < 0) return;

  const std::string sd = device->getServiceData(idx);
  BtHome r;
  if (!bthomeDecode(reinterpret_cast<const uint8_t*>(sd.data()), sd.size(), r)) {
    return;  // encrypted, or not v2
  }
  if (!r.haveTemperature && !r.haveHumidity && !r.haveBattery) return;

  std::string addr = device->getAddress().toString().c_str();
  for (auto& ch : addr) ch = tolower(ch);
  if (!boundMac_.empty() && addr != boundMac_) return;

  Reading next;
  next.haveTemp = r.haveTemperature;
  next.tempC = r.temperatureC;
  next.haveHum = r.haveHumidity;
  next.humPct = r.humidityPct;
  next.haveBatt = r.haveBattery;
  next.battPct = r.batteryPct;

  if (source_ != addr) {
    log_i("%s: hearing %s", cfg_.name, addr.c_str());
    source_ = addr;
  }

  // Fill the slot completely before publishing it - the main loop reads this
  // without a lock. Losing one advertisement to a race is harmless; they
  // arrive every few seconds.
  pending_ = next;
  __sync_synchronize();
  havePending_ = true;
}

void BtHomeSensor::loop() {
  if (havePending_) {
    havePending_ = false;
    __sync_synchronize();
    const Reading r = pending_;

    if (r.haveTemp) temp_.set(r.tempC);
    if (r.haveHum) hum_.set(r.humPct);
    if (r.haveBatt) batt_.set(static_cast<float>(r.battPct));

    lastHeardMs_ = millis();
    setLink(LinkState::Online, source_.c_str());

    // Bring-up evidence: the numbers actually published, not just that
    // something was heard. Only on a change - a serial write is by far the
    // slowest thing in this function, and logging every reading on a timer
    // tripped Hub's 50 ms guard at 66 ms. Belongs on the System page
    // eventually; see ROADMAP.
    const bool changed = !logged_ || r.tempC != lastTemp_ ||
                         r.humPct != lastHum_ || r.battPct != lastBatt_;
    if (changed) {
      logged_ = true;
      lastTemp_ = r.tempC;
      lastHum_ = r.humPct;
      lastBatt_ = r.battPct;
      char t[16], h[16], b[16];
      log_i("%s: temp=%s hum=%s batt=%s from %s", cfg_.name,
            r.haveTemp ? temp_.format(t, sizeof(t)) : "--",
            r.haveHum ? hum_.format(h, sizeof(h)) : "--",
            r.haveBatt ? batt_.format(b, sizeof(b)) : "--", source_.c_str());
    }
  }

  // Nothing to poll: entities carry their own update time and the UI greys
  // them out on its own. All this tracks is whether the link still counts as
  // up - and it is cosmetic, so it does not need doing on every pass.
  //
  // loop() runs upwards of 20,000 times a second, which makes even a millis()
  // call a real per-iteration tax: millis() is a 64-bit division. Checking
  // every 4096 passes is still several times a second against a 15 s timeout.
  if ((++tick_ & 0x0FFF) != 0) return;
  if (lastHeardMs_ != 0 && millis() - lastHeardMs_ > CFG_STALE_AFTER_MS) {
    setLink(LinkState::Searching, "lost");
  }
}

BtHomeSensor& indoorSensor() {
  static BtHomeSensorConfig cfg{
      "Indoor sensor",
      "climate.indoor_temp", "Indoor temp",
      "climate.indoor_hum",  "Indoor humidity",
      "climate.indoor_batt", "Sensor battery",
      CFG_BTHOME_INDOOR_MAC,
  };
  static BtHomeSensor s(cfg);
  return s;
}

}  // namespace cc
