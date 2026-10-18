# Interface control document - controller to phone

**ICD version 1.** This is the contract between the firmware in `src/` and the
Android app in `android/`. It is normative: where this document and either
implementation disagree, this document is right and the code is a bug.

Two files are this document in compilable form, and
`tools/check_icd.py` checks them against each other:

| | |
|---|---|
| `include/icd.h` | firmware half |
| `android/src/com/campercontrol/monitor/Icd.java` | app half |

Why this exists at all: the two ends ship separately. The van gets reflashed
over a cable in a driveway and the phone gets an APK whenever someone
remembers. They will be different ages, and the failure mode of a silent
mismatch is not a crash - it is a plausible wrong number on a screen somebody
is using to decide whether to run the heater.

## The link

The controller **advertises** and never connects. It is non-connectable
(`BLE_GAP_CONN_MODE_NON`), which is what keeps it off NimBLE's three-connection
budget and gives the van no inbound BLE surface at all. The app only listens.
Reasoning and rejected alternatives: `decisions/0016`.

| | |
|---|---|
| Advertisement type | legacy, non-connectable, no scan response |
| Service data UUID | `0xFCD2` (BTHome v2) |
| Payload format | BTHome v2, **unencrypted** |
| Local name | `CC` + ICD version, so `CC1` |
| Advertising interval | 1000 ms (`CFG_BLE_BROADCAST_INTERVAL_MS`) |
| Payload refresh | 2000 ms (`CFG_BLE_BROADCAST_REFRESH_MS`) |

The payload is unencrypted on purpose: it is what lets any BTHome-aware tool
read the van with no app at all, which is how this was validated before the app
existed. It also means anyone in range can read the pack. BTHome's AES-CCM is
the answer if that ever matters, and it would be a version bump.

## Identity: what counts as "the van"

**The app identifies the controller by what its frame carries - pack power or
pack current - and not by its name or its address.**

- Not by address, because iOS hands an app a rotating per-install UUID rather
  than a MAC. Confirmed on 2026-10-18: macOS reported the board as
  `715481C8-CF97-BE31-0B54-F5DC3C4A33ED`.
- Not by name, because a scanner may deliver an advertisement without one.
- No thermo-hygrometer broadcasts pack power, so the test is unambiguous.

**The consequence to remember:** for the first ten seconds after a reboot the
BMS has not connected, so the frame carries no readings and the controller is
*not* identifiable. The app handles this by remembering the device once it has
identified itself, and by never listing a broadcaster that has no readings.
This cost a real bug - see `android/README.md`.

## The frame

Objects in ascending id order, which BTHome requires and `BtHomeBuilder`
enforces by refusing an out-of-order or duplicate id.

| Id | Reading | Type | Factor | Source entity |
|---|---|---|---|---|
| `0x00` | packet id | uint8 | 1 | a counter, wraps at 255 |
| `0x01` | state of charge | uint8 | 1 % | `battery.soc` |
| `0x02` | battery temperature | sint16 | 0.01 C | `battery.temperature` |
| `0x0C` | pack voltage | uint16 | 0.001 V | `battery.voltage` |
| `0x5C` | pack power | **sint32** | 0.01 W | `battery.power` |
| `0x5D` | pack current | **sint16** | 0.001 A | `battery.current` |

Multi-byte values are little-endian. Temperatures are Celsius on the wire,
always - the °C/°F choice is a property of a display, never of a reading
(`ARCHITECTURE.md`).

### A field that is absent means "no value", never "zero"

A reading is left out of the frame when it is stale (older than
`CFG_STALE_AFTER_MS`) or when it does not fit its field. Readers must show a
gap, not a last-known value and not zero.

### Pack current is the awkward one

`0x5D` is sint16 x0.001, so it spans **+-32.767 A**, and this pack is rated
past 120 A. The firmware **drops** the field rather than clamping it.

A reader that wants current at any magnitude computes `power / voltage`. That
is a recovery of the same number rather than an estimate from a second
measurement, because the firmware publishes power as exactly voltage x current
(`bms_jbd.cpp`, `power_.set(v * a)`). `BtHome.currentOrDerived()` does this and
reports which route it used. See `decisions/0017`.

### What is deliberately not in the frame

- **Remaining amp-hours.** BTHome v2 has no object id for it. Borrowing a
  volume or mass field would be a lie about units.
- **Cell minimum, maximum and spread.** Three more voltages, expressible only
  as repeated `0x0C` objects told apart by position, which is ambiguous to
  every generic reader - and `bthomeDecode` takes the first of a repeated id
  by design.
- **Cabin temperature and humidity.** The app reads the sensors off the air
  itself; see below.

## The byte budget, which is the real constraint

A legacy advertisement is **31 bytes** and the frame is close to full:

```
  service data AD : 1 len + 1 type (0x16) + 2 UUID + 19 payload = 23
  local name AD   : 1 len + 1 type (0x09) + 3 ("CC1")           =  5
                                                           total = 28
```

