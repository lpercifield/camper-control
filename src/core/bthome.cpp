#include "core/bthome.h"

namespace cc {
namespace {

// Object ids this decoder can size. Deliberately short: an id that is not here
// stops the parse, which is safe, where a guessed width would silently produce
// wrong readings. Extend it as sensors need it, not speculatively.
enum : uint8_t {
  kObjPacketId    = 0x00,  // uint8
  kObjBattery     = 0x01,  // uint8, percent
  kObjTemperature = 0x02,  // sint16, x0.01 C
  kObjHumidity    = 0x03,  // uint16, x0.01 %
  kObjVoltage     = 0x0C,  // uint16, x0.001 V
  kObjHumidityU8  = 0x2E,  // uint8, x1 %
  kObjTemperature1= 0x45,  // sint16, x0.1 C
};

// Zero means "unknown", which the parser treats as a full stop.
uint8_t objectSize(uint8_t id) {
  switch (id) {
    case kObjPacketId:     return 1;
    case kObjBattery:      return 1;
    case kObjHumidityU8:   return 1;
    case kObjTemperature:  return 2;
    case kObjHumidity:     return 2;
    case kObjVoltage:      return 2;
    case kObjTemperature1: return 2;
    default:               return 0;
  }
}

uint16_t u16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

int16_t s16(const uint8_t* p) { return static_cast<int16_t>(u16(p)); }

}  // namespace

bool bthomeDecode(const uint8_t* data, size_t len, BtHome& out) {
  out = BtHome{};
  if (data == nullptr || len < 1) return false;

  // Device info byte: bit 0 is encryption, bits 5-7 are the version.
  const uint8_t info = data[0];
  if (info & 0x01) return false;          // encrypted; we hold no key
  if ((info >> 5) != 2) return false;     // not BTHome v2

  out.valid = true;

  size_t i = 1;
  while (i < len) {
    const uint8_t id = data[i];
    const uint8_t size = objectSize(id);
    if (size == 0) return true;           // unknown id: stop, complete = false
    if (i + 1 + size > len) return true;  // truncated: same treatment

    const uint8_t* v = &data[i + 1];
    switch (id) {
      case kObjPacketId:
        out.packetId = v[0];
        out.havePacketId = true;
        break;
      case kObjBattery:
        out.batteryPct = v[0];
        out.haveBattery = true;
        break;
      case kObjHumidityU8:
        if (!out.haveHumidity) {
          out.humidityPct = static_cast<float>(v[0]);
          out.haveHumidity = true;
        }
        break;
      case kObjTemperature:
        if (!out.haveTemperature) {
          out.temperatureC = static_cast<float>(s16(v)) * 0.01f;
          out.haveTemperature = true;
        }
        break;
      case kObjTemperature1:
        if (!out.haveTemperature) {
          out.temperatureC = static_cast<float>(s16(v)) * 0.1f;
          out.haveTemperature = true;
        }
        break;
      case kObjHumidity:
        if (!out.haveHumidity) {
          out.humidityPct = static_cast<float>(u16(v)) * 0.01f;
          out.haveHumidity = true;
        }
        break;
      case kObjVoltage:
        if (!out.haveVoltage) {
          out.voltageV = static_cast<float>(u16(v)) * 0.001f;
          out.haveVoltage = true;
        }
        break;
      default:
        return true;  // unreachable while objectSize and this switch agree
    }
    i += 1 + size;
  }

  out.complete = true;
  return true;
}

}  // namespace cc
