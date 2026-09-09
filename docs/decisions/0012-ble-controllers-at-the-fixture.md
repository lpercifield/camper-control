# 0012 - Control lights with BLE controllers at the fixture

- **Date:** 2026-09-09
- **Status:** Accepted

## Context

`Domain::Lighting` has a page and no integration. The van already has 12 V
power runs to its fixtures, and the expensive part of adding controllable
lighting is not the controller - it is pulling a control conductor back to a
panel through a finished vehicle. **Avoiding new control wiring is the whole
design goal**; anything that requires it has already lost.

The project also believed it could hold only one BLE connection. That was a
Bluedroid-era fact recorded in `decisions/0003`, which `0010` superseded when
the NimBLE port landed. NimBLE's `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` defaults to
3 and this project does not override it, so there are two connection slots
beyond the BMS. The plan below depends on that.

## Decision

Each fixture gets a **BLE LED controller of its own**, powered from the run
that already feeds it, driving an addressable 12 V strip. The van talks to each
one over its own NimBLE connection. The reference part is the **SP107E**:
DC5-24 V, up to 960 pixels, WS2813/WS2815/WS2818, service `0xFFE0`,
characteristic `0xFFE1`, four-byte commands, and an "automatic memory saving
function" so a fixture comes back as it was after the power is cut. Rated
-20 C to 60 C, which is the same floor as the outdoor sensor.

It reports its own state - `CHECK_DEVICE` (`0x01`) and `GET_INFO` (`0x02`) read
back colour and power - so `SwitchEntity::command()` and `confirm()` work as
`ADDING_AN_INTEGRATION.md` intends. That requirement drove the choice as much
as the voltage did.

## Alternatives rejected

**PWM from the RP2040 over the Grove ports.** Technically the best answer:
instant response, no radio contention, no connection budget, and genuinely
readable state. Rejected because it needs a control conductor to every fixture,
which is exactly the cost this design exists to avoid. It also needs the
RP2040 UART link, which nothing has built yet. Revisit for any *new* fixture
where the wire is going in anyway.

**An analogue controller such as ELK-BLEDOM.** Cheaper (~$8), and the only
option that converts an existing run *without replacing the strip* - a real
advantage. Rejected because it is write-only: there is no state readback at
all, so a fixture changed by its IR remote or its phone app would leave the
screen showing what we last wished for. That directly contradicts "confirm, do
not assume", and buying a device that forces an exception to a safety-shaped
rule is a poor trade for eight dollars. Reconsider only for a run whose
existing strip must stay.

**The SP110E.** Better documented than the SP107E and it reads state back
(`0xD5` returns 13 bytes). Rejected on voltage: it is rated **DC5-12 V**, and a
LiFePO4 house bank rests near 13.4 V and absorbs at 14.6 V - the BMS reports
its own limits as 9.60-14.60 V. It would be over spec on every charge cycle, at
every fixture. A buck converter per fixture would fix it and reintroduce the
per-fixture hardware this design is trying to avoid.

**WLED over Wi-Fi.** More capable than any of the above and the best long-term
answer if the van ever has an access point. Rejected for now because Wi-Fi is
not configurable yet (`ROADMAP.md`), it needs something to be the AP, and it
makes lighting depend on a network that does not exist. Revisit when the Wi-Fi
backhaul lands.

## Consequences

**Two controllers, maximum.** The NimBLE ceiling is three connections and the
BMS holds one. A third fixture means raising
`CONFIG_BT_NIMBLE_MAX_CONNECTIONS`, which costs RAM, or moving lighting to
Wi-Fi. This is the sharpest limit the decision creates and it is easy to
forget until the third fixture. **UNVERIFIED:** two simultaneous connections
have never been made on this board - only ever one.

**It makes the scanner starvation worse.** `BleSerialClient` already pauses the
shared scanner across a whole connect sequence, which starves the temperature
sensors whenever the BMS is struggling (`TROUBLESHOOTING.md`). A second
connectable device adds its own connect and reconnect cycles to that. Fixing
the starvation is a prerequisite, not a follow-up.

**Existing strips get replaced.** The SP107E is addressable-only. The run is
reused; the strip is not.

**Idle draw is real.** 20-130 mA for the controller plus roughly 1 mA per pixel
IC with the lights off - about 0.2 A, or 5 Ah a day, per fixture doing nothing.
Several fixtures want a switched feed.

**Do not port the SP110E protocol verbatim.** Same family, different dialect,
and the differences are silent rather than loud: off is `0xBB` here against
`0xAB`, static colour `0x0C` against `0x1E`, state `0x01`/`0x02` against `0xD5`.
The return payload is only partly mapped - RGB plus undocumented bytes - so the
full struct needs a capture against a real device.

**White may disappoint.** WS2815 and WS2818 are RGB, and RGB white is muddy for
cabin lighting. WS2814 is the 12 V RGBW part; whether the SP107E drives it is
unconfirmed and worth asking before ordering.
