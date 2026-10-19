#pragma once
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// SP107E BLE LED controller: frame building and state decoding.
//
// Pure byte manipulation with no hardware in it, so it lives here and is tested
// on the host - the same arrangement as `core/bthome.h`. The integration in
// `integrations/lighting.*` owns the radio; this owns the format.
//
// Every byte here was measured against a real controller driving 30 APA102
// pixels on 2026-10-18, by writing a command and reading the state back.
// `decisions/0022` has the evidence and the two traps:
//
// 1. **The advertised service UUID is not the GATT service.** The device
//    advertises `0xFFB0` and serves `0xFFE0`. Filtering a scan on the GATT
//    service finds nothing at all.
// 2. **The SP110E command table is not a fallback.** Most of its bytes are
//    inert here, and the two tables collide on bytes that matter: the SP110E's
//    rename is `0xBB`, which on this device is *off*.
//
// There is deliberately no rename builder. `0x03` renames and reboots the
// controller, a stray one blanked the device name during bring-up, and nothing
// this project does needs it. See `TROUBLESHOOTING.md`.
// -----------------------------------------------------------------------------

namespace cc {

// What the scan sees, which is not what the client connects to.
constexpr uint16_t kSp107eAdvertisedUuid = 0xFFB0;
// What the client connects to.
constexpr uint16_t kSp107eServiceUuid = 0xFFE0;
constexpr uint16_t kSp107eCharUuid = 0xFFE1;

// Every command is four bytes: three of data and the command byte last.
constexpr size_t kSp107eFrameLen = 4;

struct Sp107eFrame {
  uint8_t b[kSp107eFrameLen];
};

// Commands this project uses, and only those. The data bytes are ignored by
// the device for the ones that take none; they are zeroed here rather than
// left arbitrary so a capture is readable.
Sp107eFrame sp107eOn();
Sp107eFrame sp107eOff();
Sp107eFrame sp107eQuery();
Sp107eFrame sp107eColour(uint8_t r, uint8_t g, uint8_t b);

// **Brightness is not a command on this device.** No brightness byte exists -
// a full capture of the vendor app shows it driving brightness by writing
// scaled colour, and so do we. Scaling is rounded rather than truncated so a
// dim warm white does not drift in hue, and a non-zero colour never scales to
// black while the light is meant to be on: the floor is 1 per non-zero
// channel. `decisions/0022`.
Sp107eFrame sp107eColourScaled(uint8_t r, uint8_t g, uint8_t b,
                               uint8_t brightness);

// The state readback. `0x01` makes the device send two 15-byte notifications;
// byte 0 is a checksum whose algorithm is unknown and is therefore not
// verified, and byte 1 is the frame index.
struct Sp107eState {
  bool haveOnOff = false;   // frame 1 arrived
  bool haveColour = false;  // frame 2 arrived

  bool on = false;
  uint8_t segments = 0;
  uint8_t leds = 0;
  uint8_t r = 0, g = 0, b = 0;

  // Both frames seen, so the picture is whole.
  bool complete() const { return haveOnOff && haveColour; }
};

// Folds one notification into `out`, leaving the other frame's fields alone.
// **Merging rather than replacing is load-bearing**: the two frames arrive as
// separate notifications a few hundred milliseconds apart, so replacing would
// blank the colour every time the on/off frame lands. The same shape as
// `EnvSensors::onAdvertisement`.
//
// Returns true if the notification was a frame this decoder understands.
bool sp107eFeed(const uint8_t* data, size_t len, Sp107eState& out);

}  // namespace cc
