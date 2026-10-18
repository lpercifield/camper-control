#include "core/bthome_encode.h"

#include <math.h>

#include "core/bthome.h"

namespace cc {
namespace {

// BTHome v2, unencrypted: version 2 in bits 5-7, encryption bit clear. The
// decoder in core/bthome.cpp checks for exactly this.
constexpr uint8_t kInfoV2Unencrypted = 2 << 5;

enum : uint8_t {
  kObjPacketId    = 0x00,
  kObjBattery     = 0x01,
  kObjTemperature = 0x02,
  kObjHumidity    = 0x03,
  kObjVoltage     = 0x0C,
  kObjPower       = 0x5C,
  kObjCurrent     = 0x5D,
};

// Rounds to nearest rather than truncating: at x0.01 scaling, truncation makes
// every reading read a hundredth low, which is visible on a cell voltage.
bool scaleToU16(float v, float factor, uint16_t& out) {
  if (isnan(v) || isinf(v)) return false;
  const float raw = roundf(v / factor);
  if (raw < 0.0f || raw > 65535.0f) return false;
  out = static_cast<uint16_t>(raw);
  return true;
}

bool scaleToS16(float v, float factor, int16_t& out) {
  if (isnan(v) || isinf(v)) return false;
  const float raw = roundf(v / factor);
  if (raw < -32768.0f || raw > 32767.0f) return false;
  out = static_cast<int16_t>(raw);
  return true;
}

bool scaleToS32(float v, float factor, int32_t& out) {
  if (isnan(v) || isinf(v)) return false;
  const float raw = roundf(v / factor);
  // Compared as float against the exact power-of-two bound, because
  // 2147483647 is not representable as a float and rounds up to 2^31.
  if (raw < -2147483648.0f || raw >= 2147483648.0f) return false;
  out = static_cast<int32_t>(raw);
  return true;
}

}  // namespace

void BtHomeBuilder::reset() {
  buf_[0] = kInfoV2Unencrypted;
  len_ = 1;
  lastId_ = -1;
  overflowed_ = false;
  rejected_ = false;
}

bool BtHomeBuilder::put(uint8_t id, const uint8_t* bytes, uint8_t n) {
  // Ascending order, and no duplicate ids: a receiver sizes objects by id, and
  // two of the same id in one frame is ambiguous about which sensor is which.
  if (static_cast<int16_t>(id) <= lastId_) {
    rejected_ = true;
    return false;
  }
  if (len_ + 1u + n > kMaxPayload) {
    overflowed_ = true;
    return false;
  }
  buf_[len_++] = id;
  for (uint8_t i = 0; i < n; ++i) buf_[len_++] = bytes[i];
  lastId_ = static_cast<int16_t>(id);
  return true;
}

bool BtHomeBuilder::addPacketId(uint8_t id) {
  return put(kObjPacketId, &id, 1);
}

bool BtHomeBuilder::addBattery(uint8_t percent) {
  if (percent > 100) percent = 100;  // a percent is a percent; clamping is safe
  return put(kObjBattery, &percent, 1);
}

bool BtHomeBuilder::addTemperature(float celsius) {
  int16_t raw;
  if (!scaleToS16(celsius, 0.01f, raw)) {
    rejected_ = true;
    return false;
  }
  const uint8_t b[2] = {static_cast<uint8_t>(raw & 0xFF),
                        static_cast<uint8_t>((raw >> 8) & 0xFF)};
  return put(kObjTemperature, b, 2);
}

bool BtHomeBuilder::addHumidity(float percent) {
  uint16_t raw;
  if (!scaleToU16(percent, 0.01f, raw)) {
    rejected_ = true;
    return false;
  }
  const uint8_t b[2] = {static_cast<uint8_t>(raw & 0xFF),
                        static_cast<uint8_t>((raw >> 8) & 0xFF)};
  return put(kObjHumidity, b, 2);
}

bool BtHomeBuilder::addVoltage(float volts) {
  uint16_t raw;
  if (!scaleToU16(volts, 0.001f, raw)) {
    rejected_ = true;
    return false;
  }
  const uint8_t b[2] = {static_cast<uint8_t>(raw & 0xFF),
                        static_cast<uint8_t>((raw >> 8) & 0xFF)};
  return put(kObjVoltage, b, 2);
}

bool BtHomeBuilder::addPower(float watts) {
  int32_t raw;
  if (!scaleToS32(watts, 0.01f, raw)) {
    rejected_ = true;
    return false;
  }
  const uint32_t u = static_cast<uint32_t>(raw);
  const uint8_t b[4] = {static_cast<uint8_t>(u & 0xFF),
                        static_cast<uint8_t>((u >> 8) & 0xFF),
                        static_cast<uint8_t>((u >> 16) & 0xFF),
                        static_cast<uint8_t>((u >> 24) & 0xFF)};
  return put(kObjPower, b, 4);
}

bool BtHomeBuilder::addCurrent(float amps) {
  int16_t raw;
  // The narrow one: +-32.767 A. A pack on a hard charge goes past this, and
  // the honest answer is to drop the field rather than clamp it - a reader
  // that needs current regardless divides power by voltage. See the header.
  if (!scaleToS16(amps, 0.001f, raw)) {
    rejected_ = true;
    return false;
  }
  const uint16_t u = static_cast<uint16_t>(raw);
  const uint8_t b[2] = {static_cast<uint8_t>(u & 0xFF),
                        static_cast<uint8_t>((u >> 8) & 0xFF)};
  return put(kObjCurrent, b, 2);
}

}  // namespace cc
