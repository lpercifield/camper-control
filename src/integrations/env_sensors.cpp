#include "integrations/env_sensors.h"

#include <string.h>

#include "bsp/storage.h"
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

const History* EnvSensors::historyAt(size_t i) const {
  if (i >= bound_) return nullptr;
  return &history_[i];
}

const DayExtremes* EnvSensors::extremesAt(size_t i) const {
  if (i >= bound_) return nullptr;
  return &extremes_[i];
}

const char* EnvSensors::logStatus() const { return logStatus_; }

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
  // The same flag is how the main loop learns a slot has just been bound,
  // which is the moment the trend behind it stopped being about that sensor.
  for (size_t i = 0; i < bound_; i++) {
    if (!slots_[i].needsNameLoad) continue;
    slots_[i].needsNameLoad = false;
    history_[i].clear();
    extremes_[i].reset(Clock::instance().dayIndex());
    loggedKey_[i][0] = '\0';
    Settings::instance().sensorName(slots_[i].mac, slots_[i].customName,
                                    sizeof(slots_[i].customName));
    log_i("sensor %s: adv='%s' name='%s'", slots_[i].mac, slots_[i].advName,
          slots_[i].customName[0] ? slots_[i].customName : "(none)");
  }

  // loop() runs upwards of 20,000 times a second, so the link check is gated
  // on a counter rather than paying for millis() every pass. See
  // ADDING_AN_INTEGRATION.md.
  if ((++tick_ & 0x0FFF) != 0) return;

  // Sampling for the graph rides the same gate - History::record() decides
  // whether enough time has passed, so this costs one millis() and a compare
  // per sensor a few times a second. A sensor that has gone quiet still gets
  // its samples: a flat line through a gap would be a lie, so what goes in is
  // the gap itself.
  const uint32_t now = millis();
  for (size_t i = 0; i < bound_; i++) {
    const EnvSensor& s = slots_[i];
    const bool fresh = !s.stale();
    history_[i].record(now, fresh && s.haveTemp, s.tempC, fresh && s.haveHum,
                       s.humPct);
  }

  serviceLog(now);

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

// ---- the day log ------------------------------------------------------------
// Everything below writes flash and therefore runs on the main loop only; see
// bsp/storage.h and decisions/0004 for why that matters on this board.

void EnvSensors::serviceLog(uint32_t nowMs) {
  Clock& clk = Clock::instance();

  // The daily high and low need a clock and nothing else. Without one there is
  // no midnight to reset at, so "today" has no meaning - worth saying on the
  // Climate page rather than showing a dash that looks like a broken sensor.
  if (!clk.isSet()) {
    logStatus_ = "set the clock for daily highs";
    return;
  }

  const uint32_t day = clk.dayIndex();
  const uint16_t minute = (uint16_t)clk.minuteOfDay();

  if (!dayStarted_ || day != logDay_) startDay(day);

  // One sample per sensor per minute, gated on the clock's own minute rather
  // than on elapsed milliseconds: that keeps rows aligned to the wall clock and
  // makes a duplicate impossible even if this is called twice within a minute.
  if (minute == lastLoggedMinute_) {
#if CFG_LOG_TO_FLASH
    // Flushing is the part that actually reaches flash, so it is deliberately
    // rarer than appending: LittleFS buffers the rows until a block fills.
    if (logOpen_ && (nowMs - lastFlushMs_) >= 300000UL) {
      lastFlushMs_ = nowMs;
      bsp::logFlush();
    }
#else
    (void)nowMs;
#endif
    return;
  }
  lastLoggedMinute_ = minute;

  for (size_t i = 0; i < bound_; i++) observeMinute(i, minute);
}

// The half of the old writeRow() that costs nothing and cannot crash: fold this
// minute's reading into today's high and low. Kept separate from the file
// because the file is off on this board (config.h, CFG_LOG_TO_FLASH) and the
// high and low are the part the crew actually reads.
void EnvSensors::observeMinute(size_t slot, uint16_t minute) {
  const EnvSensor& s = slots_[slot];

  // A stale sensor contributes nothing, rather than its last reading. Same
  // rule as the graph: an outage is a gap, not a flat line.
  const bool fresh = !s.stale();
  const bool haveT = fresh && s.haveTemp;
  const bool haveH = fresh && s.haveHum;

  if (extremes_[slot].day != logDay_) extremes_[slot].reset(logDay_);
  extremes_[slot].observe(haveT, s.tempC, haveH, s.humPct);

#if CFG_LOG_TO_FLASH
  writeRow(slot, minute, haveT, haveH);
#else
  (void)minute;
#endif
}

