# 0022 - The SP107E protocol, as measured, and what we will use of it

- **Date:** 2026-10-18
- **Status:** Accepted. Supplies the protocol `decisions/0012` said would need
  "a real device in hand before it is written".

## Context

`decisions/0012` chose a BLE LED controller at each fixture and recorded what
was known of the SP107E from documentation: service `0xFFE0`, characteristic
`0xFFE1`, four-byte commands, off is `0xBB` not the SP110E's `0xAB`, colour is
`0x0C` not `0x1E`, and a state readback involving `0x01` and `0x02`. A real
controller and a 30-pixel APA102 strip reached the bench on 2026-10-18 and all
of that was checked.

It was broadly right, and wrong in two places that cost real time.

## What is verified

Everything below was confirmed by round trip - command written, state read
back, value compared - and most of it also by watching the strip.

**Transport.** GATT service `0xFFE0`, characteristic `0xFFE1`, properties
**write and notify, not read**. The device advertises service `0xFFB0`, which
is *not* the GATT service - filtering a scan on `0xFFE0` finds nothing.

**Frame format.** Four bytes, `[d0, d1, d2, CMD]`, command last. Commands that
ignore their data accept anything there; the vendor app sends random bytes.

| CMD | Meaning | Format |
|---|---|---|
| `0x01` | state readback | `xx xx xx 01` -> two 15-byte notifications |
| `0xAA` | on | `xx xx xx AA` |
| `0xBB` | **off** | `xx xx xx BB` |
| `0x0C` | set colour | `RR GG BB 0C` |
| `0x03` | **rename** | `LEN 00 00 03 [ASCII]`, then the device reboots |
| `0x06` | segments and LED count | `SEG LED 00 06` |

**The readback is a write-then-notify, not a GATT read.** `0x01` returns two
15-byte notifications. Byte 0 is a checksum and byte 1 is a frame index, `01`
or `02` - so `0012`'s "reads its own state back (`0x01`, `0x02`)" describes
the *response frames*, not two query commands.

Fields located so far:

| Field | Meaning |
|---|---|
| `f1[2]` | on/off, `0x01` on and `0x00` off |
| `f1[5]`, `f1[6]` | segments, then LED count |
| `f2[2..4]` | red, green, blue |
| `f1[7]` | drifts unprompted - probably the onboard microphone level |

## What was wrong in `0012`, and what it cost

**`0x0C` for colour and `0xBB` for off were both correct.** Good record.

**The SP110E command table is not a safe fallback.** `0x1C` (IC model),
`0x2D` (pixel count), `0x2A` (brightness), `0x3C` (RGB order), `0xAB` (off)
and `0x1E` (colour) are all inert on this device. Only the bytes `0012`
already named worked.

**`0x03` is rename, and it is a trap.** A probe of `5A 5A 5A 03` was read as
"length 90, no characters", which blanked the advertised name and rebooted the
controller. Recovering it took `06 00 00 03 53 50 31 30 37 45`. The SP110E
uses `0xBB` for rename, which is this device's *off* - so the two tables do
not merely differ, they collide on bytes that matter.

**Do not sweep command bytes on this device.** That is how the rename
happened. The state readback is a perfectly good oracle: write one candidate,
read the state, compare. Every finding above came from that loop, and it never
needed a sweep.

## Decision

The integration will use `0xAA`, `0xBB`, `0x0C` and `0x01`, and nothing else.

**Brightness is done by scaling the RGB values, not by a brightness
command.** No brightness byte was found, and the evidence says the vendor app
does the same: a full capture of an app session - connect, toggle off and on,
drag the brightness slider - contains only a CCCD write, two state queries,
the two on/off commands, and a stream of RGB triples. There is no separate
brightness write in it. Scaling also keeps `confirm()` honest, because the
result is visible in `f2[2..4]`.

**Configuration stays in the vendor app.** IC type, segments and LED count are
set once per fixture by the person installing it. The firmware will read them
back and report them, but will not write them: `0x06` is understood only
partially, and setting segments where LEDs were meant is what made the strip
look dead for an hour.

## Alternatives rejected

**Keep hunting for the brightness command.** Rejected because scaling RGB
achieves it, the vendor app appears to do exactly that, and the only way to
find the byte is the sweep that already cost us the device name.

**Capture the vendor app's traffic to get the remaining bytes.** Attempted and
**it cannot work on a stock phone.** Android's bugreport ships only
`btsnooz_hci.log`, which truncates every packet to 15 bytes; after the H4,
ACL and L2CAP headers and the ATT opcode and handle, exactly three bytes of
value survive - so the fourth byte, the command, is always the one cut off.
Measured: 40 packets with `orig=16, included=15`. The full `btsnoop_hci.log`
needs root, and `persist.bluetooth.btsnooplogmode` cannot be set without it.
The live snoop socket on port 8872 accepts a connection and then streams
nothing. The capture was still worth taking - it is what located the on/off
field and showed the app has no brightness command.

## Consequences

The lighting integration can be written: on, off, colour, brightness by
scaling, and an honest `confirm()` from the readback.

**UNVERIFIED:** `f1[5]`/`f1[6]` as segments and LED count comes from the owner
configuring the strip in the vendor app and observing which one we had set
wrongly, not from a round trip of our own. The remaining bytes of both frames
are unmapped, and two connected controllers have still never been tried on
this board.
