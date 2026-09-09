#pragma once
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// BTHome v2 advertisement decoding.
//
// Pure byte manipulation with no hardware in it, so it lives here and is tested
// on the host - see `docs/ARCHITECTURE.md`. The caller pulls the service data
// off an advertisement; this turns it into numbers.
//
// BTHome rather than a vendor format on purpose: it is open, versioned and
// published, one decoder covers the Xiaomi tags (pvvx >= 6.0 speaks only
// BTHome), Shelly BLU and most DIY sensors, and it does not move under us the
// way a reverse-engineered format can. See `ROADMAP.md` design gap 1.
// -----------------------------------------------------------------------------

namespace cc {

// The 16-bit service UUID BTHome advertises under.
constexpr uint16_t kBtHomeServiceUuid = 0xFCD2;

struct BtHome {
  // The header parsed: BTHome v2, unencrypted. Nothing below is meaningful
  // unless this is true.
  bool valid = false;
  // Reached the end of the payload without meeting an object id we cannot
  // size. When false, the fields below are still good - they are simply
  // whatever was decoded before the parser had to stop.
  bool complete = false;

  bool havePacketId = false;
  bool haveBattery = false;
  bool haveTemperature = false;
  bool haveHumidity = false;
  bool haveVoltage = false;

  uint8_t packetId = 0;
  uint8_t batteryPct = 0;
  float temperatureC = 0.0f;
  float humidityPct = 0.0f;
  float voltageV = 0.0f;
};

// `data` is the BTHome service-data payload: everything *after* the two UUID
// bytes. Returns out.valid.
//
// A BTHome object carries no length, so its size is implied by its id. An id
// this decoder does not know is therefore not skippable - the parser stops
// there and reports `complete = false` rather than guessing an offset and
// decoding garbage. Ids are emitted in ascending order by convention, so the
// small set understood here (packet id, battery, temperature, humidity,
// voltage) arrives before the exotic ones.
bool bthomeDecode(const uint8_t* data, size_t len, BtHome& out);

}  // namespace cc