void EnvSensors::writeRow(size_t slot, uint16_t minute, bool haveT, bool haveH) {
  if (!logOpen_) return;
  const EnvSensor& s = slots_[slot];

  LogRecord r;
  r.minuteOfDay = minute;
  if (!sensorKey(s.mac, r.key, sizeof(r.key))) return;

  r.tempCx10 = haveT
                   ? (int16_t)(s.tempC * 10.0f + (s.tempC >= 0 ? 0.5f : -0.5f))
                   : History::kNoValue;
  r.humPct = haveH ? (int16_t)(s.humPct + 0.5f) : History::kNoValue;
  r.battPct = s.haveBatt ? (int16_t)s.battPct : History::kNoValue;

  char line[kLogLineMax];

  // Announce the sensor the first time it appears in this file, or again if
  // the slot has since become a different sensor - the keys in the rows mean
  // nothing without it.
  if (strcmp(loggedKey_[slot], r.key) != 0) {
    const size_t n = formatSensorHeader(r.key, s.mac, s.displayName(), line,
                                        sizeof(line));
    if (n > 0 && bsp::logAppend(line, n)) {
      strncpy(loggedKey_[slot], r.key, kSensorKeyLen - 1);
      loggedKey_[slot][kSensorKeyLen - 1] = '\0';
    }
  }

  const size_t n = formatRow(r, line, sizeof(line));
  if (n > 0) bsp::logAppend(line, n);
}

void EnvSensors::startDay(uint32_t dayIndex) {
  dayStarted_ = true;
  logDay_ = dayIndex;
  lastLoggedMinute_ = 0xFFFF;
  for (size_t i = 0; i < kMaxEnvSensors; i++) extremes_[i].reset(dayIndex);

#if !CFG_LOG_TO_FLASH
  // No file on this board: the flash filesystem cannot be touched while the
  // panel refreshes. See config.h and decisions/0014.
  logStatus_ = "daily highs from midnight";
  return;
#else
  openDay(dayIndex);
#endif
}

void EnvSensors::openDay(uint32_t dayIndex) {
  bsp::logClose();
  logOpen_ = false;

  if (!bsp::storageReady()) {
    logStatus_ = "no filesystem - not logging";
    return;
  }

  if (!bsp::ensureDir(kLogDir)) {
    logStatus_ = "cannot create /log";
    if (!logTried_) log_e("log: cannot create %s", kLogDir);
    logTried_ = true;
    return;
  }

  rotate(dayIndex);

  char path[48];
  dayFilePath(Clock::instance().now(), path, sizeof(path));
  const bool resuming = bsp::fileExists(path);

  if (!bsp::logOpen(path)) {
    logStatus_ = "cannot open the day file";
    return;
  }
  logOpen_ = true;
  logTried_ = true;

  for (size_t i = 0; i < kMaxEnvSensors; i++) loggedKey_[i][0] = '\0';

  if (resuming) {
    // A reboot mid-day. Replay what is already there so the high and low are
    // the day's, not this boot's - otherwise every power cycle resets them and
    // the number means nothing.
    replayDay(path);
  } else {
    char line[kLogLineMax];
    int n = snprintf(line, sizeof(line), "# camper-control log %d\n",
                     kLogFormatVersion);
    if (n > 0) bsp::logAppend(line, (size_t)n);
    char date[12];
    Clock::formatDate(Clock::instance().now(), date, sizeof(date));
    n = snprintf(line, sizeof(line), "# date %s\n", date);
    if (n > 0) bsp::logAppend(line, (size_t)n);
  }
  bsp::logFlush();

  log_i("log: %s %s (%u bytes free)", resuming ? "resumed" : "started", path,
        (unsigned)bsp::storageFreeBytes());
  logStatus_ = "logging";
}

void EnvSensors::replayDay(const char* path) {
  size_t rows = 0;
  bsp::readLines(path, kLogLineMax, [&](const char* line) {
    LogRecord r;
    if (!parseRow(line, r)) return true;  // headers and corrupt rows: skip
    rows++;
    // Match the row back to a slot by key, never by position - see the note in
    // core/datalog.h. A key we no longer have bound is simply a sensor that has
    // not been heard yet this boot.
    for (size_t i = 0; i < bound_; i++) {
      char key[kSensorKeyLen];
      if (!sensorKey(slots_[i].mac, key, sizeof(key))) continue;
      if (strcmp(key, r.key) != 0) continue;
      extremes_[i].observeRecord(r);
      strncpy(loggedKey_[i], r.key, kSensorKeyLen - 1);
      loggedKey_[i][kSensorKeyLen - 1] = '\0';
      break;
    }
    return true;
  });
  log_i("log: replayed %u rows from today's file", (unsigned)rows);
}

void EnvSensors::rotate(uint32_t today) {
  // Two limits, and the space one is the one that actually fires: eight
  // sensors fill this partition in under a week.
  const bool tight = bsp::storageFreeBytes() < CFG_LOG_MIN_FREE_BYTES;
  uint32_t keep = CFG_LOG_KEEP_DAYS;

  for (int pass = 0; pass < 2; pass++) {
    // Collect first, delete second: deleting inside listDir() mutates the
    // directory being walked.
    char doomed[8][20];
    size_t n = 0;
    bsp::listDir(kLogDir, [&](const char* name) {
      if (n >= 8) return;
      if (!shouldRotate(name, today, keep)) return;
      strncpy(doomed[n], name, sizeof(doomed[0]) - 1);
      doomed[n][sizeof(doomed[0]) - 1] = '\0';
      n++;
    });
    for (size_t i = 0; i < n; i++) {
      char path[48];
      snprintf(path, sizeof(path), "%s/%s", kLogDir, doomed[i]);
      if (bsp::removeFile(path)) log_i("log: rotated out %s", path);
    }
    // Only take a second, harsher pass if space is what is short.
    if (!tight || bsp::storageFreeBytes() >= CFG_LOG_MIN_FREE_BYTES) break;
    keep = 2;
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
