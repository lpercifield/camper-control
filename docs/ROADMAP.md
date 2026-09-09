# Roadmap

Last reviewed 2026-09-09.

## Where it actually is

Runs on hardware. Boots, drives the panel, reads touch, and carries a Settings
overlay whose values survive a power cycle. Flash 49.2%, static RAM 32.6%,
**~97 KB free heap** since the NimBLE port. Main loop ~23k iterations/sec and
LVGL ~84 flushes/sec, re-measured on hardware 2026-09-09 - the older "~46k
loops/sec, ~48 flushes/sec" read the cumulative heartbeat counters as if they
were rates. No integration holds the loop.

The BMS connects on the remembered address and reaches `online`, so the live
pack display works. **There are no blockers left.** What remains is the
difference between a working device and one worth living with.

## Blockers

None. The device boots, drives the panel, reads touch, connects to the battery
and remembers its settings.

## Design gaps

**1. No temperature sensor has been bought.** Everything else is done and
proven: `BleScanner` fans advertisements out to listeners, `core/bthome.*`
decodes BTHome v2, and `integrations/bthome_sensor.*` publishes temperature,
humidity and sensor battery into `Domain::Climate`. The whole path was verified
on 2026-09-09 against a synthetic BTHome advertisement broadcast from an
Android phone - `40 01 54 02 C4 09 03 BF 13` under service UUID `0xFCD2` came
out as 25.0 C, 51% and 84% on the Climate page. What is left is a purchase.

`CFG_BTHOME_INDOOR_MAC` is empty, which means bring-up mode: attach to any
BTHome advertiser heard. That is what makes the phone test work at all, since
Android rotates its advertising address - it changed three times during
testing. Set a real address once there is a real sensor, and add a second
config for outdoor.

Prefer broadcast sensors over connectable ones: a connectable sensor competes
for the one connection slot the BMS holds, a broadcasting one costs nothing but
scan time. That rules out most Inkbird models, which require a connection.

### Shortlist (revised 2026-09-09, nothing bought yet)

- **Outdoor: SwitchBot Indoor/Outdoor Thermo-Hygrometer (W3400010)**, $14.99.
  Broadcasts unencrypted with no pairing, so it never touches the connection
  slot: temperature and humidity in manufacturer data (company `0x0969`),
  battery in service data (UUID `0xFD3D`, device type `'w'`). 2xAAA for about
  two years, IP65, -20 to 60 C. The AAA cells are the point - they behave far
  better in cold than a coin cell, and take lithium AAAs if it gets serious.
  **The catch:** this is the one SwitchBot device that does not follow
  SwitchBot's own documented BLE format, `SwitchBotAPI-BLE` issue 26 was closed
  without a resolution, and every decoder in the wild is reverse-engineered.
  It does **not** speak BTHome, so it needs ~15 lines of its own on top of
  `bthome.*`, and expect the two published decoders to disagree on byte indices
  by exactly two depending on whether they count the company ID.
- **Indoor: Xiaomi LYWSD03MMC with pvvx firmware >= 6.0 in BTHome mode**,
  ~$5-8. Stock firmware encrypts its beacons; the community firmware reflashes
  over BLE from a browser - no hardware, no soldering. CR2032, roughly a year.
  Decodes with `core/bthome.*` as it stands, no new code.
- **If nothing should need reflashing: Shelly BLU H&T**, ~$20-25. Speaks
  BTHome v2 natively, so it also needs no new code. CR2032 for ~3 years, IP54,
  -20 to 60 C - fine indoors, but IP54 and a coin cell is not what to hang
  outside a van year-round.
- **RuuviTag Pro** (~$40+) is the answer only if the van actually sees below
  -20 C. It is the one thing on this list rated to -40 C, IP67.

**Correction to the 2026-09-07 survey.** That survey chose RuuviTag for outdoor
at $30-40 because it was "rated well below freezing", against a coin cell that
"dies in February". That is wrong for the standard tag: **RuuviTag on its stock
battery is rated -20 to +70 C**, the same as the $14.99 SwitchBot. Only
RuuviTag *Pro* reaches -40 C. The stated reason for paying two to three times
more did not hold, and the cheaper sensor has the better battery chemistry for
cold. The survey also listed "SwitchBot Meter (~$15, vendor-documented)" - the
indoor Meter is documented, but the outdoor W3400010 recommended above is
specifically the one that is not.

**Still open:** `BleScanner` uses active scanning, which makes the ESP32 send
scan requests that sensors must answer out of *their* battery. The BMS is found
from a service UUID carried in the advertisement rather than the scan response,
so this could probably be passive. Untested, and worth a measurement once there
is a sensor whose battery life is worth protecting.

**2. `Domain::System` has no page.** The enum and `domainName()` know about it;
`kNavDomains` does not. Anything registered there - uptime, heap, link health,
exactly the diagnostics that would have shortened this week - is silently
invisible. A trap for the next integration author.

**3. Wi-Fi backhaul is not configurable.** The memory objection is gone: Wi-Fi
costs ~41 KB and there are now ~97 KB free, so it fits with room to spare, and
coexistence was never the problem. What remains is the work - an SSID scan, an
on-screen keyboard, credential storage - and one decision: **what the backhaul
actually talks to.** MQTT is the obvious default and pairs with Home Assistant,
but nothing should be built until that is settled.

