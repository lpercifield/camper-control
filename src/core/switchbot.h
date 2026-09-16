#pragma once
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// SwitchBot Indoor/Outdoor Thermo-Hygrometer (W3400010) advertisement decoding.
//
// Pure byte manipulation, so it lives here and is tested on the host - see
// `docs/ARCHITECTURE.md`.
//
// Unlike BTHome this is not an open format. It is the one SwitchBot device that
// does not follow SwitchBot's own published BLE spec; their API issue 26 was
// closed without a resolution and every decoder in the wild is reverse
// engineered. Worse, the two published decoders **disagree on byte indices by
// exactly two**, because one counts the 2-byte company id and the other does
// not. The offsets here assume the id IS included, which is what NimBLE's
// getManufacturerData() returns.
//
// The readings arrive split across two advertisements: temperature and humidity
// in manufacturer data, battery in service data. A sensor that has only sent one
// of them yet is normal, not an error.
// -----------------------------------------------------------------------------

namespace cc {

// Little-endian 0x0969 in the advertisement.
constexpr uint16_t kSwitchBotCompanyId = 0x0969;
// 16-bit service UUID the battery arrives under.
constexpr uint16_t kSwitchBotServiceUuid = 0xFD3D;
// Device type byte for the outdoor meter, 'w'.
constexpr uint8_t kSwitchBotTypeOutdoorMeter = 0x77;

struct SwitchBotReading {
  bool valid = false;
  bool haveTemp = false;
  bool haveHum = false;
  float temperatureC = 0.0f;
  float humidityPct = 0.0f;
};

// `data` is the manufacturer-data field **including** the two company-id bytes,
// exactly as NimBLE hands it over. Returns out.valid.
bool switchbotDecodeManufacturer(const uint8_t* data, size_t len,
                                 SwitchBotReading& out);

// `data` is the service-data payload **after** the 16-bit UUID. Writes the
// battery percentage and returns true only for a frame that looks like this
// device.
bool switchbotDecodeService(const uint8_t* data, size_t len, uint8_t& batteryPct);

}  // namespace cc
