# 0016 - Broadcast the van's state rather than serve it

- **Date:** 2026-10-18
- **Status:** Accepted. The decision below stands, but its rejection of pack
  current and power is **superseded by `0017`** - that rejection rested on an
  incomplete reading of the BTHome table, which does define signed variants.
  The rest of this record is unaffected.

## Context

Several weeks of real use killed the phone plan this project had held since the
start. `ROADMAP.md` gap 2 and gap 6 both assumed one Wi-Fi SoftAP would serve
both a readings page and a firmware upload, so neither had to justify the
measured 41 KB on its own. A phone holds exactly one Wi-Fi association, and
wireless CarPlay and Android Auto *are* that association - the phone joins the
head unit's access point. Joining the van's AP drops the car's screen, and the
ESP32-S3 is 2.4 GHz only, so there is no second band to escape into. The real
choice was the van's readings or the car's screen, every time.

BLE carries no such exclusivity. But the obvious BLE shape - a GATT server the
phone connects to - spends one of NimBLE's three connection slots, which the
BMS and the lighting plan in `decisions/0012` have already divided between
them, and it gives the van its first inbound attack surface.

## Decision

The van **advertises** its state as an unencrypted BTHome v2 service-data frame
under UUID `0xFCD2`, and is **never connectable**
(`BLE_GAP_CONN_MODE_NON`). `core/bthome_encode.*` builds the frame and
`integrations/ble_broadcast.*` puts it on the radio every
`CFG_BLE_BROADCAST_REFRESH_MS`. Reading the van costs no connection slot, no
pairing and no inbound surface, and serves any number of phones at once.

The MVP frame is packet id, state of charge as BTHome's battery percent,
battery temperature and pack voltage - 11 bytes, which with a 9-character local
name leaves five bytes spare inside the 31-byte legacy advertisement. No
payload rotation was needed after all.

Control - switches, setpoints - is deliberately not built. It needs a
connection, and everything under gap 2's *Writing* heading still applies when
it is.

## Alternatives rejected

**A GATT server.** The natural BLE answer, and what a reasonable person would
build. Rejected because reading is the whole of the use case that prompted this
- glance at the pack from bed or from the driver's seat - and a connection buys
nothing for it while costing a slot `decisions/0012` has promised, plus an
inbound write path that would then need authentication. Revisit when control is
wanted, and read the bonding warning in gap 2 before doing so: NimBLE's bond
store writes NVS from the host task, which breaks this project's rule that
every NVS write happens on the main loop through `Settings`.

**A format of our own, since both ends are ours.** Rejected because BTHome
means any existing BTHome-aware tool reads the van with no app written at all,
which is what made this increment free to validate - and it was validated that
way, twice, before the app existed. It also keeps iOS open: BTHome lives in
*service* data, and iOS will not reliably hand an app manufacturer data behind
a background scan filter.

**Broadcasting pack current and power.** **Superseded by `0017`: this
paragraph is wrong.** BTHome v2 does define signed current (`0x5D`) and signed
power (`0x5C`); the table below was read incompletely. The conclusion that
survives is narrower - signed *current* holds only +-32.767 A, so it is power
that carries the van's range. Left here unedited, because the error is the
point: the spec was quoted from a partial fetch and the record sounded certain.

Rejected on the format, not on
preference. BTHome v2 defines current (`0x43`) as uint16 x0.001 A and power
(`0x0B`) as uint24 x0.01 W: both unsigned, and current tops out at 65.535 A
against a pack rated past 120 A. The sign is the entire point - it is what says
whether the van is charging or draining - so sending either would put a
confident wrong number on a phone. That is the failure `decisions/0011` exists
to prevent. They stay out until a signed object id is confirmed in the spec.

**Flutter for the app, as gap 2 recommends.** Still the right answer and still
the plan; it is what keeps iOS one signing certificate away rather than one
rewrite away. Not used *here* only because this machine has no Flutter SDK, and
the MVP receiver is ~430 lines of dependency-free Java that builds offline from
the SDK tools already installed. What preserves the iOS option is the protocol
- service data, a service-UUID identity, legacy advertising - not the app
framework, and the app is cheap to rewrite. See `android/README.md`.

## Consequences

**Advertising costs about 9% of the cooperative loop's spare headroom, and the
UI does not notice.** Measured on hardware 2026-10-18, broadcast on against
off, same build otherwise:

| | loops/sec (median) | flushes/sec | adv/s | boot heap |
|---|---|---|---|---|
| broadcast off | 22,837 | 84.0 | 4.21 | 74,280 |
| broadcast on | 20,849 | 84.0 | 4.05 | 74,096 |

The flush rate is identical, so nothing a person can see changed. Static RAM
went up 2.6 KB and flash 3.5 KB.

**The cost is the radio, not the refresh**, which is worth knowing before
anyone tries to tune it: refreshing the payload every 10 s instead of every 2 s
left the loop rate unchanged at 20,856 against 20,849. That is the same shape
as this project's Wi-Fi measurement, where advertisement reception fell the
moment the radio came up and transmitting barely added to it. So
`CFG_BLE_BROADCAST_REFRESH_MS` is a freshness knob and nothing else.

The 4.21 to 4.05 adv/s difference is **not** established as real: a third run
with the broadcast on measured 4.35/s, above the broadcast-off arm. One run per
arm cannot separate a 4% effect from this signal's spread.

**Anyone in BLE range can read the pack.** The frame is unencrypted, by choice,
because an encrypted payload is invisible to the generic tools that make this
free to validate. It reveals state of charge, pack voltage, a temperature, and
that the van is powered at all. BTHome's own AES-CCM encryption is the fix if
that ever matters, and its symmetric key is a single `Settings` write on the
main loop, consistent with the NVS rule. Turning it on costs the no-app
property.

**This is the first thing in the project that reads the registry and touches
hardware**, so the architecture's one-way arrow now has an exception. See
`ARCHITECTURE.md`.

Revisit all of this if control is wanted, if a second broadcaster makes the
single-frame payload too small, or if the ~9% of loop headroom is ever needed.
