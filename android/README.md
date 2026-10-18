# Camper Control monitor (Android)

Reads the van's BTHome broadcast and shows state of charge, pack power, pack
current, pack voltage and battery temperature - and lists every
thermo-hygrometer in earshot, read **directly off the air** rather than
relayed by the controller. It only listens: there is no connection, no pairing
and nothing it can change on the van. Why it is shaped that way is
`docs/decisions/0016` and `docs/decisions/0017`.

**The wire format is `docs/ICD.md`, and it is normative.** `Icd.java` is this
app's half of it and `include/icd.h` is the firmware's;
`tools/check_icd.py` holds the two to each other and `build.sh` runs it before
every build, so an app that would misread the van fails to compile rather than
failing in a van.

It lives in the firmware repository on purpose. The wire formats are the
contract between the two ends, and this app carries hand-written mirrors of
both firmware decoders - `BtHome.java` of `src/core/bthome.cpp` and
`SwitchBot.java` of `src/core/switchbot.cpp`. Keeping them in one repository is
what lets them be compared by eye when either changes.

**The SwitchBot mirror has a trap built into it.** Its byte offsets count the
two company-id bytes, because that is what NimBLE's `getManufacturerData()`
returns and therefore what the firmware's offsets assume. Android's
`getManufacturerSpecificData()` strips them, so `MainActivity` puts them back
before calling the decoder. The two published decoders for this device disagree
on indices by exactly two for this reason, and choosing wrong yields a
plausible wrong temperature rather than an error.

## Build

```
./android/build.sh            # -> android/build/campercontrol.apk
./android/build.sh install    # and adb install -r
```

The **development phone is a Pixel 6 Pro, serial `1C241FDEE006MY`**, on
Android 17 (SDK 37). Install on that one; the owner's main phone gets builds
by hand. With both attached `adb` refuses to guess, so set `ANDROID_SERIAL`:

```
export ANDROID_SERIAL=1C241FDEE006MY
```

Worth knowing that the dev phone runs **two API levels above the app's
`targetSdk 35`**, which is the right way round - it catches the platform
tightening behaviour before the main phone does. The edge-to-edge bug below
is exactly that kind of change.

No Gradle. This machine has the SDK (platform 35, build-tools 35.0.0) and a
JDK but no Gradle, the app has no third-party dependencies, and the UI is built
in code with no resource files - so `aapt2` has only a manifest to link and the
whole Gradle layer would exist to download itself. **If this ever needs an
icon, a layout file or a library, that is the moment to move to Gradle rather
than keep stretching the script.**

Two traps already paid for, both in `build.sh`:

- **This repository's path contains spaces.** `javac $(find ...)` hands the
  compiler half a path. Sources are collected into a bash array with
  `find -print0` instead.
- **`keytool -genkeypair` has no `-quiet`** on JDK 17.

`android/debug.keystore` is generated on first build and is **not committed** -
anything signed with it installs as an update to this app.

## What it does and does not do

- **minSdk is 31** (Android 12). From Android 12, `BLUETOOTH_SCAN` with
  `neverForLocation` is enough to scan. Before it, any BLE scan required
  `ACCESS_FINE_LOCATION`, which is an absurd thing to demand in order to read a
  battery. Supporting Android 11 and earlier means adding that permission and
  explaining it to the person installing this.
- **Foreground only.** The scan is unfiltered and filtered in code, because a
  `ScanFilter` on service *data* needs a data prefix and mask to match and gets
  it wrong silently. Background scanning does need a real filter, and Android
  will not run an unfiltered scan in the background at all.
- **Identified by service UUID and advertised name, never by address.** iOS
  hands an app a rotating per-install UUID instead of a MAC - confirmed on
  2026-10-18, when macOS reported the board as
  `715481C8-CF97-BE31-0B54-F5DC3C4A33ED` - so an address-keyed design cannot be
  ported. Android is held to the same rule here deliberately.
- **A missing field shows `--`, never the last known value.** Same choice the
  firmware makes: when a reading goes stale it leaves the frame, so an honest
  gap beats a confident wrong number.
- **Pack current may be derived.** BTHome's signed current field holds only
  +-32.767 A, and this pack is rated past 120, so the firmware drops the field
  rather than clamping it. The app then shows power / voltage, which recovers
  the identical number because the firmware publishes power as exactly voltage
  x current. The detail block says which route was used.
