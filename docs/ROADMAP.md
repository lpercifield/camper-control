# Roadmap

Last reviewed 2026-09-09.

## Where it actually is

Runs on hardware. Boots, drives the panel, reads touch, and carries a Settings
overlay whose values survive a power cycle. Flash 49.5%, static RAM 32.9%,
**~94 KB free heap**. Main loop ~23k iterations/sec and LVGL ~84 flushes/sec,
re-measured on hardware 2026-09-09 - the older "~46k loops/sec, ~48
flushes/sec" read the cumulative heartbeat counters as if they were rates. No
integration holds the loop.

The BMS connects on the remembered address and reaches `online`, so the live
pack display works. Wireless temperature and humidity works too: a SwitchBot
W3400010 is read on the Climate page, confirmed against the vendor app on
2026-09-16, and any BTHome v2 broadcaster is read by the same path.

**There are no blockers left.** What remains is one bug worth fixing - the
scanner starves other listeners while the BMS is failing to connect - and then
the difference between a working device and one worth living with.

## Blockers

None. The device boots, drives the panel, reads touch, connects to the battery
and remembers its settings.

## Design gaps

**1. `Domain::System` has no page.** The enum and `domainName()` know about it;
`kNavDomains` does not. Anything registered there - uptime, heap, link health,
exactly the diagnostics that would have shortened this week - is silently
invisible. A trap for the next integration author.

**2. Wi-Fi backhaul is not configurable.** The memory objection is gone: Wi-Fi
costs ~41 KB and there are now ~97 KB free, so it fits with room to spare, and
coexistence was never the problem. What remains is the work - an SSID scan, an
on-screen keyboard, credential storage - and one decision: **what the backhaul
actually talks to.** MQTT is the obvious default and pairs with Home Assistant,
but nothing should be built until that is settled.

`core/settings.*` and the Settings overlay exist, and the Settings page carries
a placeholder row, but nothing scans, joins or stores a network.

Note NVS is not encrypted, so a stored Wi-Fi password is readable by anyone who
can dump the flash. See `decisions/0009`.

**3. The arduino-cli harness is dead weight.** Linux-only, already drifted, and
looks maintained. Delete it or put it in CI. See `decisions/0006`.

**4. Lighting has a page and no integration.** The plan is settled and the
hardware is not bought: each fixture gets its own BLE LED controller, powered
from the 12 V run that already feeds it, so an existing run becomes a
controllable light **without pulling any new control wiring** - which is the
entire point, and the reason the obvious answer of PWM from the RP2040 was
rejected. Reasoning and the rejected alternatives are in `decisions/0012`.

Reference part is the **SP107E**, ~$15: DC5-24 V so it survives a bank that
absorbs at 14.6 V, up to 960 pixels, WS2813/WS2815/WS2818, remembers its state
across a power cut, rated -20 to 60 C. Service `0xFFE0`, characteristic
`0xFFE1`, four-byte commands, and it reads its own state back (`0x01`, `0x02`)
so `confirm()` can be honest.

Three things gate it:

- **Fix the scanner starvation first.** A second connectable device adds its
  own connect and reconnect cycles to a scanner that is already held down
  across the BMS's, and the temperature sensors starve. This is a prerequisite,
  not a follow-up. See the item under Smaller things.
