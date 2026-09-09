#include "integrations/bthome_sensor.h"

#include <string.h>

#include "config.h"

namespace cc {

const char* BtHomeSensor::displayName() const {
  if (customName[0]) return customName;
  if (advName[0]) return advName;
  return mac;
}

const char* BtHomeSensor::subtitle() const {
  return advName[0] ? advName : mac;
}

bool BtHomeSensor::stale() const {
  return lastHeardMs == 0 || (millis() - lastHeardMs) > CFG_STALE_AFTER_MS;
}

// ---- the table --------------------------------------------------------------

bool BtHomeSensors::begin() {
  BleScanner::instance().addListener(this);
  setLink(LinkState::Searching, "listening");
  return true;
}

const BtHomeSensor* BtHomeSensors::at(size_t i) const {
  if (i >= bound_) return nullptr;
  return &slots_[i];
}

// Called only from onAdvertisement, i.e. only from the BLE host task, so the
// table has exactly one writer and needs no lock of its own.
BtHomeSensor* BtHomeSensors::findOrBind(const char* mac) {
  for (size_t i = 0; i < bound_; i++) {
    if (strcmp(slots_[i].mac, mac) == 0) return &slots_[i];
  }

  size_t idx;
  if (bound_ < kMaxBtHomeSensors) {
    idx = bound_;
  } else {
    // Full. Reuse whichever slot has been quiet longest - a phone under test
    // rotates its advertising address every few minutes and would otherwise
    // wedge the table shut against a real sensor.
    idx = 0;
    for (size_t i = 1; i < kMaxBtHomeSensors; i++) {
      if (slots_[i].lastHeardMs < slots_[idx].lastHeardMs) idx = i;
    }
    log_i("sensor table full, reusing slot %u (%s)", (unsigned)idx,
          slots_[idx].mac);
  }

  BtHomeSensor& s = slots_[idx];
  s = BtHomeSensor{};
  strncpy(s.mac, mac, sizeof(s.mac) - 1);
  s.bound = true;
  s.needsNameLoad = true;
  __sync_synchronize();
  if (idx == bound_) bound_ = idx + 1;
  return &s;
}

void BtHomeSensors::onAdvertisement(const NimBLEAdvertisedDevice* device) {
  if (!device->haveServiceData()) return;

  // Cheapest rejection first: this runs for every advertisement from every
  // device in range, so nothing may allocate until we know the packet is ours.
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

  BtHomeSensor* s = findOrBind(addr.c_str());
  if (s == nullptr) return;

  // The advertised name can arrive in a later packet than the readings, so
  // take it whenever it shows up rather than only at bind time.
  if (device->haveName()) {
    const std::string n = device->getName();
    if (!n.empty() && strncmp(s->advName, n.c_str(), sizeof(s->advName) - 1) != 0) {
      strncpy(s->advName, n.c_str(), sizeof(s->advName) - 1);
      s->advName[sizeof(s->advName) - 1] = '\0';
    }
  }

  if (r.haveTemperature) {
    s->tempC = r.temperatureC;
    s->haveTemp = true;
  }
  if (r.haveHumidity) {
    s->humPct = r.humidityPct;
    s->haveHum = true;
  }
  if (r.haveBattery) {
    s->battPct = r.batteryPct;
    s->haveBatt = true;
  }
  // Published last: the UI treats lastHeardMs as "this row is worth drawing",
  // so everything else has to be in place before it moves.
  __sync_synchronize();
  s->lastHeardMs = millis();
}

void BtHomeSensors::loop() {
  // NVS must not be touched from the BLE task, so name loading lands here.
  for (size_t i = 0; i < bound_; i++) {
    if (!slots_[i].needsNameLoad) continue;
    slots_[i].needsNameLoad = false;
    Settings::instance().sensorName(slots_[i].mac, slots_[i].customName,
                                    sizeof(slots_[i].customName));
    log_i("sensor %s: adv='%s' name='%s'", slots_[i].mac, slots_[i].advName,
          slots_[i].customName[0] ? slots_[i].customName : "(none)");
  }

  // loop() runs upwards of 20,000 times a second, so the link check is gated
  // on a counter rather than paying for millis() every pass. See
  // ADDING_AN_INTEGRATION.md.
  if ((++tick_ & 0x0FFF) != 0) return;

  size_t live = 0;
  for (size_t i = 0; i < bound_; i++) {
    if (!slots_[i].stale()) live++;
  }
  if (live > 0) {
    setLink(LinkState::Online, "listening");
  } else {
    setLink(LinkState::Searching, bound_ ? "lost" : "listening");
  }
}

void BtHomeSensors::rename(const char* mac, const char* newName) {
  if (mac == nullptr) return;
  Settings::instance().setSensorName(mac, newName);
  for (size_t i = 0; i < bound_; i++) {
    if (strcmp(slots_[i].mac, mac) != 0) continue;
    if (newName == nullptr) {
      slots_[i].customName[0] = '\0';
    } else {
      strncpy(slots_[i].customName, newName, sizeof(slots_[i].customName) - 1);
      slots_[i].customName[sizeof(slots_[i].customName) - 1] = '\0';
    }
    return;
  }
}

BtHomeSensors& btHomeSensors() {
  static BtHomeSensors s;
  return s;
}

}  // namespace cc
