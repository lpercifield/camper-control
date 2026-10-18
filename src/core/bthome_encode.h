#pragma once
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// BTHome v2 advertisement *encoding* - the other direction from core/bthome.h.
//
// Pure byte manipulation with no hardware in it, so it lives here and is tested
// on the host against the decoder that already exists: encode, decode, compare.
// See `docs/ARCHITECTURE.md`.
//
// Why BTHome rather than a format of our own, when both ends would be ours:
// every BTHome-aware tool on a phone reads the van with no app written at all,
// which is what makes the first increment free to validate. It also keeps iOS
// open, because BTHome lives in *service* data and iOS will not reliably hand
// an app manufacturer data behind a background scan filter. See `ROADMAP.md`
// design gap 2.
//
// Two rules this builder enforces rather than trusts the caller with:
//
// 1. **Object ids must ascend.** BTHome carries no length per object, so a
//    receiver sizes each one by its id; the format specifies ascending order
//    and `bthomeDecode` stops at the first id it cannot size. Adding an id out
//    of order fails instead of emitting a frame that decodes to garbage.
// 2. **A value that does not fit is dropped, not clamped.** BTHome voltage is
//    uint16 x0.001, so it tops out at 65.535 V; a 70 V reading clamped to that
//    is a plausible lie on a phone screen, and a missing field is an honest
//    gap. This is the same choice `core/history.h` makes about outages and
//    `decisions/0011` makes about vendor formats.
// -----------------------------------------------------------------------------

namespace cc {

class BtHomeBuilder {
 public:
  // The BTHome service-data payload only - everything after the two UUID
  // bytes, which is exactly what `bthomeDecode` takes. The cap is the buffer's,
  // not the format's: the real ceiling is the 31-byte legacy advertisement
  // budget, shared with the flags and the local name, and the caller is what
  // knows how much of that it has spent. `size()` is there to be checked.
  static constexpr size_t kMaxPayload = 22;

  BtHomeBuilder() { reset(); }

  // Starts a frame: writes the v2 unencrypted device-info byte.
  void reset();

  // Each returns false and adds nothing if the value is unrepresentable, the id
  // would break ascending order, or the payload is full. A caller that wants to
  // know which happened has `overflowed()` and `rejected()`.
  bool addPacketId(uint8_t id);        // 0x00  uint8
  bool addBattery(uint8_t percent);    // 0x01  uint8,  %
  bool addTemperature(float celsius);  // 0x02  sint16 x0.01 C
  bool addHumidity(float percent);     // 0x03  uint16 x0.01 %
  bool addVoltage(float volts);        // 0x0C  uint16 x0.001 V
  bool addPower(float watts);          // 0x5C  sint32 x0.01 W
  bool addCurrent(float amps);         // 0x5D  sint16 x0.001 A

  // Note the order these two must be added in: 0x5C before 0x5D, because ids
  // ascend. And note which one is the awkward one - **BTHome's signed current
  // field holds only +-32.767 A**, which a pack rated past 120 A overruns,
  // while signed power is sint32 and has room to spare. So `addCurrent` is the
  // one that will quietly start refusing on a hard charge, and a reader that
  // needs current at any magnitude should divide power by voltage instead:
  // `JbdBms` publishes power as exactly voltage x current, so the division
  // recovers the same number rather than approximating a second measurement.

  const uint8_t* data() const { return buf_; }
  size_t size() const { return len_; }

  // True if any add() was refused for want of room, as opposed to for an
  // unrepresentable value. Kept apart because one is a payload-budget bug and
  // the other is a sensor reading the format cannot carry.
  bool overflowed() const { return overflowed_; }
  // True if any add() was refused because the value did not fit the field.
  bool rejected() const { return rejected_; }

 private:
  bool put(uint8_t id, const uint8_t* bytes, uint8_t n);

  uint8_t buf_[kMaxPayload];
  size_t len_ = 0;
  int16_t lastId_ = -1;  // -1 so object id 0x00 can still be first
  bool overflowed_ = false;
  bool rejected_ = false;
};

}  // namespace cc
