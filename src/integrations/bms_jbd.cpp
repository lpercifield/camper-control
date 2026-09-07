#include "integrations/bms_jbd.h"

#include <WiFi.h>

#include "config.h"
#include "core/alarms.h"
#include "core/registry.h"

namespace cc {
namespace {
constexpr uint32_t kRefreshMs = 500;
constexpr uint32_t kParamsDelayMs = 1500;  // let the first replies land first
}  // namespace

JbdBms& bms() {
  static JbdBms instance;
  return instance;
}

bool JbdBms::begin() {
  Registry& reg = Registry::instance();
  reg.add(&soc_);
  reg.add(&voltage_);
  reg.add(&current_);
  reg.add(&power_);
  reg.add(&remaining_);
  reg.add(&temperature_);
  reg.add(&cellMin_);
  reg.add(&cellMax_);
  reg.add(&cellDelta_);
  reg.add(&chargeFet_);
  reg.add(&dischargeFet_);

  // The BLE client advertises a name derived from our own MAC, which is how the
  // old monitor identified itself. Keep that behaviour.
  char deviceName[24];
  uint8_t mac[6] = {0};
  String macStr = WiFi.macAddress();
  sscanf(macStr.c_str(), "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX", &mac[0], &mac[1],
         &mac[2], &mac[3], &mac[4], &mac[5]);
  mac[5] += 2;
  snprintf(deviceName, sizeof(deviceName), "CamperCtl-%02X%02X", mac[4], mac[5]);

  ble_.setTargetAddress(CFG_BMS_MAC);
  ble_.begin(deviceName);
  ble_.setTimeout(10);
  bleStarted_ = true;
  setLink(LinkState::Searching, "looking for BMS");
  return true;
}

void JbdBms::loop() {
  if (!bleStarted_) return;

  ble_.bleLoop();

  const bool connected = ble_.connected();

  if (connected && !bmsAttached_) {
    bms_.begin(&ble_);
    bmsAttached_ = true;
    paramsRead_ = false;
    paramStep_ = 0;
    attachedAtMs_ = millis();
    setLink(LinkState::Connecting, "reading BMS");
  } else if (!connected && bmsAttached_) {
    bms_.end();
    bmsAttached_ = false;
    paramsRead_ = false;
    paramStep_ = 0;
    cells_.count = 0;
    publishOffline();
    setLink(LinkState::Searching, "BMS out of range");
  }

  if (!bmsAttached_) return;

  bms_.main_task(true);

  if (!paramsRead_ && (millis() - attachedAtMs_) > kParamsDelayMs) {
    readNextProtectionParam();
  }

  if (millis() - lastRefreshMs_ >= kRefreshMs) {
    lastRefreshMs_ = millis();
    refresh();
    evaluateAlarms();
  }
}

void JbdBms::readNextProtectionParam() {
  // A zero means the BMS did not answer (the library returns 0 on timeout), so
  // we keep the fallback rather than alarming against a 0 V threshold.
  float v;
  switch (paramStep_) {
    case 0:
      if ((v = bms_.get_0x20_batt_over_volt_trig() / 1000.0f) > 0) packOverV_ = v;
      break;
    case 1:
      if ((v = bms_.get_0x22_batt_under_volt_trig() / 1000.0f) > 0) packUnderV_ = v;
      break;
    case 2:
      if ((v = bms_.get_0x24_cell_over_volt_trig() / 1000.0f) > 0) cellOverV_ = v;
      break;
    case 3:
      if ((v = bms_.get_0x26_cell_under_volt_trig() / 1000.0f) > 0) cellUnderV_ = v;
      break;
    case 4:
      if ((v = bms_.get_0x28_charge_over_current_trig() / 1000.0f) > 0) chargeOverA_ = v;
      break;
    case 5:
      if ((v = bms_.get_0x29_discharge_over_current_release() / 1000.0f) > 0) {
        dischargeOverA_ = v;
      }
      break;
    default:
      break;
  }

  if (++paramStep_ > 5) {
    paramsRead_ = true;
    log_i("BMS limits: pack %.2f-%.2f V, cell %.3f-%.3f V, %.0f/%.0f A", packUnderV_,
          packOverV_, cellUnderV_, cellOverV_, chargeOverA_, dischargeOverA_);
  }
}

void JbdBms::refresh() {
  const uint8_t n = bms_.get_num_cells();
  if (n == 0) return;  // nothing has come back yet

  setLink(LinkState::Online, "connected");

  const float v = bms_.get_voltage();
  const float a = bms_.get_current();
  voltage_.set(v);
  current_.set(a);
  power_.set(v * a);
  soc_.set(bms_.get_state_of_charge());
  remaining_.set(bms_.get_balance_capacity());
  temperature_.set(bms_.get_ntc_temperature(1));
  chargeFet_.set(bms_.get_charge_mosfet_status());
  dischargeFet_.set(bms_.get_discharge_mosfet_status());

  cells_.count = (n > BMS_MAX_CELLS) ? BMS_MAX_CELLS : n;
  float lo = 5.0f, hi = 0.0f, sum = 0.0f;
  uint8_t counted = 0;
  for (uint8_t i = 0; i < cells_.count; i++) {
    float cv = bms_.get_cell_voltage(i);
    // query_0x04_cell_voltages() zeroes the library's cell array as it sends the
    // request and only refills it when the reply lands. We poll faster than that
    // round trip, so a zero here means "no answer yet this cycle", not "0 V".
    // Copying it through made every cell bar blink at the query rate; keep the
    // last known reading instead. A real disconnect zeroes count_ and hides them.
    if (cv > 0.0f) {
      cells_.volts[i] = cv;
      cells_.balancing[i] = bms_.get_balance_status(i);
    }
    cv = cells_.volts[i];
    if (cv <= 0.0f) continue;  // a cell we have not heard from yet
    if (cv < lo) lo = cv;
    if (cv > hi) hi = cv;
    sum += cv;
    counted++;
  }
  if (counted > 0) {
    cells_.min = lo;
    cells_.max = hi;
    cells_.avg = sum / counted;
    cells_.delta = hi - lo;
    cellMin_.set(cells_.min);
    cellMax_.set(cells_.max);
    cellDelta_.set(cells_.delta);
  }
}

void JbdBms::publishOffline() {
  // Nothing to say - entities go stale on their own and the UI greys them out.
  Alarms::instance().clearPrefix("bms.");
}

void JbdBms::evaluateAlarms() {
  if (!dataValid()) return;

  Alarms& al = Alarms::instance();
  const float v = voltage_.value();
  const float a = current_.value();

  // Pre-alarm ahead of the BMS's own cutoff, per ABYC. The margin buys the crew
  // a few seconds to shed a load before the pack disconnects.
  if (v > packOverV_ - CFG_ALARM_MARGIN_V) {
    al.raise("bms.pack_over_v", "Pack overvoltage", Severity::Critical);
  } else {
    al.clear("bms.pack_over_v");
  }

  if (v < packUnderV_ + CFG_ALARM_MARGIN_V) {
    al.raise("bms.pack_under_v", "Pack undervoltage - shed load", Severity::Critical);
  } else {
    al.clear("bms.pack_under_v");
  }

  if (a > chargeOverA_) {
    al.raise("bms.charge_over_a", "Charge overcurrent", Severity::Critical);
  } else {
    al.clear("bms.charge_over_a");
  }

  if (a < -dischargeOverA_) {
    al.raise("bms.discharge_over_a", "Discharge overcurrent", Severity::Critical);
  } else {
    al.clear("bms.discharge_over_a");
  }

  bool cellHigh = false, cellLow = false;
  for (uint8_t i = 0; i < cells_.count; i++) {
    const float cv = cells_.volts[i];
    if (cv <= 0.0f) continue;
    if (cv > cellOverV_) cellHigh = true;
    if (cv < cellUnderV_) cellLow = true;
  }
  if (cellHigh) {
    al.raise("bms.cell_over_v", "Cell overvoltage", Severity::Critical);
  } else {
    al.clear("bms.cell_over_v");
  }
  if (cellLow) {
    al.raise("bms.cell_under_v", "Cell undervoltage", Severity::Critical);
  } else {
    al.clear("bms.cell_under_v");
  }

  // Anything the BMS itself is protecting against.
  ProtectionStatus p = bms_.get_protection_status();
  if (p.front_end_detection_ic_error) {
    al.raise("bms.afe", "BMS front-end error", Severity::Critical);
  } else {
    al.clear("bms.afe");
  }
  if (p.short_circuit_protection) {
    al.raise("bms.short", "Short circuit protection", Severity::Critical);
  } else {
    al.clear("bms.short");
  }
  if (p.charging_over_temperature_protection || p.discharge_over_temperature_protection) {
    al.raise("bms.over_temp", "Battery over temperature", Severity::Warning);
  } else {
    al.clear("bms.over_temp");
  }
  if (p.charging_low_temperature_protection || p.discharge_low_temperature_protection) {
    al.raise("bms.under_temp", "Battery too cold", Severity::Warning);
  } else {
    al.clear("bms.under_temp");
  }
  if (!chargeFet_.value() && !dischargeFet_.value()) {
    al.raise("bms.fets_open", "BMS has disconnected the pack", Severity::Critical);
  } else {
    al.clear("bms.fets_open");
  }
}

const char* JbdBms::statusLine() const {
  if (link() != LinkState::Online) return statusText();
  const Alarm* worst = Alarms::instance().worstAlarm();
  if (worst) return worst->text;
  const float a = current_.value();
  if (a > 0.05f) return "Charging";
  if (a < -0.05f) return "Discharging";
  return "Idle";
}

}  // namespace cc
