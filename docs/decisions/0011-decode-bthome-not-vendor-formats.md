# 0011 - Decode BTHome v2 rather than each vendor's own format

- **Date:** 2026-09-09
- **Status:** Accepted

## Context

The van needs an indoor and an outdoor temperature and humidity reading, from a
battery sensor that broadcasts rather than connects - the BMS holds the only
connection slot (`0003`). Every candidate sensor speaks a different dialect:

- Xiaomi LYWSD03MMC encrypts its stock beacons; the community pvvx firmware
  replaces them, and **pvvx 6.0 drops its own non-standard formats and speaks
  only BTHome v2**.
- Shelly BLU H&T broadcasts BTHome v2 out of the box.
- The SwitchBot W3400010 - the best-value outdoor unit - uses neither. It is
  the one SwitchBot device that does not follow SwitchBot's own published BLE
  format, its API issue was closed without a resolution, and every decoder in
  the wild is reverse-engineered.

A decoder had to be written before any sensor could be read, and the choice of
which format to aim at came before the choice of which sensor to buy.

## Decision

`src/core/bthome.*` decodes **BTHome v2** - service data UUID `0xFCD2`, a
device-info byte, then length-implied TLV objects. It is the format the shared
scanner hands bytes to, and the one a sensor is expected to speak. A sensor
that does not speak it needs its own small decoder on top; BTHome is the floor,
not the only option.

Because a BTHome object carries no length, an object id the decoder cannot size
is not skippable. The parser stops there and reports `complete = false` rather
than guessing an offset - a wrong guess does not crash anything, it puts a
plausible wrong temperature on the screen, which is worse.

## Alternatives rejected

**Write the SwitchBot decoder first**, since the SwitchBot is the sensor most
likely to be bought. Rejected: it is fifteen lines against a reverse-engineered
format with no vendor contract behind it, and it reads exactly one product. It
would also have been the *only* decoder, so the second sensor would have
started this argument again.

**Write an ATC/pvvx custom-format decoder**, which is what the 2026-09-07
survey implied. Rejected outright once pvvx 6.0 turned out to drop those
formats: it would have shipped with an expiry date already on it.

**Wait until a sensor is actually bought.** Rejected because the decoder is
pure logic and testable on the host, so it costs nothing to have it ready and
proven before the hardware arrives - and because the format choice is a real
input to which sensor is worth buying.

## Consequences

One decoder covers the Xiaomi tags on current firmware, Shelly BLU, b-parasite
and most DIY sensors, with seventeen host tests and no board required. The
SwitchBot still needs its own fifteen lines if it is bought.

The object-size table is deliberately short - packet id, battery, temperature
(both scalings), humidity (both scalings), voltage. Anything else stops the
parse. Revisit when a sensor genuinely emits something outside that set, not
speculatively; the failure is visible (`complete == false`) rather than silent.

Revisit the whole decision if BTHome v3 appears, or if the sensors that get
bought turn out to speak only vendor formats after all.
