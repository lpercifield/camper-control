#include "integrations/lighting.h"

#include "config.h"
#include "core/light_presets.h"
#include "core/registry.h"

namespace cc {
namespace {

// Each fixture holds a connection, and the BMS holds one. If this ever fails,
// the fix is CONFIG_BT_NIMBLE_MAX_CONNECTIONS in platformio.ini and the RAM
// note beside CFG_LIGHTING_MAX_FIXTURES - not a smaller comment.
static_assert(CFG_LIGHTING_MAX_FIXTURES + 1 <= CONFIG_BT_NIMBLE_MAX_CONNECTIONS,
              "every fixture holds a BLE connection and the BMS holds one; "
              "raise CONFIG_BT_NIMBLE_MAX_CONNECTIONS in platformio.ini");

// How often the task asks a fixture what it is actually doing. The answer is
// what drives confirm(), so this is also how long a change made from the
// vendor app takes to show up on our screen.
constexpr uint32_t kPollMs = 2000;
// Matches BleSerialClient: a connect attempt is allowed to fail and be
// retried, and the scanner runs in the gap rather than being held down.
constexpr uint32_t kRetryScanMs = 1200;
constexpr uint8_t kMaxConnectAttempts = 4;

// Ids and names are literals by contract: Entity holds the pointer rather
// than copying. These tables are what caps Lighting::kMaxFixtures.
const char* const kPowerIds[] = {"lighting.1", "lighting.2", "lighting.3",
                                 "lighting.4", "lighting.5", "lighting.6"};
const char* const kLevelIds[] = {
    "lighting.1_level", "lighting.2_level", "lighting.3_level",
    "lighting.4_level", "lighting.5_level", "lighting.6_level"};
// Not "Cabin light": with more than one fixture the crew needs to tell them
// apart, and a number is the only thing we can honestly claim. Renaming them
// is the same job EnvSensors does for sensors and is not done here yet.
const char* const kFixtureNames[] = {"Light 1", "Light 2", "Light 3",
                                     "Light 4", "Light 5", "Light 6"};
constexpr size_t kNameTableLen = sizeof(kPowerIds) / sizeof(kPowerIds[0]);
static_assert(CFG_LIGHTING_MAX_FIXTURES <= kNameTableLen,
              "add entity id and name literals for the extra fixtures");

uint32_t packColour(uint8_t r, uint8_t g, uint8_t b) {
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

uint32_t defaultColour() {
  const LightPreset& p = kLightPresets[kLightPresetDefault];
  return packColour(p.r, p.g, p.b);
}

}  // namespace

// ---- discovery --------------------------------------------------------------

void Lighting::Finder::onAdvertisement(const NimBLEAdvertisedDevice* device) {
  // Matched on the **advertised** UUID, which is not the GATT service - the
  // device advertises 0xFFB0 and serves 0xFFE0, and filtering on the service
  // finds nothing at all. `decisions/0022`.
  if (!device->isAdvertisingService(NimBLEUUID(kSp107eAdvertisedUuid))) return;
  owner_->bind(device->getAddress(), device->getAddress().toString().c_str());
}

void Lighting::bind(const NimBLEAddress& address, const char* mac) {
  // Already ours? Nothing to do - this runs for every advertisement a fixture
  // sends, which is several a second.
  for (size_t i = 0; i < kMaxFixtures; i++) {
    if (fixtures_[i].haveAddress.load() &&
        strcmp(fixtures_[i].mac, mac) == 0) {
      return;
    }
  }

  for (size_t i = 0; i < kMaxFixtures; i++) {
    Fixture& f = fixtures_[i];
    if (f.haveAddress.load()) continue;
    strncpy(f.mac, mac, sizeof(f.mac));
    f.mac[sizeof(f.mac) - 1] = '\0';
    f.address = address;
    f.wantColour.store(defaultColour());
    // The address must be visible before the flag that says to read it.
    __sync_synchronize();
    f.haveAddress.store(true);
    f.needsCommit.store(true);
    log_i("lighting: %s is a fixture at %s", fixtureName(i), f.mac);
    return;
  }
  // Every slot taken. Said once per advertisement would be a flood, so this
  // is deliberately quiet - the Lighting page showing four fixtures is the
  // honest signal that a fifth has nowhere to go.
}

// ---- lifecycle --------------------------------------------------------------

bool Lighting::begin() {
  Registry& reg = Registry::instance();
  Settings& st = Settings::instance();

  // Reserved once, and never grown again: the registry holds pointers to
  // these.
  power_.reserve(kMaxFixtures);
  level_.reserve(kMaxFixtures);
  for (size_t i = 0; i < kMaxFixtures; i++) {
    power_.emplace_back(kPowerIds[i], kFixtureNames[i], Domain::Lighting,
                        [this, i](bool on) { return this->requestOn(i, on); });
    level_.emplace_back(kLevelIds[i], kFixtureNames[i], Domain::Lighting, "%",
                        0);
    fixtures_[i].wantColour.store(defaultColour());
  }

  // Slots remembered from last time come back bound, so "Light 2" is the same
  // lamp as it was before the power cut and its row appears immediately -
  // offline until it answers, which is the truth rather than a blank page.
  size_t restored = 0;
  for (size_t i = 0; i < kMaxFixtures; i++) {
    if (!st.hasLightMac(i)) continue;
    Fixture& f = fixtures_[i];
    strncpy(f.mac, st.lightMac(i), sizeof(f.mac));
    f.mac[sizeof(f.mac) - 1] = '\0';
    f.address = NimBLEAddress(f.mac, BLE_ADDR_PUBLIC);
    f.haveAddress.store(true);
    f.registered = true;
    reg.add(&power_[i]);
    reg.add(&level_[i]);
    restored++;
  }
  if (restored > 0) log_i("lighting: %u fixture(s) remembered", (unsigned)restored);

  if (!NimBLEDevice::isInitialized()) {
    setLink(LinkState::Fault, "NimBLE not up");
    return false;
  }
  BleScanner::instance().addListener(&finder_);
  refreshLink();

  // Core 0, away from the Arduino loop, same as the BMS task. 4 KB rather
  // than the BMS's 8: this one holds no protocol library, and the BMS's own
  // headroom report says 8 KB was already generous. The fixture table is a
  // member, not a stack frame, so more fixtures do not deepen this.
  xTaskCreatePinnedToCore(taskEntry, "lighting", 4096, this, 1, &task_, 0);
  return task_ != nullptr;
}

void Lighting::loop() {
  // **Gated on a counter, and that is not a micro-optimisation.** The body
  // below is four atomic read-modify-writes, and running them on every pass
  // of a loop that turns over ~17,000 times a second cost **14% of the main
  // loop rate** - 39,115 loops per heartbeat down to 33,729, measured on
  // hardware 2026-10-19 before this line existed. The flush rate never moved,
  // which is why the log looked healthy and the arithmetic did not.
  //
  // Every 4096 passes is ~4 Hz, so a fixture the scanner just found waits at
  // most a quarter second to be committed. The same idiom `Settings::loop()`
  // uses, and the one `ADDING_AN_INTEGRATION.md` tells the next author to use.
  if ((++tick_ & 0x0FFF) != 0) return;

  // The one job the main loop owns: a fixture the scanner found needs its
  // address written to NVS and its entities put in the registry, and neither
  // is safe on the lighting task - flash because of `decisions/0014`, the
  // registry because the UI iterates it.
  Registry& reg = Registry::instance();
  for (size_t i = 0; i < kMaxFixtures; i++) {
    Fixture& f = fixtures_[i];
    if (!f.needsCommit.exchange(false)) continue;
    if (!f.registered) {
      reg.add(&power_[i]);
      reg.add(&level_[i]);
      f.registered = true;
    }
    Settings::instance().setLightMac(i, f.mac);
  }
}

void Lighting::taskEntry(void* self) {
  static_cast<Lighting*>(self)->taskLoop();
}

void Lighting::taskLoop() {
  for (;;) {
    service();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ---- commands from the UI ---------------------------------------------------

bool Lighting::requestOn(size_t slot, bool on) {
  if (slot >= kMaxFixtures) return false;
  // Accepted, not done. SwitchEntity only changes state when confirm() is
  // called, which happens when the controller says so.
  fixtures_[slot].wantOn.store(on);
  fixtures_[slot].dirty.store(true);
  return fixtures_[slot].connected.load();
}

void Lighting::setColour(size_t slot, uint8_t r, uint8_t g, uint8_t b) {
  if (slot >= kMaxFixtures) return;
  fixtures_[slot].wantColour.store(packColour(r, g, b));
  fixtures_[slot].dirty.store(true);
}

void Lighting::setBrightness(size_t slot, uint8_t brightness) {
  if (slot >= kMaxFixtures) return;
  fixtures_[slot].wantBrightness.store(brightness);
  fixtures_[slot].dirty.store(true);
}

// ---- what the UI reads ------------------------------------------------------

const char* Lighting::fixtureName(size_t slot) {
  return slot < kMaxFixtures ? kFixtureNames[slot] : "?";
}

bool Lighting::present(size_t slot) const {
  return slot < kMaxFixtures && fixtures_[slot].registered;
}

size_t Lighting::presentCount() const {
  size_t n = 0;
  for (size_t i = 0; i < kMaxFixtures; i++) {
    if (fixtures_[i].registered) n++;
  }
  return n;
}

bool Lighting::online(size_t slot) const {
  return slot < kMaxFixtures && fixtures_[slot].connected.load();
}

uint8_t Lighting::leds(size_t slot) const {
  return slot < kMaxFixtures ? fixtures_[slot].confirmed.leds : 0;
}

SwitchEntity* Lighting::power(size_t slot) {
  if (slot >= power_.size()) return nullptr;
  return &power_[slot];
}

uint8_t Lighting::brightness(size_t slot) const {
  return slot < kMaxFixtures ? fixtures_[slot].wantBrightness.load() : 0;
}

uint32_t Lighting::colour(size_t slot) const {
  return slot < kMaxFixtures ? fixtures_[slot].wantColour.load() : 0;
}

bool Lighting::colourIsOurs(size_t slot) const {
  return slot < kMaxFixtures && fixtures_[slot].colourIsOurs.load();
}

// ---- the task ---------------------------------------------------------------

void Lighting::service() {
  // Writes and polls first. They are cheap and every connected fixture gets
  // them every pass.
  for (size_t i = 0; i < kMaxFixtures; i++) {
    if (fixtures_[i].connected.load()) serviceLink(fixtures_[i]);
  }
  serviceConnects();
  refreshLink();
}

void Lighting::serviceConnects() {
  for (size_t n = 0; n < kMaxFixtures; n++) {
    const size_t slot = (connectCursor_ + n) % kMaxFixtures;
    Fixture& f = fixtures_[slot];
    if (!f.haveAddress.load() || f.connected.load()) continue;
    if (millis() < f.nextConnectMs) continue;

    // Whatever happens below, the next pass starts at the following slot, so
    // one unreachable fixture cannot starve the others of connect attempts.
    connectCursor_ = (slot + 1) % kMaxFixtures;

    BleScanner::instance().pause(BleScanner::kPauseLighting);
    // Stay paused and come back next pass: the scan has been asked to stop
    // and has not stopped yet.
    if (BleScanner::instance().scanning()) return;

    const bool ok = connectFixture(f, slot);
    BleScanner::instance().resume(BleScanner::kPauseLighting);
    if (!ok) {
      if (++f.connectAttempts >= kMaxConnectAttempts) {
        f.connectAttempts = 0;
        // Give up on this address and let the scanner find it again. A
        // fixture on a wall switch is off most of the time, and hammering a
        // device that is not powered costs every other listener airtime. The
        // slot keeps its MAC in NVS, so it comes back as the same light.
        f.haveAddress.store(false);
        publishOffline(f);
        log_i("lighting: %s is not answering, searching again",
              fixtureName(slot));
      }
      f.nextConnectMs = millis() + kRetryScanMs;
    }
    return;  // one connect attempt per pass, whatever the outcome
  }

  // Nobody wanted the radio this pass, so do not hold the scanner down.
  BleScanner::instance().resume(BleScanner::kPauseLighting);
}

void Lighting::serviceLink(Fixture& f) {
  if (f.dirty.exchange(false)) {
    const uint32_t c = f.wantColour.load();
    const Sp107eFrame colour = sp107eColourScaled(
        (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, f.wantBrightness.load());
    // Colour first, then the power state. Writing power last means a light
    // turned on never flashes the previous colour.
    send(f, colour);
    send(f, f.wantOn.load() ? sp107eOn() : sp107eOff());
    send(f, sp107eQuery());
    f.lastPollMs = millis();
    return;
  }

  if (millis() - f.lastPollMs >= kPollMs) {
    f.lastPollMs = millis();
    send(f, sp107eQuery());
  }
}

bool Lighting::connectFixture(Fixture& f, size_t slot) {
  if (f.client == nullptr) {
    f.client = NimBLEDevice::createClient();
    if (f.client == nullptr) {
      log_w("lighting: no client available for %s", fixtureName(slot));
      return false;
    }
    f.client->setConnectTimeout(5000);
  }

  if (!f.client->connect(f.address)) return false;

  NimBLERemoteService* svc =
      f.client->getService(NimBLEUUID(kSp107eServiceUuid));
  if (svc == nullptr) {
    log_w("lighting: no %04X service on %s", kSp107eServiceUuid, f.mac);
    disconnectFixture(f);
    return false;
  }
  f.chr = svc->getCharacteristic(NimBLEUUID(kSp107eCharUuid));
  if (f.chr == nullptr) {
    log_w("lighting: no %04X characteristic on %s", kSp107eCharUuid, f.mac);
    disconnectFixture(f);
    return false;
  }
  // Write and notify, never read - the state comes back as a notification in
  // answer to a query, not from a GATT read. `decisions/0022`.
  if (f.chr->canNotify()) {
    f.chr->subscribe(true, [this, slot](NimBLERemoteCharacteristic*, uint8_t* d,
                                        size_t len,
                                        bool) { this->onNotify(slot, d, len); });
  }

  f.connected.store(true);
  f.confirmed = Sp107eState{};
  f.colourIsOurs.store(false);
  f.dirty.store(true);  // push our desired state onto a fixture we just met
  log_i("lighting: %s connected at %s", fixtureName(slot), f.mac);
  return true;
}

void Lighting::disconnectFixture(Fixture& f) {
  if (f.client != nullptr && f.client->isConnected()) f.client->disconnect();
  f.connected.store(false);
  f.chr = nullptr;
}

bool Lighting::send(Fixture& f, const Sp107eFrame& frame) {
  if (f.chr == nullptr) return false;
  // Write with response: the characteristic offers write, not
  // write-without-response, and a no-response write is silently dropped.
  if (!f.chr->writeValue(frame.b, kSp107eFrameLen, true)) {
    log_w("lighting: write to %s failed, dropping the link", f.mac);
    disconnectFixture(f);
    publishOffline(f);
    return false;
  }
  return true;
}

void Lighting::onNotify(size_t slot, const uint8_t* data, size_t len) {
  if (slot >= kMaxFixtures) return;
  Fixture& f = fixtures_[slot];
  if (!sp107eFeed(data, len, f.confirmed)) return;

  // The entities move only here, on what the controller actually reported.
  if (f.confirmed.haveOnOff) power_[slot].confirm(f.confirmed.on);

  const uint32_t c = f.wantColour.load();
  const uint8_t bright = f.wantBrightness.load();
  f.colourIsOurs.store(sp107eColourMatches(f.confirmed, (c >> 16) & 0xFF,
                                           (c >> 8) & 0xFF, c & 0xFF, bright));
  // Reported as a percentage of what we asked for, which is the only honest
  // reading available: the device knows the scaled colour, not the brightness
  // that produced it. It is published here rather than when the slider moves,
  // so a fixture that has stopped answering lets the row go stale.
  level_[slot].set(100.0f * bright / 255.0f);
}

void Lighting::publishOffline(Fixture& f) {
  f.confirmed = Sp107eState{};
  f.colourIsOurs.store(false);
  // Deliberately not confirm(false): we do not know the light is off, only
  // that we cannot reach it. Letting the entity go stale says that honestly,
  // where a confident "OFF" would not.
}

void Lighting::refreshLink() {
  size_t known = 0;
  size_t up = 0;
  for (size_t i = 0; i < kMaxFixtures; i++) {
    if (!fixtures_[i].haveAddress.load()) continue;
    known++;
    if (fixtures_[i].connected.load()) up++;
  }

  // Literals rather than a formatted count, because statusText_ is a borrowed
  // pointer read by the UI on the main loop while this runs on the task - a
  // buffer being rewritten here could be read half-formed. The per-fixture
  // detail is on the Lighting page, which is where it belongs anyway.
  if (known == 0) {
    setLink(LinkState::Searching, "looking for a fixture");
  } else if (up == known) {
    setLink(LinkState::Online, "connected");
  } else if (up > 0) {
    setLink(LinkState::Online, "a fixture is offline");
  } else {
    setLink(LinkState::Searching, "fixtures not answering");
  }
}

Lighting& lighting() {
  static Lighting instance;
  return instance;
}

}  // namespace cc