`core/settings.*` and the Settings overlay exist, and the Settings page carries
a placeholder row, but nothing scans, joins or stores a network.

Note NVS is not encrypted, so a stored Wi-Fi password is readable by anyone who
can dump the flash. See `decisions/0009`.

**4. The arduino-cli harness is dead weight.** Linux-only, already drifted, and
looks maintained. Delete it or put it in CI. See `decisions/0006`.

## Smaller things

- **`WiFi.h` is included for a single line.** `bms_jbd.cpp` pulls in the whole
  Wi-Fi and Networking stack purely to call `WiFi.macAddress()` for the BLE
  device name, and that include is the only reason `platformio.ini` carries the
  `Network` lib_deps and `-I` workaround. `NimBLEDevice::getAddress()` returns
  the same thing. Removing the include should let the workaround go with it -
  worth doing before Wi-Fi lands for real, so the dependency is deliberate
  rather than accidental.
- **A BMS that cannot connect starves every other BLE listener.**
  `BleSerialClient` pauses the shared scanner for the whole connect sequence,
  and a connect failing with HCI `0x3e` retries four times at a 5 s timeout, so
  the scanner can sit paused for twenty seconds at a stretch and temperature
  readings go stale. Observed 2026-09-09; see `TROUBLESHOOTING.md`. The fix is
  to resume the scanner between attempts rather than across the whole cycle.
- **An entity updated in the first millisecond after boot reads as never
  updated.** `Entity` encodes "never updated" as `updatedMs_ == 0`, which is
  also a legitimate value of `millis()`, so such a value shows `--` until the
  next update. Found by `test_entity.cpp:test_epoch_zero`, which pins the
  current behaviour rather than fixing it - nothing publishes that early (the
  BMS takes ~10 s to reach `online`), so this has never been observable. A
  separate `everUpdated_` flag is the fix if it ever matters.
- **Bring-up scaffolding is still in `src/bsp/display.cpp`**: the loop/flush
  heartbeat and `scanI2C`. The expander sweep and the `0x48` probe are gone,
  having answered their question. These two are worth keeping in some form -
  the natural home is the System page (2), where they would be readable on the
  device instead of only over the wire.
- **`flushCb` rewrites LVGL's buffer in place** to rotate 180 degrees. Correct
  for `RENDER_MODE_PARTIAL` today, but it is a side effect on memory LVGL owns
  and will break quietly if the render mode changes. `lv_display_set_rotation()`
  or a corrected panel init table is the honest fix.
- **`Registry::inDomain()` allocates per call**, returning a `std::vector` by
  value from the UI refresh at 4 Hz. Heap churn on a device with no
  defragmentation. An iterator or caller-supplied span costs nothing now.
- **Entity ownership is undocumented in the header.** The registry holds raw
  pointers it does not own; integrations hold the entities as members. Reasonable
  for embedded, but it is an unwritten contract. One sentence in `registry.h`.
- **Alarm slots are never reclaimed.** `count_` only grows and `clear()` just
  sets `active = false`. Fine while ids are static literals - which they must be
  anyway, and now doubly so, since alarms are raised from the BMS task and read
  from the main loop. State the constraint where `raise()` is declared.
- **The BMS task's stack is oversized.** 8 KB allocated, and `taskLoop()`
  reports 6,044 bytes of headroom - about 2 KB actually used. It could drop to
  4 KB, but with ~97 KB free since the NimBLE port there is no reason to take
  that risk yet. The reporting stays so the decision can be evidence-based
  whenever RAM matters again.

## Done

- **Shared BLE scanner, and a BTHome v2 decoder** (2026-09-09).
  `BleScanner` owns `NimBLEDevice::getScan()` and fans advertisements out to
  registered listeners; `BleSerialClient` is now one listener among them rather
  than the owner. Crucially it **keeps scanning while the BMS connection is
  up**, which the shipped code never did - the advertisement counter used to
  sit frozen for the whole time it was connected. Verified on hardware with a
  temporary second listener: `listeners=2`, and the second one kept receiving
  ~3.7 advertisements/sec with the BMS `online`. Costs the cooperative loop
  nothing (46,729 loops and 168 flushes per heartbeat against 46,821 and 168
  before) and ~1.5-2 KB of heap while a window is in flight.
  `core/bthome.*` decodes BTHome v2 with 17 host tests, chosen over any vendor
  format because pvvx >= 6.0 speaks only BTHome and one decoder covers the
  Xiaomi tags, Shelly BLU and most DIY sensors. See `ARCHITECTURE.md`.
