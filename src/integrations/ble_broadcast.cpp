#include "integrations/ble_broadcast.h"

#include <NimBLEDevice.h>

#include "config.h"
#include "icd.h"
#include "core/bthome.h"
#include "core/registry.h"

// Dumps every frame as hex. Off: at a two-second refresh this is 30 lines a
// minute forever. The same idiom as SWITCHBOT_TRACE in env_sensors.cpp.
#define BLE_BROADCAST_TRACE 0

namespace cc {
namespace {

// A reading the frame wants, if the registry has it and it is fresh. Returns
// nullptr when the entity is missing, is the wrong kind, or has gone stale -
// and a stale reading is deliberately treated as absent, so the frame loses a
// field rather than broadcasting a number the van stopped believing. Same
// choice core/history.h makes about an outage.
const NumericEntity* freshNumeric(const char* id) {
  Entity* e = Registry::instance().find(id);
  if (e == nullptr) return nullptr;
  if (e->kind() != EntityKind::Numeric) return nullptr;
  if (e->stale()) return nullptr;
  return static_cast<const NumericEntity*>(e);
}

}  // namespace

bool BleBroadcast::begin() {
  // NimBLE is normally already up, because the BMS integration initialises it
  // in Hub order before this one. Standing it up here as well keeps this
  // working if the BMS is ever disabled or fails to start, and init() is safe
  // to ask about rather than guess at.
  if (!NimBLEDevice::isInitialized()) {
    if (!NimBLEDevice::init(CC_ICD_NAME)) {
      setLink(LinkState::Fault, "NimBLE init failed");
      return false;
    }
    log_i("phone broadcast: brought NimBLE up itself");
  }

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv == nullptr) {
    setLink(LinkState::Fault, "no advertiser");
    return false;
  }

  // Non-connectable first: NimBLE drops the flags AD for this mode, because a
  // device nobody can connect to has nothing to be discoverable *for*. That
  // buys back three of the 31 bytes for the local name.
  adv->setConnectableMode(BLE_GAP_CONN_MODE_NON);
  adv->enableScanResponse(false);

  // BLE advertising intervals are in units of 0.625 ms.
  const uint16_t units =
      static_cast<uint16_t>(CFG_BLE_BROADCAST_INTERVAL_MS * 1000UL / 625UL);
  adv->setMinInterval(units);
  adv->setMaxInterval(units);

  fields_ = publish();
  if (!adv->start()) {
    setLink(LinkState::Fault, "advertising refused");
    return false;
  }
  advertising_ = true;
  lastRefreshMs_ = millis();

  log_i("phone broadcast: ICD v%d, advertising as '%s', every %lu ms, "
        "%u readings",
        CC_ICD_VERSION, CC_ICD_NAME,
        (unsigned long)CFG_BLE_BROADCAST_INTERVAL_MS, (unsigned)fields_);
  setLink(fields_ > 0 ? LinkState::Online : LinkState::Searching,
          fields_ > 0 ? "advertising" : "no readings yet");
  return true;
}

uint8_t BleBroadcast::publish() {
  builder_.reset();
  // Ascending object ids, which is what BTHome requires and what BtHomeBuilder
  // enforces: packet id (0x00), battery (0x01), temperature (0x02), voltage
  // (0x0C).
  builder_.addPacketId(packetId_++);

  uint8_t fields = 0;

  if (const NumericEntity* soc = freshNumeric("battery.soc")) {
    // State of charge is already a percent, which is exactly BTHome's battery
    // field. The one place the two models line up without a conversion.
    if (builder_.addBattery(static_cast<uint8_t>(soc->value() + 0.5f))) {
      ++fields;
    }
  }

  if (const NumericEntity* t = freshNumeric("battery.temperature")) {
    // TemperatureEntity::value() is Celsius whatever the Climate page is
    // showing, and BTHome is defined in Celsius. That is core/units.h's rule
    // paying off: no conversion here, and none to get wrong.
    if (builder_.addTemperature(t->value())) ++fields;
  }

  if (const NumericEntity* v = freshNumeric("battery.voltage")) {
    if (builder_.addVoltage(v->value())) ++fields;
  }

  // Power before current: ids ascend, and 0x5C < 0x5D.
  //
  // Power is the one that always fits - sint32 x0.01 W against a pack that
  // peaks near 1.6 kW. Current is sint16 x0.001 A, so it holds only
  // +-32.767 A and **will** drop out on a hard charge or a big inverter load.
  // That is deliberate: `addCurrent` refuses rather than clamps, and a reader
  // that wants current at any magnitude divides power by voltage, which
  // recovers the identical number because `JbdBms` publishes power as exactly
  // voltage x current. See `decisions/0017`.
  if (const NumericEntity* w = freshNumeric("battery.power")) {
    if (builder_.addPower(w->value())) ++fields;
  }

  if (const NumericEntity* a = freshNumeric("battery.current")) {
    if (builder_.addCurrent(a->value())) ++fields;
  }

  // Still absent, and not for want of trying: **remaining amp-hours has no
  // BTHome object id at all**, and the cell minimum, maximum and spread are
  // three more voltages, which BTHome can only express as repeated 0x0C
  // objects distinguished by position - ambiguous to every generic reader, and
  // `bthomeDecode` takes the first of a repeated id by design. Putting them in
  // a volume or mass field to make them fit would be a lie about units.
  // `ROADMAP.md` design gap 2 has the routes that would carry them.

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv == nullptr) return 0;

  NimBLEAdvertisementData data;
  data.setName(CC_ICD_NAME);
  data.setServiceData(NimBLEUUID(kBtHomeServiceUuid), builder_.data(),
                      builder_.size());

  if (!adv->setAdvertisementData(data)) {
    // The 31-byte budget is the likely cause, and it is worth saying out loud
    // with the numbers rather than failing quietly.
    log_e("phone broadcast: controller refused %u bytes of AD (payload %u)",
          (unsigned)data.getPayload().size(), (unsigned)builder_.size());
    return 0;
  }

  if (builder_.overflowed()) {
    log_w("phone broadcast: payload full, readings dropped");
  }

  ++frames_;

#if BLE_BROADCAST_TRACE
  char hex[3 * BtHomeBuilder::kMaxPayload + 1] = {0};
  for (size_t i = 0; i < builder_.size(); ++i) {
    snprintf(&hex[i * 3], 4, "%02X ", builder_.data()[i]);
  }
  log_i("phone broadcast: ad=%u payload=%u [%s]",
        (unsigned)data.getPayload().size(), (unsigned)builder_.size(), hex);
#endif

  return fields;
}

void BleBroadcast::loop() {
  if (!advertising_) return;
  const uint32_t now = millis();
  if (now - lastRefreshMs_ < CFG_BLE_BROADCAST_REFRESH_MS) return;
  lastRefreshMs_ = now;

  const uint8_t before = fields_;
  fields_ = publish();

  // Log only when the shape of the frame changes, so a BMS going offline or
  // coming back is visible in the log without 30 lines a minute when nothing
  // is happening.
  if (fields_ != before) {
    log_i("phone broadcast: %u readings in frame (was %u), frames=%lu",
          (unsigned)fields_, (unsigned)before, (unsigned long)frames_);
    setLink(fields_ > 0 ? LinkState::Online : LinkState::Searching,
            fields_ > 0 ? "advertising" : "no fresh readings");
  }
}

BleBroadcast& bleBroadcast() {
  static BleBroadcast instance;
  return instance;
}

}  // namespace cc
