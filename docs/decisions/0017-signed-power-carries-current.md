# 0017 - Signed power carries the current the field cannot

- **Date:** 2026-10-18
- **Status:** Accepted. Supersedes the "broadcasting pack current and power"
  rejection in `decisions/0016`, whose premise was wrong.

## Context

`decisions/0016` left pack current and power out of the phone broadcast, on the
grounds that BTHome v2 defines current as `0x43` (uint16 x0.001 A) and power as
`0x0B` (uint24 x0.01 W) - both unsigned - so neither could say whether the van
was charging or draining.

**That was read off an incomplete table.** BTHome v2 also defines signed
variants: `0x5C` power as **sint32 x0.01 W** and `0x5D` current as **sint16
x0.001 A**. The sign is available after all, and the decision record built on
the wrong premise.

The two signed fields are not equally useful, which is the real constraint:

- Signed power spans about +-21.5 MW. A 120 A pack at 14.6 V peaks near
  1.75 kW, so power fits with four orders of magnitude to spare.
- Signed current spans **+-32.767 A**. This pack is rated past 120 A. The
  field is simply too narrow for the hardware.

## Decision

The broadcast carries **signed power (`0x5C`) always, and signed current
(`0x5D`) when it fits**. `BtHomeBuilder::addCurrent` refuses a value outside
+-32.767 A rather than clamping it, so the field disappears from the frame on a
hard charge instead of reporting 32.8 A while 95 A flows.

A reader that wants current at any magnitude **divides power by voltage**.
That is a recovery of the same number, not an estimate from a second
measurement: `JbdBms` publishes `power_.set(v * a)`, so the division returns
exactly what the BMS reported. `BtHome.currentOrDerived()` in the app does
this, and says which route it used.

This fills the advertisement exactly. The frame is seven objects and 19 payload
bytes, which is 23 bytes of service-data AD, leaving 8 for the local name - two
of header and six of name, so `CFG_BLE_BROADCAST_NAME` is now `Camper`.
Measured on hardware: `ad=31 payload=19`, no refusals.

**Remaining amp-hours and the per-cell voltages stay out.** Not for want of a
signed field - BTHome has **no amp-hour object id at all**, and the cell
minimum, maximum and spread are three more voltages, which BTHome can only
express as repeated `0x0C` objects told apart by position. That is ambiguous to
every generic reader, and `bthomeDecode` takes the first of a repeated id by
design. There is also no room left.

**The temperature sensors are not relayed.** The phone reads them off the air
itself. See below.

## Alternatives rejected

**Clamp current to the field instead of dropping it.** Rejected for the reason
`decisions/0011` exists: 32.8 A displayed while 95 A flows is a plausible wrong
number, which is worse than a gap. The gap is also recoverable, and the clamp
is not.

**Relay the temperature sensors through the controller.** The obvious reading
of "add the temperature sensors", and rejected on three counts. The
advertisement is now exactly full, so there is no room. Two temperatures in one
BTHome frame are both object `0x02` and cannot be told apart - the format has
no per-sensor channel that generic readers honour. And it is redundant work:
the sensors already broadcast, the phone is in the same van, and the app
already scans. Reading them directly costs no firmware change, no payload and
no ceiling on how many sensors there are.

What relaying would have bought, and what is therefore still missing: the names
the crew typed into the controller, and the daily highs and lows it keeps. Both
live in its NVS and the phone cannot see them. If those matter, the route is a
second advertisement or a connection, not a bigger frame.

**Extended advertising, to escape the 31-byte budget.** It would carry
everything - cells, amp-hours, named sensors - and the ESP32-S3 supports it.
Rejected for now because it is not a universal floor across phones, no
BTHome-aware tool expects it, and it would cost the no-app property that made
this increment free to validate. The cheaper half-step, if room is ever needed,
is a scan response: non-connectable but scannable advertising buys a second 31
bytes while still spending no connection slot. Neither has been measured, and
the radio already costs ~9% of the loop.

## Consequences

The phone now shows state of charge, pack power, pack current, pack voltage and
battery temperature, plus every thermo-hygrometer in earshot read directly.

The advertisement budget is **exactly full at 31 bytes**. Anything added from
here displaces something, and `CFG_BLE_BROADCAST_NAME` cannot grow past six
characters. The arithmetic is pinned by
`test_bthome_encode.cpp:test_the_full_frame_fits_a_legacy_advertisement`, and
`ble_broadcast.cpp:publish` logs an error with the byte counts if the
controller ever refuses the data.

**UNVERIFIED: no negative power or current has been observed on hardware.** The
bench pack sits at rest, so every frame measured so far carries `+0.00 W` and
`+0.000 A`. The signed path is covered by host tests, including the wire bytes
of -312.50 W (`EE 85 FF FF`), but the real sign convention end to end wants a
van that is actually charging or discharging.
