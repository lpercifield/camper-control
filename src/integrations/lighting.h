#pragma once
#include <BleScanner.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

#include "core/entity.h"
#include "core/integration.h"
#include "core/sp107e.h"

namespace cc {

// One BLE LED fixture: an SP107E driving a strip, powered from the 12 V run
// that already feeds the light. `decisions/0012` is why the controller is at
// the fixture rather than PWM from the RP2040; `decisions/0022` is the
// protocol, measured rather than documented.
//
// **This integration owns a FreeRTOS task**, for the same reason the BMS does
// (`decisions/0008`): NimBLE's connect is synchronous and would otherwise
// freeze the UI for up to five seconds. `loop()` is deliberately empty.
//
// **It holds the connection** while the van is awake. That is the mitigation
// in `decisions/0018` for a class of controller with no access control at
// all - while we are connected, the peripheral refuses everyone else - and it
// is also what makes a tap feel instant instead of costing a three-second
// connect.
//
// Brightness is **not** a device command. There is no brightness byte; the
// vendor app scales the colour and so do we. See `core/sp107e.h`.
class Lighting : public Integration {
 public:
  const char* name() const override { return "Lighting"; }
  bool begin() override;
  // Empty on purpose: the work is on this integration's own task.
  void loop() override {}

  // ---- for the UI, main loop only ----
  // Desired colour and brightness. Both are requests: the task applies them
  // and the entities only change when the controller confirms, which is the
  // SwitchEntity contract applied to everything else here.
  void setColour(uint8_t r, uint8_t g, uint8_t b);
  void setBrightness(uint8_t brightness);

  uint8_t brightness() const { return brightness_.load(); }
  // What the controller last told us it is doing, not what we asked for.
  bool confirmedOn() const { return confirmed_.haveOnOff && confirmed_.on; }
  uint8_t confirmedR() const { return confirmed_.r; }
  uint8_t confirmedG() const { return confirmed_.g; }
  uint8_t confirmedB() const { return confirmed_.b; }
  // Pixels the controller believes it is driving. Set in the vendor app, read
  // here, never written: `decisions/0022` explains why.
  uint8_t ledCount() const { return confirmed_.leds; }

 private:
  static void taskEntry(void* self);
  void taskLoop();
  void service();

  bool connectToFixture();
  void disconnectFixture();
  bool send(const Sp107eFrame& f);
  void requestState();
  void publishOffline();

  // Called from the NimBLE host task.
  void onNotify(const uint8_t* data, size_t len);

  // A listener on the shared scanner, used only to find the fixture once.
  class Finder : public BleAdvertisementListener {
   public:
    explicit Finder(Lighting* owner) : owner_(owner) {}
    void onAdvertisement(const NimBLEAdvertisedDevice* device) override;

   private:
    Lighting* owner_;
  };

  Finder finder_{this};
  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* chr_ = nullptr;
  TaskHandle_t task_ = nullptr;

  // Written by the scanner callback, read by our task.
  NimBLEAddress address_;
  std::atomic<bool> haveAddress_{false};
  std::atomic<bool> connected_{false};

  // Desired state, written from the main loop, read by the task. Packed into
  // one word so a colour is never torn halfway through being read.
  std::atomic<uint32_t> wantColour_{0x00FFC88C};  // 0x00RRGGBB, warm white
  std::atomic<uint8_t> brightness_{255};
  std::atomic<bool> wantOn_{false};
  std::atomic<bool> dirty_{true};

  // What the controller has confirmed. Written by the task and the notify
  // callback, read by the UI.
  Sp107eState confirmed_;

  uint32_t lastPollMs_ = 0;
  uint32_t nextConnectMs_ = 0;
  uint8_t connectAttempts_ = 0;

  SwitchEntity power_{"lighting.main", "Cabin light", Domain::Lighting,
                      [this](bool on) { return this->requestOn(on); }};
  NumericEntity level_{"lighting.main_level", "Brightness", Domain::Lighting,
                       "%", 0};

  bool requestOn(bool on);
};

Lighting& lighting();

}  // namespace cc
