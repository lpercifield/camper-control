#pragma once
#include <BleScanner.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <vector>

#include "config.h"
#include "core/entity.h"
#include "core/integration.h"
#include "core/settings.h"
#include "core/sp107e.h"

namespace cc {

// BLE LED fixtures: SP107E controllers driving strips, each powered from the
// 12 V run that already feeds its light. `decisions/0012` is why the controller
// is at the fixture rather than PWM from the RP2040; `decisions/0022` is the
// protocol, measured rather than documented.
//
// **This integration owns a FreeRTOS task**, for the same reason the BMS does
// (`decisions/0008`): NimBLE's connect is synchronous and would otherwise
// freeze the UI for up to five seconds.
//
// **It holds a connection per fixture** while the van is awake. That is the
// mitigation in `decisions/0018` for a class of controller with no access
// control at all - while we are connected, the peripheral refuses everyone
// else - and it is also what makes a tap feel instant instead of costing a
// three-second connect. It is why `CFG_LIGHTING_MAX_FIXTURES` is a connection
// budget rather than a display limit.
//
// Brightness is **not** a device command. There is no brightness byte; the
// vendor app scales the colour and so do we. One consequence reaches all the
// way to the screen: the controller reports the *product* of colour and
// brightness, so the base colour cannot be read back out of it. See
// `sp107eColourMatches` in `core/sp107e.h` for the question that can be
// answered instead.
class Lighting : public Integration {
 public:
  static constexpr size_t kMaxFixtures = CFG_LIGHTING_MAX_FIXTURES;

  const char* name() const override { return "Lighting"; }
  bool begin() override;
  // Nearly empty, and what is left is here because it must be: committing a
  // newly discovered fixture means writing NVS and touching the registry,
  // neither of which may happen on this integration's task.
  void loop() override;

  // ---- for the UI, main loop only --------------------------------------
  // The Lighting page asks this integration directly rather than reading the
  // registry, which is the **same exception** the Climate page takes for
  // sensors and the Power page for cells: an array of like things, which the
  // entity model deliberately does not carry. `ARCHITECTURE.md`.
  //
  // Slots are stable. A fixture's slot is remembered in NVS, so "Light 2" is
  // the same lamp after a power cut - without that the scanner's arrival
  // order would rename every lamp on every boot.
  static const char* fixtureName(size_t slot);
  // A slot that has a fixture assigned, now or remembered from NVS. This is
  // what the page draws a row for, and it **stays true while the fixture is
  // unreachable** - a lamp switched off at the wall is still one of the van's
  // lights, and a row that vanished would read as a fault.
  bool present(size_t slot) const;
  size_t presentCount() const;
  // Reachable right now: we hold a connection and a tap will land.
  bool online(size_t slot) const;
  // Pixels the controller believes it is driving. Set in the vendor app, read
  // here, never written: `decisions/0022` explains why.
  uint8_t leds(size_t slot) const;

  // The switch itself, so the UI issues commands through the one contract
  // that exists for it - command() asks, confirm() is what moves the value.
  // Null for a slot we have never heard of.
  SwitchEntity* power(size_t slot);

  // Requested, not confirmed, and the distinction is forced by the protocol:
  // see the class comment. `colourIsOurs` is how the UI knows whether to keep
  // claiming the swatch below is the one that is lit.
  uint8_t brightness(size_t slot) const;
  uint32_t colour(size_t slot) const;  // 0x00RRGGBB
  bool colourIsOurs(size_t slot) const;

  void setColour(size_t slot, uint8_t r, uint8_t g, uint8_t b);
  void setBrightness(size_t slot, uint8_t brightness);

 private:
  // Everything one fixture needs. Not copyable - it holds atomics - which is
  // why this is a plain array rather than a vector.
  struct Fixture {
    // ---- identity ----
    NimBLEAddress address;
    char mac[Settings::kMacLen] = {0};
    std::atomic<bool> haveAddress{false};
    // Raised by the scanner on the NimBLE host task when a slot binds, lowered
    // by loop() on the main loop once the address is in NVS and the entities
    // are in the registry. The same handover shape as
    // `EnvSensor::needsNameLoad`, and for the same reason: flash and the
    // registry belong to the main loop.
    std::atomic<bool> needsCommit{false};
    bool registered = false;  // main loop only

    // ---- link ----
    NimBLEClient* client = nullptr;
    NimBLERemoteCharacteristic* chr = nullptr;
    std::atomic<bool> connected{false};
    uint32_t nextConnectMs = 0;
    uint32_t lastPollMs = 0;
    uint8_t connectAttempts = 0;

    // ---- what the UI asked for ----
    // Colour packed into one word so it is never torn halfway through a read.
    std::atomic<uint32_t> wantColour{0};
    std::atomic<uint8_t> wantBrightness{255};
    std::atomic<bool> wantOn{false};
    std::atomic<bool> dirty{true};

    // ---- what the controller confirmed ----
    Sp107eState confirmed;
    std::atomic<bool> colourIsOurs{false};
  };

  static void taskEntry(void* self);
  void taskLoop();
  void service();
  // At most one connect attempt per pass of the task, round-robin. A connect
  // pauses the shared scanner and can block for five seconds, so attempting
  // four in one pass would take every other listener off the air for twenty -
  // which is the starvation the scanner fix of 2026-10-18 was about.
  void serviceConnects();
  void serviceLink(Fixture& f);

  bool connectFixture(Fixture& f, size_t slot);
  void disconnectFixture(Fixture& f);
  bool send(Fixture& f, const Sp107eFrame& frame);
  void publishOffline(Fixture& f);
  void refreshLink();

  // Called from the NimBLE host task.
  void onNotify(size_t slot, const uint8_t* data, size_t len);

  // Binds a slot to an address. Scanner context, so it only writes the slot
  // and raises needsCommit; loop() does the rest.
  void bind(const NimBLEAddress& address, const char* mac);

  // A listener on the shared scanner. Unlike the single-fixture version it
  // keeps listening for the whole session, because a second fixture can be
  // switched on hours after the first.
  class Finder : public BleAdvertisementListener {
   public:
    explicit Finder(Lighting* owner) : owner_(owner) {}
    void onAdvertisement(const NimBLEAdvertisedDevice* device) override;

   private:
    Lighting* owner_;
  };

  Finder finder_{this};
  TaskHandle_t task_ = nullptr;
  Fixture fixtures_[kMaxFixtures];
  size_t connectCursor_ = 0;
  // loop() runs on the main loop at ~17,000 Hz and its body is four atomic
  // exchanges. See the measurement in loop().
  uint16_t tick_ = 0;

  // Entities live here rather than in Fixture because their ids and names have
  // to be literals - Entity holds the pointer, it does not copy - so they are
  // built from the tables in the implementation. **Reserved exactly once**:
  // the registry holds non-owning pointers, so a reallocation would dangle
  // every one of them.
  std::vector<SwitchEntity> power_;
  std::vector<NumericEntity> level_;

  bool requestOn(size_t slot, bool on);
};

Lighting& lighting();

}  // namespace cc
