#include "integrations/lighting.h"

#include "config.h"
#include "core/registry.h"

namespace cc {
namespace {

// How often the task asks the fixture what it is actually doing. The answer
// is what drives confirm(), so this is also how long a change made from the
// vendor app takes to show up on our screen.
constexpr uint32_t kPollMs = 2000;
// Matches BleSerialClient: a connect attempt is allowed to fail and be
// retried, and the scanner runs in the gap rather than being held down.
constexpr uint32_t kRetryScanMs = 1200;
constexpr uint8_t kMaxConnectAttempts = 4;

uint32_t packColour(uint8_t r, uint8_t g, uint8_t b) {
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

}  // namespace

// ---- discovery --------------------------------------------------------------

void Lighting::Finder::onAdvertisement(const NimBLEAdvertisedDevice* device) {
  if (owner_->haveAddress_.load()) return;
  // Matched on the **advertised** UUID, which is not the GATT service - the
  // device advertises 0xFFB0 and serves 0xFFE0, and filtering on the service
  // finds nothing at all. `decisions/0022`.
  if (!device->isAdvertisingService(NimBLEUUID(kSp107eAdvertisedUuid))) return;

  owner_->address_ = device->getAddress();
  owner_->haveAddress_.store(true);
  log_i("lighting: found a fixture at %s",
        owner_->address_.toString().c_str());
}

// ---- lifecycle --------------------------------------------------------------

bool Lighting::begin() {
  Registry& reg = Registry::instance();
  reg.add(&power_);
  reg.add(&level_);
  level_.set(100.0f * brightness_.load() / 255.0f);

  if (!NimBLEDevice::isInitialized()) {
    setLink(LinkState::Fault, "NimBLE not up");
    return false;
  }
  BleScanner::instance().addListener(&finder_);
  setLink(LinkState::Searching, "looking for a fixture");

  // Core 0, away from the Arduino loop, same as the BMS task. 4 KB rather
  // than the BMS's 8: this one holds no protocol library, and the BMS's own
  // headroom report says 8 KB was already generous.
  xTaskCreatePinnedToCore(taskEntry, "lighting", 4096, this, 1, &task_, 0);
  return task_ != nullptr;
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

bool Lighting::requestOn(bool on) {
  // Accepted, not done. SwitchEntity only changes state when confirm() is
  // called, which happens when the controller says so.
  wantOn_.store(on);
  dirty_.store(true);
  return haveAddress_.load();
}

void Lighting::setColour(uint8_t r, uint8_t g, uint8_t b) {
  wantColour_.store(packColour(r, g, b));
  dirty_.store(true);
}

void Lighting::setBrightness(uint8_t brightness) {
  brightness_.store(brightness);
  dirty_.store(true);
}

// ---- the task ---------------------------------------------------------------

void Lighting::service() {
  if (!haveAddress_.load()) return;

  if (!connected_.load()) {
    if (millis() < nextConnectMs_) {
      BleScanner::instance().resume(BleScanner::kPauseLighting);
      return;
    }
    BleScanner::instance().pause(BleScanner::kPauseLighting);
    if (BleScanner::instance().scanning()) return;

    const bool ok = connectToFixture();
    BleScanner::instance().resume(BleScanner::kPauseLighting);
    if (!ok) {
      if (++connectAttempts_ >= kMaxConnectAttempts) {
        connectAttempts_ = 0;
        // Give up on this address and let the scanner find it again. A
        // fixture on a wall switch is off most of the time, and hammering a
        // device that is not powered costs every other listener airtime.
        haveAddress_.store(false);
        setLink(LinkState::Searching, "fixture not answering");
        publishOffline();
      }
      nextConnectMs_ = millis() + kRetryScanMs;
      return;
    }
    connectAttempts_ = 0;
  }

  if (dirty_.exchange(false)) {
    const uint32_t c = wantColour_.load();
    const Sp107eFrame colour = sp107eColourScaled(
        (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, brightness_.load());
    // Colour first, then the power state. Writing power last means a light
    // turned on never flashes the previous colour.
    send(colour);
    send(wantOn_.load() ? sp107eOn() : sp107eOff());
    requestState();
    lastPollMs_ = millis();
    return;
  }

  if (millis() - lastPollMs_ >= kPollMs) {
    lastPollMs_ = millis();
    requestState();
  }
}

bool Lighting::connectToFixture() {
  if (client_ == nullptr) {
    client_ = NimBLEDevice::createClient();
    if (client_ == nullptr) return false;
    client_->setConnectTimeout(5000);
  }

  if (!client_->connect(address_)) {
    log_w("lighting: connect failed, will retry");
    return false;
  }

  NimBLERemoteService* svc =
      client_->getService(NimBLEUUID(kSp107eServiceUuid));
  if (svc == nullptr) {
    log_w("lighting: no %04X service on that device", kSp107eServiceUuid);
    disconnectFixture();
    return false;
  }
  chr_ = svc->getCharacteristic(NimBLEUUID(kSp107eCharUuid));
  if (chr_ == nullptr) {
    log_w("lighting: no %04X characteristic", kSp107eCharUuid);
    disconnectFixture();
    return false;
  }
  // Write and notify, never read - the state comes back as a notification in
  // answer to a query, not from a GATT read. `decisions/0022`.
  if (chr_->canNotify()) {
    chr_->subscribe(true,
                    [this](NimBLERemoteCharacteristic*, uint8_t* d, size_t len,
                           bool) { this->onNotify(d, len); });
  }

  connected_.store(true);
  confirmed_ = Sp107eState{};
  dirty_.store(true);  // push our desired state onto a fixture we just met
  setLink(LinkState::Online, "connected");
  log_i("lighting: connected to %s", address_.toString().c_str());
  return true;
}

void Lighting::disconnectFixture() {
  if (client_ != nullptr && client_->isConnected()) client_->disconnect();
  connected_.store(false);
  chr_ = nullptr;
}

bool Lighting::send(const Sp107eFrame& f) {
  if (chr_ == nullptr) return false;
  // Write with response: the characteristic offers write, not
  // write-without-response, and a no-response write is silently dropped.
  if (!chr_->writeValue(f.b, kSp107eFrameLen, true)) {
    log_w("lighting: write failed, dropping the link");
    disconnectFixture();
    setLink(LinkState::Fault, "write failed");
    return false;
  }
  return true;
}

void Lighting::requestState() {
  const Sp107eFrame q = sp107eQuery();
  send(q);
}

void Lighting::onNotify(const uint8_t* data, size_t len) {
  if (!sp107eFeed(data, len, confirmed_)) return;

  // The entities move only here, on what the controller actually reported.
  if (confirmed_.haveOnOff) power_.confirm(confirmed_.on);
  level_.set(100.0f * brightness_.load() / 255.0f);
}

void Lighting::publishOffline() {
  confirmed_ = Sp107eState{};
  // Deliberately not confirm(false): we do not know the light is off, only
  // that we cannot reach it. Letting the entity go stale says that honestly,
  // where a confident "OFF" would not.
}

Lighting& lighting() {
  static Lighting instance;
  return instance;
}

}  // namespace cc