- **Two controllers is the ceiling.** NimBLE allows three connections and the
  BMS holds one. A third fixture means raising
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` and paying RAM for it, or moving lighting
  to Wi-Fi. **UNVERIFIED:** two simultaneous connections have never been made
  on this board.
- **Confirm RGBW before ordering.** The listed ICs are RGB, and RGB white is
  muddy in a cabin. WS2814 is the 12 V RGBW part; whether this controller
  drives it is unconfirmed.

The protocol is close to the SP110E's but not the same - off is `0xBB` not
`0xAB`, colour `0x0C` not `0x1E` - and the readback payload is only partly
mapped, so the integration wants a real device in hand before it is written.
Budget ~0.2 A per fixture of idle draw, controller plus pixel ICs, with the
lights off.

## Smaller things

- **`WiFi.h` is included for a single line.** `bms_jbd.cpp` pulls in the whole
  Wi-Fi and Networking stack purely to call `WiFi.macAddress()` for the BLE
  device name, and that include is the only reason `platformio.ini` carries the
  `Network` lib_deps and `-I` workaround. `NimBLEDevice::getAddress()` returns
  the same thing. Removing the include should let the workaround go with it -
  worth doing before Wi-Fi lands for real, so the dependency is deliberate
  rather than accidental.
- **A longer BLE connection interval measured worse, not better.** The BMS link
  runs on NimBLE's default 50 ms interval and is polled twice a second
  (`kRefreshMs` is 500), so the radio wakes roughly 15 times more often than
  the data needs. Asking for 200 ms works - the BMS accepts, and the negotiated
  values come back 200 ms with a 4000 ms supervision timeout - but measured on
  2026-09-10 it took advertisement reception *down*, from a mean 4.93/s across
  two runs to 4.03/s across two. Loop rate and BMS function were unaffected
  either way. The prediction was that waking the radio four times less often
  would free airtime for scanning; it did not, and **the mechanism is not
  understood**. Left in `BleSerialClient.cpp` behind `CONN_PARAM_TUNING`,
  defaulting off. Worth re-running when a second connection actually contends
  for the radio - which is the lighting plan in `decisions/0012` - because that
  is the case it was meant for and the only one where the trade can be judged.
- **The BLE MTU never negotiates up.** `BleSerialClient.h` defines `MIN_MTU 50`
  and `BLE_BUFFER_SIZE 512`, but the link comes up at the 23-byte default -
  20 bytes of payload per write - and stays there. Nothing asks for more.
  Whether that costs anything on a protocol whose frames are short is not
  measured, but the constant claims an intent the code does not carry out.
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

- **Wireless temperature and humidity, end to end** (2026-09-16). A real
  SwitchBot W3400010 reads on the Climate page, confirmed against the
  SwitchBot app. `core/bthome.*` decodes BTHome v2, `core/switchbot.*` decodes
  the SwitchBot, and `integrations/env_sensors.*` publishes either into
  `Domain::Climate`. Sensors are discovered rather than configured: any
  broadcaster in earshot binds a slot in a fixed table of eight, the Climate
  page gives each one a row, and tapping a row renames it into NVS.

  The SwitchBot settled two things its documentation could not. The byte
  offsets that work are the ones **counting the two company-id bytes**, which
  is what `NimBLEAdvertisedDevice::getManufacturerData()` returns - the other
  published convention is off by two, and choosing wrong yields a plausible
  wrong temperature rather than an error. And the device **sets the top bit of
  the battery byte**, sending `0xE4` rather than `0x64`, so masking it is
  load-bearing. A real frame is manufacturer data
  `69 09 <address> 0B 02 08 95 26 00` under company `0x0969` plus service data
  `77 00 E4` under UUID `0xFD3D`, decoding to 21.8 C, 38% and 100% battery.

  Readings arrive split across two advertisements - temperature and humidity in
  manufacturer data, battery in service data - so `onAdvertisement` merges
  rather than replaces; otherwise a battery-only frame blanks the temperature
  twice a minute. `SWITCHBOT_TRACE` in `env_sensors.cpp` dumps raw frames and
  decoded values, and is off.
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
The RP2040 is otherwise idle. See `PORTING_NOTES.md`.

This section used to say "one BLE connection only", citing `decisions/0003` and
`PORTING_NOTES.md`. Both citations were wrong: `0003` was superseded by `0010`
when the NimBLE port landed, and `PORTING_NOTES.md` never carried the claim.
NimBLE allows **three** connections and this project does not override the
default, so the BMS leaves two spare. Corrected 2026-09-09 - the claim had
been quietly ruling out a whole class of accessory. The one-client limit that
does exist is on the JBD BMS itself, not on this radio; see
`TROUBLESHOOTING.md`.

## Suggested order

1. **Fix the scanner starvation.** Not a numbered gap - it is under Smaller
   things - but it is the only thing actively degrading a feature that works:
   sensors go stale whenever the BMS struggles to connect. It also gates lights.
2. Add the System page (1), and move the heartbeat and bus scan into it - they
   are the numbers that diagnosed most of this project, and they are only
   visible over a serial cable.
3. Lights (4), once the starvation is fixed and a controller is in hand. The
   decision is made; what is left is the protocol capture and one integration
   file.
4. Wi-Fi backhaul (2). Decide what it talks to before building the plumbing -
   and note that ESP-NOW for lighting would ride on the same 41 KB.
5. Resolve the build-system split (3).

Sensors were the top item and are done. What is left is one bug, then the
things that turn a working device into a product.