- **The van is told apart from a sensor by what its frame carries**, not by
  its name or its address: no thermo-hygrometer broadcasts pack power or
  current. Sensors are keyed by the platform's device identifier, used as an
  opaque string - never parsed, shown or persisted, because the same slot holds
  a rotating UUID on iOS.
- **Sensor readings are merged, not replaced.** A SwitchBot splits its reading
  across two advertisements - temperature and humidity in manufacturer data,
  battery in service data - so overwriting on every frame blanks the
  temperature twice a minute. The firmware learned this the same way.

## Two bugs worth not reintroducing

Both found by running it on a Pixel 8 Pro on 2026-10-18, and neither is
visible from reading the code.

- **The controller listed itself as a sensor.** The van is told apart from a
  thermo-hygrometer by carrying pack power or current - but for the first ten
  seconds after a reboot it carries neither, because the BMS has not connected
  yet, so it advertises a frame with no readings at all. That frame is
  indistinguishable from an anonymous BTHome sensor. Fixed by remembering the
  device once it has identified itself, and by never giving a row to a
  broadcaster with no readings in it.
- **The top of the screen was drawn under the status bar.** An app targeting
  SDK 35 on Android 15 is edge-to-edge **whether it asks or not** - the window
  spans the status bar and the gesture bar - so the status line and the first
  reading were hidden behind the clock, and no amount of scrolling revealed
  them because the content fitted and the ScrollView had nothing to scroll.
  Fixed by taking `Theme.DeviceDefault.NoActionBar` and applying
  `WindowInsets.Type.systemBars()` as padding. Worth knowing that this is
  invisible in an emulator with no cutout, and that the symptom looks exactly
  like a scrolling bug.
- **Rebuilding the sensor rows every tick reset the scroll position.** The
  first version called `removeAllViews()` and re-added every row on each
  500 ms render, so the heading and the first reading scrolled off and could
  not be scrolled back - the next tick undid it. Rows are now created once and
  only their text changes, and the list is in discovery order rather than
  freshest-first, because re-sorting is the same churn by another name.

## This is not the app

`ROADMAP.md` gap 2 recommends **Flutter with `flutter_blue_plus`**, so that iOS
is one signing certificate away rather than one rewrite away. That is still the
plan. This exists because there is no Flutter SDK on the build machine and 430
lines of dependency-free Java proved the firmware end today. What preserves the
iOS option is the protocol - service data, a service-UUID identity, legacy
advertising - not this app, and this app is cheap to throw away.

## Verified

- `aapt2 dump badging`: `minSdkVersion:'31'`, `targetSdkVersion:'35'`,
  `BLUETOOTH_SCAN` with `neverForLocation`, launchable activity, BLE required.
- `BtHome.decode` run on the JVM against a frame captured off-air from the real
  board - `40 00 0A 01 63 02 B9 05 0C F6 36` - decoding to packet id 10, 99%,
  14.65 C, 14.070 V, matching both the firmware's encoder and an independent
  Python decoder.
- Installed and run on a Pixel 6 Pro, Android 17 / SDK 37 (2026-10-18): the
  permission prompt, the layout and the live readings all behave, with no
  crash-buffer entries. Two API levels above `targetSdk`.
- Installed and run on a Pixel 8 Pro (2026-10-18). Reads the live broadcast -
  99%, +0 W, +0.0 A, 14.04 V, 15.0 C - and a real SwitchBot W3400010 at
  23.0 C, 24% and 100% battery, heard at -99 dBm roughly every two minutes.
  The controller is correctly kept out of the sensor list.
- `SwitchBot.decodeManufacturer` run on the JVM against the real W3400010
  frame recorded in `docs/ROADMAP.md` (`69 09 <address> 0B 02 08 95 26 00`
  plus service data `77 00 E4`), decoding to 21.8 C, 38% and 100% battery -
  the values that record documents. Calling it without the company id
  prepended returns `valid=false` rather than a plausible wrong temperature,
  so the off-by-two trap fails loudly.
- **UNVERIFIED: no negative power or current has been displayed.** The bench
  pack sits at rest, so every frame so far reads `+0 W` and `+0.0 A`. The
  derived-current fallback has likewise never been exercised against a real
  pack past 32.767 A - only in host tests.
- Two SwitchBots listed at once (23.9 C / 24% and 22.2 C / 18%), so the list
  and its discovery ordering have been seen with more than one.
- The ICD mismatch path: with firmware deliberately built at v2 against this
  v1 app, the pack readings were hidden and the banner named which end was
  older, while the sensor list kept working.
