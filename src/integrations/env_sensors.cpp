#include "integrations/env_sensors.h"

#include <string.h>

#include "config.h"

namespace cc {

const char* EnvSensor::displayName() const {
  if (customName[0]) return customName;
  if (advName[0]) return advName;
  return mac;
}

const char* EnvSensor::subtitle() const {
  return advName[0] ? advName : mac;
}

bool EnvSensor::stale() const {
  return lastHeardMs == 0 || (millis() - lastHeardMs) > CFG_STALE_AFTER_MS;
}

// ---- the table --------------------------------------------------------------

bool EnvSensors::begin() {
  BleScanner::instance().addListener(this);
  setLink(LinkState::Searching, "listening");
  return true;
}

const EnvSensor* EnvSensors::at(size_t i) const {
  if (i >= bound_) return nullptr;
  return &slots_[i];
}

// Called only from onAdvertisement, i.e. only from the BLE host task, so the
// table has exactly one writer and needs no lock of its own.
EnvSensor* EnvSensors::findOrBind(const char* mac) {
  for (size_t i = 0; i < bound_; i++) {
    if (strcmp(slots_[i].mac, mac) == 0) return &slots_[i];
  }

  size_t idx;
  if (bound_ < kMaxEnvSensors) {
    idx = bound_;
  } else {
    // Full. Reuse whichever slot has been quiet longest - a phone under test
    // rotates its advertising address every few minutes and would otherwise
    // wedge the table shut against a real sensor.
    idx = 0;
    for (size_t i = 1; i < kMaxEnvSensors; i++) {
      if (slots_[i].lastHeardMs < slots_[idx].lastHeardMs) idx = i;
    }
    log_i("sensor table full, reusing slot %u (%s)", (unsigned)idx,
          slots_[idx].mac);
  }

  EnvSensor& s = slots_[idx];
  s = EnvSensor{};
  strncpy(s.mac, mac, sizeof(s.mac) - 1);
  s.bound = true;
  s.needsNameLoad = true;
  __sync_synchronize();
  if (idx == bound_) bound_ = idx + 1;
  return &s;
}

// Bring-up aid: dump the raw bytes of any SwitchBot frame. The byte offsets in
// core/switchbot.* come from reverse-engineered documentation whose two
// published versions disagree, so the only way to know they are right on a real
// device is to look. Set to 0 once a sensor has been confirmed.
#define SWITCHBOT_TRACE 0

#if SWITCHBOT_TRACE
namespace {
void traceHex(const char* what, const char* addr, const uint8_t* d, size_t n) {
  char hex[3 * 32 + 1];
  size_t o = 0;
  for (size_t i = 0; i < n && o + 3 < sizeof(hex); i++) {
    o += snprintf(hex + o, sizeof(hex) - o, "%02X ", d[i]);
  }
  hex[o] = '\0';
  log_i("SB %s %s [%u] %s", what, addr, (unsigned)n, hex);
}
}  // namespace
#endif

void EnvSensors::onAdvertisement(const NimBLEAdvertisedDevice* device) {
  // Cheapest rejection first: this runs for every advertisement from every
  // device in range, so nothing may allocate until we know a packet is ours.
  static const NimBLEUUID kBtHomeUuid(kBtHomeServiceUuid);
  static const NimBLEUUID kSwitchBotUuid(kSwitchBotServiceUuid);

  int btHomeIdx = -1;
  int switchBotIdx = -1;
  if (device->haveServiceData()) {
    const int count = static_cast<int>(device->getServiceDataCount());
    for (int i = 0; i < count; i++) {
      const NimBLEUUID u = device->getServiceDataUUID(i);
      if (u == kBtHomeUuid) btHomeIdx = i;
      else if (u == kSwitchBotUuid) switchBotIdx = i;
    }
  }

  // SwitchBot splits a reading across two advertisements: temperature and
  // humidity ride in manufacturer data, battery in service data. Seeing only
  // one of them is normal.
  bool switchBotMfr = false;
  std::string mfr;
  if (device->haveManufacturerData()) {
    mfr = device->getManufacturerData();
    switchBotMfr = mfr.size() >= 2 &&
                   (static_cast<uint8_t>(mfr[0]) |
                    (static_cast<uint8_t>(mfr[1]) << 8)) == kSwitchBotCompanyId;
  }

  if (btHomeIdx < 0 && switchBotIdx < 0 && !switchBotMfr) return;

  // Only now is the address worth building.
  std::string addr = device->getAddress().toString().c_str();
  for (auto& ch : addr) ch = tolower(ch);

  bool gotSomething = false;
  float tempC = 0.0f, humPct = 0.0f;
  uint8_t battPct = 0;
  bool haveTemp = false, haveHum = false, haveBatt = false;

  if (btHomeIdx >= 0) {
    const std::string sd = device->getServiceData(btHomeIdx);
    BtHome r;
    if (bthomeDecode(reinterpret_cast<const uint8_t*>(sd.data()), sd.size(), r)) {
      haveTemp = r.haveTemperature;
      tempC = r.temperatureC;
      haveHum = r.haveHumidity;
      humPct = r.humidityPct;
      haveBatt = r.haveBattery;
      battPct = r.batteryPct;
      gotSomething = haveTemp || haveHum || haveBatt;
    }
  }

  if (switchBotMfr) {
    const uint8_t* d = reinterpret_cast<const uint8_t*>(mfr.data());
#if SWITCHBOT_TRACE
    traceHex("mfr", addr.c_str(), d, mfr.size());
#endif
    SwitchBotReading r;
    if (switchbotDecodeManufacturer(d, mfr.size(), r)) {
      if (r.haveTemp) { tempC = r.temperatureC; haveTemp = true; }
      if (r.haveHum) { humPct = r.humidityPct; haveHum = true; }
      gotSomething = gotSomething || r.haveTemp || r.haveHum;
    }
  }

  if (switchBotIdx >= 0) {
    const std::string sd = device->getServiceData(switchBotIdx);
    const uint8_t* d = reinterpret_cast<const uint8_t*>(sd.data());
#if SWITCHBOT_TRACE
    traceHex("svc", addr.c_str(), d, sd.size());
#endif
    uint8_t batt = 0;
    if (switchbotDecodeService(d, sd.size(), batt)) {
      battPct = batt;
      haveBatt = true;
      gotSomething = true;
    }
  }

  if (!gotSomething) return;

#if SWITCHBOT_TRACE
  if (switchBotMfr || switchBotIdx >= 0) {
    char t[12] = "--", h[12] = "--", b[12] = "--";
    if (haveTemp) snprintf(t, sizeof(t), "%.1f", tempC);
    if (haveHum) snprintf(h, sizeof(h), "%.0f", humPct);
    if (haveBatt) snprintf(b, sizeof(b), "%u", (unsigned)battPct);
    log_i("SB decoded %s temp=%s C hum=%s %% batt=%s %%", addr.c_str(), t, h, b);
  }
#endif

  EnvSensor* s = findOrBind(addr.c_str());
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

  // Merge rather than replace: a SwitchBot battery frame carries no
  // temperature, and overwriting the last good reading with nothing would make
  // the row flicker between a value and a dash twice a minute.
  if (haveTemp) { s->tempC = tempC; s->haveTemp = true; }
  if (haveHum) { s->humPct = humPct; s->haveHum = true; }
  if (haveBatt) { s->battPct = battPct; s->haveBatt = true; }

  // Published last: the UI treats lastHeardMs as "this row is worth drawing",
  // so everything else has to be in place before it moves.
  __sync_synchronize();
  s->lastHeardMs = millis();
}

void EnvSensors::loop() {
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

void EnvSensors::rename(const char* mac, const char* newName) {
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

EnvSensors& envSensors() {
  static EnvSensors s;
  return s;
}

}  // namespace cc
