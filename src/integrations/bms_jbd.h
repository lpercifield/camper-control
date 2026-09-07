#pragma once
#include <BleSerialClient.h>
#include <bms2.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/entity.h"
#include "core/integration.h"

namespace cc {

// Per-cell detail. The generic entity model deliberately does not carry arrays,
// so the cell screen asks this integration directly.
struct CellData {
  uint8_t count = 0;
  float volts[BMS_MAX_CELLS] = {0};
  bool balancing[BMS_MAX_CELLS] = {false};
  float min = 0.0f;
  float max = 0.0f;
  float avg = 0.0f;
  float delta = 0.0f;
};

// The JBD / Overkill Solar BMS, reached over BLE. This is the port of the old
// ESP32 OLED monitor: same library, same protocol, but non-blocking so it can
// share a CPU with the UI, and publishing into entities instead of drawing.
class JbdBms : public Integration {
 public:
  const char* name() const override { return "JBD BMS"; }
  bool begin() override;
  // Deliberately does nothing - see the comment on the definition. The work
  // happens on this integration's own task. See docs/decisions/0008.
  void loop() override {}

  const CellData& cells() const { return cells_; }
  const char* statusLine() const;
  bool dataValid() const { return cells_.count > 0; }

  // Protection thresholds as read back from the BMS itself.
  float packOverV() const { return packOverV_; }
  float packUnderV() const { return packUnderV_; }
  float cellOverV() const { return cellOverV_; }
  float cellUnderV() const { return cellUnderV_; }

 private:
  static void taskEntry(void* self);
  void taskLoop();
  // One pass of the old cooperative loop. Free to block now: it runs on our
  // task, pinned to the core the Arduino loop does not use.
  void service();
  void refresh();
  // One parameter per pass: each of these is a blocking BLE round trip that
  // enters and leaves the BMS's factory mode, so reading all six at once would
  // stall the UI for seconds.
  void readNextProtectionParam();
  void evaluateAlarms();
  void publishOffline();

  BleSerialClient ble_;
  OverkillSolarBms2 bms_;
  CellData cells_;
  TaskHandle_t task_ = nullptr;

  bool bleStarted_ = false;
  bool bmsAttached_ = false;
  uint8_t paramStep_ = 0;
  bool paramsRead_ = false;
  uint32_t lastRefreshMs_ = 0;
  uint32_t attachedAtMs_ = 0;

  float packOverV_ = CFG_FALLBACK_PACK_OVER_V;
  float packUnderV_ = CFG_FALLBACK_PACK_UNDER_V;
  float cellOverV_ = CFG_FALLBACK_CELL_OVER_V;
  float cellUnderV_ = CFG_FALLBACK_CELL_UNDER_V;
  float chargeOverA_ = CFG_FALLBACK_CHG_OVER_A;
  float dischargeOverA_ = CFG_FALLBACK_DIS_OVER_A;

  // Entities owned by this integration.
  NumericEntity soc_{"battery.soc", "State of charge", Domain::Power, "%", 0};
  NumericEntity voltage_{"battery.voltage", "Pack voltage", Domain::Power, "V", 2};
  NumericEntity current_{"battery.current", "Pack current", Domain::Power, "A", 1};
  NumericEntity power_{"battery.power", "Pack power", Domain::Power, "W", 0};
  NumericEntity remaining_{"battery.remaining", "Remaining", Domain::Power, "Ah", 1};
  NumericEntity temperature_{"battery.temperature", "Battery temp", Domain::Power, "C", 1};
  NumericEntity cellMin_{"battery.cell_min", "Lowest cell", Domain::Power, "V", 3};
  NumericEntity cellMax_{"battery.cell_max", "Highest cell", Domain::Power, "V", 3};
  NumericEntity cellDelta_{"battery.cell_delta", "Cell spread", Domain::Power, "V", 3};
  BinaryEntity chargeFet_{"battery.charge_fet", "Charge MOSFET", Domain::Power};
  BinaryEntity dischargeFet_{"battery.discharge_fet", "Discharge MOSFET", Domain::Power};
};

JbdBms& bms();

}  // namespace cc
