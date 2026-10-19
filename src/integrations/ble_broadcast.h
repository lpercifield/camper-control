#pragma once
#include "core/bthome_encode.h"
#include "core/integration.h"

namespace cc {

// The van telling a phone what the pack is doing, as a BTHome v2 service-data
// advertisement. Read-only, connectionless, and **never connectable**.
//
// Why a broadcast and not a GATT server: a phone holds exactly one Wi-Fi
// association and wireless CarPlay / Android Auto already owns it, which is
// what ruled out the SoftAP this project had planned (`decisions/0016`).
// BLE has no such exclusivity - but a *connection* would spend one of
// NimBLE's three slots, which the BMS and the lighting plan in
// `decisions/0012` have already divided up. Advertising spends none, needs no
// pairing, serves any number of phones at once, and leaves the van with no
// inbound BLE surface whatsoever.
//
// **This is the first thing in the project that reads the registry and talks to
// hardware.** Every other integration owns a device and publishes entities
// *into* the registry; this one subscribes to the registry and puts it on the
// radio, so the arrow runs the other way - the same direction the UI reads in.
// It registers no entities of its own. See `docs/ARCHITECTURE.md`.
//
// It is an Integration anyway, for one concrete reason: `Hub` gives it a loop
// slice and reports it by name if it ever holds the loop past 50 ms, which is
// supervision worth having on anything that touches the radio.
class BleBroadcast : public Integration {
 public:
  const char* name() const override { return "Phone broadcast"; }
  bool begin() override;
  void loop() override;

  // Diagnostics, for the System page whenever design gap 1 is closed. These
  // are counters rather than rates; read the delta, like the display
  // heartbeat.
  uint32_t frames() const { return frames_; }
  uint8_t fieldsInLastFrame() const { return fields_; }

 private:
  // Rebuilds the payload from the registry and hands it to the controller.
  // Returns the number of readings it managed to put in the frame.
  uint8_t publish();

  BtHomeBuilder builder_;
  uint32_t lastRefreshMs_ = 0;
  uint32_t frames_ = 0;
  uint8_t packetId_ = 0;
  uint8_t fields_ = 0;
  bool advertising_ = false;
};

BleBroadcast& bleBroadcast();

}  // namespace cc
