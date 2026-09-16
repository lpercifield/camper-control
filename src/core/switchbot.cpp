#include "core/switchbot.h"

namespace cc {

bool switchbotDecodeManufacturer(const uint8_t* data, size_t len,
                                 SwitchBotReading& out) {
  out = SwitchBotReading{};
  // 2 company id + 6 address + 5 payload bytes before the humidity is readable.
  if (data == nullptr || len < 13) return false;

  const uint16_t company =
      static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8);
  if (company != kSwitchBotCompanyId) return false;

  out.valid = true;

  // [2..7] is the device address, then two bytes whose meaning is not
  // documented. Temperature is split: the low nibble of one byte is tenths,
  // the low seven bits of the next are whole degrees, and the top bit of that
  // byte is the SIGN - set means positive, which is the opposite of the usual
  // convention and the easiest thing in here to get backwards.
  const float tenths = static_cast<float>(data[10] & 0x0F) * 0.1f;
  const float whole = static_cast<float>(data[11] & 0x7F);
  const bool positive = (data[11] & 0x80) != 0;
  out.temperatureC = (whole + tenths) * (positive ? 1.0f : -1.0f);
  out.haveTemp = true;

  out.humidityPct = static_cast<float>(data[12] & 0x7F);
  out.haveHum = true;
  return true;
}

bool switchbotDecodeService(const uint8_t* data, size_t len,
                            uint8_t& batteryPct) {
  if (data == nullptr || len < 3) return false;
  // The first byte is the device type. Checking it keeps us from reading a
  // battery percentage out of some other SwitchBot product's frame.
  if (data[0] != kSwitchBotTypeOutdoorMeter) return false;
  batteryPct = data[2] & 0x7F;
  return batteryPct <= 100;
}

}  // namespace cc