There are no flags, because a non-connectable advertisement does not need them
and NimBLE drops them for us - that is worth three bytes and is load-bearing.

**Three bytes of headroom.** Adding a two-byte reading leaves one. The
arithmetic is recomputed from the actual `publish()` calls by
`tools/check_icd.py`, and pinned in
`test_bthome_encode.cpp:test_the_full_frame_fits_a_legacy_advertisement`.
`ble_broadcast.cpp:publish` also logs an error with the byte counts if the
controller ever refuses the data, so this cannot fail silently.

When it runs out, the routes are a scan response (another 31 bytes, still no
connection slot), payload rotation across frames, or extended advertising.
None has been measured. `decisions/0017` weighs them.

## Versioning

`CC_ICD_VERSION` is a **major** version carried in the advertised local name,
so `CC1` today. It is derived from the constant by token pasting rather than
written out, so bumping the version moves the name with it.

**It bumps only on a breaking change**, meaning anything that makes an existing
reader decode a *wrong* number rather than *no* number:

- an object id reused for a different reading
- a scale factor or sign convention changed
- the service UUID or the frame's meaning changed
- encryption turned on

**Adding a reading is not a breaking change** and must not bump it. BTHome is
self-describing and ids ascend, so an older reader decodes everything up to the
first id it does not recognise and reports `complete = false`. That is a short
frame, not a wrong one, and it is the compatibility mechanism this contract
relies on. It only holds while **new ids are strictly greater than every id
already in the frame** - so a new reading with a low id is a breaking change
even though it looks additive.

### What each end does about it

| Reader sees | Behaviour |
|---|---|
| its own version | decode normally |
| no name, so no version | decode normally, note the assumption |
| any other version | **hide the readings** and say which end is older |

Hiding is the point. By the definition above, a different version means
something the reader cannot see has changed meaning, so the bytes it does
understand are not safe to believe.

## Sensors are not part of this interface

Cabin temperature and humidity reach the phone **directly from the sensors**,
not through the controller. The app carries its own decoders for BTHome v2 and
for the SwitchBot W3400010, mirroring `core/bthome.cpp` and
`core/switchbot.cpp`.

This is not laziness about the relay: the advertisement has three spare bytes,
and two temperatures in one BTHome frame are both object `0x02` with no way to
tell them apart. `decisions/0017`.

What the phone therefore cannot see, and only the controller knows: the names
the crew typed in, and the daily highs and lows. Carrying those is a second
advertisement or a connection, not a bigger frame.

**The SwitchBot decoder has a trap in it.** Its byte offsets count the two
company-id bytes, because that is what NimBLE returns and therefore what the
firmware assumes. Android's `getManufacturerSpecificData()` strips them, so the
app puts them back before decoding. The two published decoders for this device
disagree by exactly two bytes for this reason, and choosing wrong gives a
plausible wrong temperature rather than an error.

## Verified

On hardware and on a Pixel 8 Pro, 2026-10-18:

- The controller advertises as `CC1` and logs `ICD v1`, and the frame reads
  off-air as `40 00 1B 01 63 02 E1 05 0C CE 36 5C 00 00 00 00 5D 00 00`.
- The app shows `Live - CC1, ICD v1`.
- **The mismatch path was exercised, not assumed.** Firmware was deliberately
  built at `CC_ICD_VERSION 2`, advertising as `CC2`, while the app stayed at
  v1. The app hid all five pack readings, showed them in amber as `--`, and
  said *"Controller speaks ICD v2; this app speaks v1 - the controller is
  newer. Update the app."* The raw frame stayed visible for diagnosis.
- **Sensors kept working through the mismatch**, which is the design behaving
  as intended rather than a happy accident: they are not part of this
  interface, so a controller the app cannot read does not blind it to the
  cabin temperature.
- `tools/check_icd.py` was tested against all four of its failure modes - a
  version bumped on one side only, a broadcast object the app cannot decode, a
  shared constant drifting, and a name grown past the byte budget. Each one
  fails the check rather than passing quietly.

## Changing this interface

1. Decide whether it is breaking, by the rule above. If in doubt it is.
2. Change `include/icd.h` and `Icd.java` together, including the version if it
   is breaking.
3. Change both decoders and the encoder.
4. Update this document. It is normative; a change that is not written here
   has not happened.
5. Run `python3 tools/check_icd.py`. The pre-commit hook runs it too, on any
   commit touching `src/`, `include/`, `android/` or this file, and
   `SKIP_DOC_CHECK=1` does **not** skip it.

What the checker enforces, and what it cannot:

- It checks that the versions match, that every object the firmware actually
  broadcasts is decodable by the app, that the shared UUIDs and the company id
  agree, and that the advertisement still fits 31 bytes.
- It cannot check that a scale factor is right, that a sign convention matches,
  or that the SwitchBot offsets are indexed the same way. Those are what the
  host tests and this document are for.
