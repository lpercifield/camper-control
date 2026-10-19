#include "core/sp107e.h"

namespace cc {
namespace {

enum : uint8_t {
  kCmdQuery  = 0x01,
  kCmdColour = 0x0C,
  kCmdOn     = 0xAA,
  kCmdOff    = 0xBB,
};

// Frame indices, in byte 1 of each notification.
enum : uint8_t {
  kFrameOnOff  = 0x01,
  kFrameColour = 0x02,
};

// Offsets within a notification. Measured, not documented - see
// `decisions/0022`.
enum : size_t {
  kOffIndex    = 1,
  kOffOn       = 2,  // frame 1
  kOffSegments = 5,  // frame 1
  kOffLeds     = 6,  // frame 1
  kOffRed      = 2,  // frame 2
  kOffGreen    = 3,
  kOffBlue     = 4,
};

// The shortest notification this decoder can read without running off the end.
// Real frames are 15 bytes; this is the bound the indices above actually need.
constexpr size_t kMinFrameLen = 7;

Sp107eFrame make(uint8_t d0, uint8_t d1, uint8_t d2, uint8_t cmd) {
  Sp107eFrame f;
  f.b[0] = d0;
  f.b[1] = d1;
  f.b[2] = d2;
  f.b[3] = cmd;
  return f;
}

// Rounded rather than truncated, so halving a warm white keeps its hue instead
// of sliding towards whichever channel divides evenly.
uint8_t scale(uint8_t v, uint8_t brightness) {
  if (v == 0) return 0;
  const uint16_t out =
      static_cast<uint16_t>((v * brightness + 127) / 255);
  // A channel that was lit stays lit. Rounding alone would drop a dim red to
  // zero and make a light that is on look broken.
  return out == 0 ? 1 : static_cast<uint8_t>(out);
}

}  // namespace

Sp107eFrame sp107eOn() { return make(0, 0, 0, kCmdOn); }
Sp107eFrame sp107eOff() { return make(0, 0, 0, kCmdOff); }
Sp107eFrame sp107eQuery() { return make(0, 0, 0, kCmdQuery); }

Sp107eFrame sp107eColour(uint8_t r, uint8_t g, uint8_t b) {
  return make(r, g, b, kCmdColour);
}

Sp107eFrame sp107eColourScaled(uint8_t r, uint8_t g, uint8_t b,
                               uint8_t brightness) {
  if (brightness == 0) return make(0, 0, 0, kCmdColour);
  return make(scale(r, brightness), scale(g, brightness), scale(b, brightness),
              kCmdColour);
}

bool sp107eColourMatches(const Sp107eState& state, uint8_t r, uint8_t g,
                         uint8_t b, uint8_t brightness) {
  if (!state.haveColour) return false;
  const Sp107eFrame want = sp107eColourScaled(r, g, b, brightness);
  return state.r == want.b[0] && state.g == want.b[1] && state.b == want.b[2];
}

bool sp107eFeed(const uint8_t* data, size_t len, Sp107eState& out) {
  if (data == nullptr || len < kMinFrameLen) return false;

  switch (data[kOffIndex]) {
    case kFrameOnOff:
      // Anything other than 0 is on. The device reports 1, but a controller
      // that answered 2 is still not off, and guessing "off" would make the
      // UI claim a light is dark while it is lit.
      out.on = data[kOffOn] != 0;
      out.segments = data[kOffSegments];
      out.leds = data[kOffLeds];
      out.haveOnOff = true;
      return true;
    case kFrameColour:
      out.r = data[kOffRed];
      out.g = data[kOffGreen];
      out.b = data[kOffBlue];
      out.haveColour = true;
      return true;
    default:
      // Byte 0 is a checksum we cannot verify and byte 1 is the index, so an
      // unknown index is the only signal that this is not our frame. Report
      // it rather than guessing at a layout.
      return false;
  }
}

}  // namespace cc