- **Host tests for `src/core/`** (2026-09-08). `env:native` in `platformio.ini`
  builds `entity.cpp`, `registry.cpp` and `alarms.cpp` against a host compiler
  and a fake-clock `test_shim/Arduino.h`: 56 cases in about two seconds with no
  board, wired into CI ahead of the firmware build. Verified by mutation, not
  just by passing - loosening `stale()` to `>=`, making `Registry::find` a
  prefix match, and deleting the alarm auto-unmute each failed exactly the test
  that claims to cover them. Firmware size is unchanged (flash 49.2%, RAM
  32.6%), because the two `testReset()` helpers compile only under
  `CC_NATIVE_TEST`. This also closed the "no CI" half of the old item, which
  had already been overtaken by the CI work earlier the same day. See
  `ARCHITECTURE.md`.
- **Touch working** (2026-09-07). The controller answers at `0x48`, not the
  `0x38` its datasheet family uses; `Touch::read` needed no changes once the
  address was right. Axis mirroring is confirmed by use. See `HARDWARE.md`.
- **The BMS integration owns a task** (2026-09-07). Bluedroid's synchronous
  `connect()` was blocking the cooperative loop for 1.5-6 s per retry and
  stopping touch being polled, which read to a user as taps being ignored.
  Loop stalls went from five per 30 s to none, touch polling from 3-6/s back to
  a steady 28/s. See `decisions/0008`.
- **The BMS connects on a remembered address** (2026-09-07). The auto-save from
  `decisions/0009` captured `a4:c1:38:e0:af:18` on a successful connect, and the
  firmware now pins to it: `offline -> searching -> connecting -> online` in
  about 19 s from boot, with no loop stalls. This closed the last blocker
  without anyone having to read a MAC off a log and edit a constant.
- **CI, and a working commit gate** (2026-09-08). `.github/workflows/build.yml`
  runs `pio run` on every push and PR; until now every build had been verified
  on exactly one laptop. The pre-commit documentation gate turned out to have
  been **inert since it was written**: its own testing used `git reset --hard`,
  which deleted the then-untracked hook, and only the documentation describing
  it was ever committed. Restored, tracked, and verified firing.
- **Documentation restructured** (2026-09-08). The standard moved to
  `CONTRIBUTING.md` at the root, where "how we work" belongs, and
  `docs/README.md` became the index its filename promises. Two files called
  README serving different audiences was a genuine source of confusion.
- **Ported to NimBLE** (2026-09-07). +40 KB of heap (56,724 to 97,116 free at
  boot) and 400 KB of flash; the Arduino BLE library is gone from the graph
  entirely. Time to `online` improved from 12-19 s to 10 s. This unblocked
  Wi-Fi, and it is the foundation the shared scanner should be built on. Two
  things it had to learn: NimBLE will not connect while scanning and `stop()`
  is asynchronous, and HCI 0x3e is common and transient - retrying the connect
  beats paying for a rescan. See `decisions/0010`.
- **Screen timeout switches the backlight off** (2026-09-07). It dimmed to a
  glow before, which in a dark van is still a light source. The touch that
  wakes a dark screen is swallowed rather than delivered as a click.
- **Settings, persisted** (2026-09-07). `core/settings.*` stores the last
  connected BMS address, brightness and screen timeout in NVS, reachable from a
  gear in the header. The BMS address is saved automatically on connect and can
  be forgotten from the page. All writes happen on the main loop; tasks hand
  over. See `decisions/0009`.
- **`Hub::loop` names slow integrations** (2026-09-07). Rule 1 of
  `ARCHITECTURE.md` was unenforced; it now logs any integration holding the
  loop past 50 ms. This is what found the bug above.

- **Arduino_GFX bounce-buffer patch made durable** (2026-09-07).
  `tools/patch_gfx.py` applies it as a PlatformIO `pre:` script; verified by
  deleting the installed library and rebuilding from clean. See
  `decisions/0004` and `decisions/0007`.
- **Under version control** (2026-09-07). Repository initialised and pushed to
  `github.com/lpercifield/camper-control`, with `.gitignore` keeping the 223 MB
  of regenerable `.pio/` out of it.
- **Documentation kept honest at the commit** (2026-09-07).
  `.githooks/pre-commit` refuses a firmware change that documents nothing and
  prints the routing rule; `SKIP_DOC_CHECK=1` is the deliberate escape. This
  file going stale within a day of being written is what prompted it. Enable
  with `git config core.hooksPath .githooks` after a clone.
- **Session workflow captured** (2026-09-07). `CLAUDE.md` holds the commands,
  the traps and the instrument-before-theorising debugging protocol.
  `tools/readlog.py` is the serial capture that neither `pio device monitor`
  nor `cat` can do.

## Known and accepted

No audible alarm - the buzzer is on the RP2040 and needs the UART link.
The RP2040 is otherwise idle. One BLE connection only. All recorded in
`PORTING_NOTES.md` and `decisions/0003`.

## Suggested order

1. Buy a sensor and write the integration (1). The scanner and the BTHome
   decoder are in; this is now a shopping decision followed by one small file.
2. Add the System page (2), and move the heartbeat and bus scan into it - they
   are the numbers that diagnosed most of this week, and they are only visible
   over a serial cable.
3. Wi-Fi backhaul (3). Decide what it talks to before building the plumbing.
4. Resolve the build-system split (4).

The test environment landed 2026-09-08 and the shared scanner 2026-09-09, so
what used to be the top two items are gone. 1 makes it useful; 2-4 make it a
product.
