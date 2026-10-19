# 0018 - Cheap BLE light controllers have no access control

- **Date:** 2026-10-18 (analysis dated 2026-09-25)
- **Status:** Accepted. Extends `decisions/0012`, which chose this class of
  hardware without considering the question.

## Context

`decisions/0012` put a BLE LED controller at each fixture so an existing 12 V
run becomes a controllable light with no new control wiring. It weighed
voltage range, idle draw, protocol dialects and the connection ceiling. It
never asked who else can connect.

The answer is anybody. The SP107E class is **open GATT** - not pairing, not
bonding, not a PIN - and so is every cheap BLE LED controller on the market.
That is a gap in the record rather than in the product, and it is recorded
here rather than by rewriting `0012`.

Three consequences, and the second is the one that gets missed:

- A neighbour with the vendor app can change the lights.
- **They can deny you control.** These peripherals accept one central at a
  time, so whoever connects first holds the slot.
- The lights answering at all tells a passer-by the van is powered.

The blast radius is lights only: the controller is an isolated peripheral and
cannot reach the BMS, the registry or anything else.

## Decision

Ship this class of hardware anyway, with two mitigations, and accept the
residual risk.

**Keep the fixture's existing wall switch upstream.** The controller is then
physically dead when the light is off: no radio, no attack surface and no
parasitic draw. One wire that is already there answers both the security
question and the ~0.2 A per fixture of idle draw.

**Hold the connection while the van is awake.** The peripheral then refuses
everyone else. Free, and it suits the design anyway. Two gaps: the slot is
open while the van is off or rebooting, and if somebody is already connected
at boot then *we* are locked out instead. **UNVERIFIED** that this class
accepts only one central - worth checking with the first controller bought.

## Alternatives rejected

**Rename the device so it is harder to find.** Not a defence; a scanner finds
it by service UUID.

**Require real access control.** No product in this class offers it. If
"nobody else may trigger the lights" ever becomes a hard requirement rather
than a preference, **this class of product cannot meet it**, and no firmware
on the van side changes that. That would supersede `0012` and make lighting
depend on Wi-Fi landing first.

## Consequences

Lighting can proceed on the hardware `0012` chose. The wall-switch mitigation
should be written into the install instructions, not just assumed, because it
is doing security work as well as saving current.

Revisit if the requirement hardens, or if a controller with bonding appears at
a sane price.
