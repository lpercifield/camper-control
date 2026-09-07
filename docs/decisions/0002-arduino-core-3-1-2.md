# 0002 - Arduino core 3.1.2 via the pioarduino fork

- **Date:** 2026-09-06 (recorded; decided during the port, 2026-09-02)
- **Status:** Accepted

## Context

The port first targeted Arduino core 2.0.17, to keep the vendored Bluedroid BLE
client on the API it was written against. That failed on the first compile:
Arduino_GFX 1.5.3 uses ESP-IDF 5.x fields (`refresh_on_demand`, `clock_source`)
that core 2.x does not have.

PlatformIO's official `espressif32` platform still ships core 2.x.

## Decision

Arduino core 3.1.2, via the pioarduino platform fork (tag `53.03.12`) pinned in
`platformio.ini`. This is the combination Seeed's own Indicator tutorial uses.

## Alternatives rejected

**Pin an older Arduino_GFX.** Would have kept core 2.x and the BLE client
untouched. Rejected because it trades a current, maintained graphics library for
an old one on a board whose panel support is recent - the wrong thing to freeze.

**PlatformIO's official platform.** Cannot reach core 3.x at all.

## Consequences

The BLE client needed exactly one change (`BLEAddress::toString()` returns
`String` rather than `std::string`) and the backlight moved to core 3.x's
pin-based `ledcAttach()`.

Less obviously, the core bump is what introduced the bounce-buffer crash in
`decisions/0004` - Arduino_GFX only sets `bounce_buffer_size_px` when
`ESP_ARDUINO_VERSION_MAJOR >= 3`. Core 2.x would not have hit it.

Depending on a third-party platform fork is a real supply-chain risk. Revisit
when the official platform ships core 3.x.
