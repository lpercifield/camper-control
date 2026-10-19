# Roadmap

Last reviewed 2026-10-18.

**This file is the work that is left, and nothing else.** The reasoning behind
a choice lives in `decisions/`; the contracts code must obey live in
`ARCHITECTURE.md` and `ICD.md`; board facts live in `HARDWARE.md`; diagnosed
failures live in `TROUBLESHOOTING.md` under the symptom. An item here says what
is missing, what it is worth and what gates it, then points at the record
holding the argument instead of repeating it.

It is written this way because it stopped being that. By 2026-10-18 it had
grown to a thousand lines and had become the place every finding landed,
which made it the one document nobody could skim for what to do next.

## Where it actually is

Runs on hardware. Boots, drives the panel, reads touch, and carries a Settings
overlay whose values survive a power cycle. Flash 50.5%, static RAM 39.5%,
**~74 KB free heap**, LVGL pool 73% used, main loop ~21k iterations/sec and
LVGL 84 flushes/sec, measured 2026-10-18. No integration holds the loop.

The battery is live: the BMS connects on a remembered address and reaches
`online` in about three seconds. Wireless temperature and humidity work - any
BTHome v2 broadcaster and the SwitchBot W3400010, on a Climate page with
24-hour graphs, daily highs and lows, and a global °C/°F setting. There is a
hand-set clock, because the board has no RTC and no NTP.

**A light works.** An SP107E at the fixture drives 30 APA102 pixels on, off
and to a colour, operable from the Lighting page, with the switch only moving
when the controller confirms. `decisions/0022`.

**A phone can read the pack without joining anything.** The van advertises
state of charge, power, current, voltage and battery temperature as BTHome v2
and is never connectable, so it spends none of the three BLE connection slots.
An Android app in `android/` reads that and the sensors directly.
`decisions/0016`, `decisions/0017`, and `ICD.md` for the contract.

**The device still cannot store anything durably.** Both routes were closed by
probe: the microSD slot belongs to the RP2040, and the flash filesystem panics
the chip while the panel refreshes. Daily highs and lows live in RAM and reset
on every power cycle. See the work item 3.

**Every update still needs a cable**, and the framework silently disables the
bootloader's rollback protection, so shipping a bad image over the air would
strand a van. Item 6, and `decisions/0020`.

## Blockers

None.

## Do these next

1. **The System page** (1). The diagnostics that shortened most of this
   project are readable only over a serial cable.
2. **The RP2040 link** (3). One protocol unlocks durable logging, the audible
   alarm and the Grove ports.
3. **A colour and brightness control** for the light (5, really 9). The
   firmware side is done and unreachable from the panel.
4. **Re-arm rollback** (6) before anything else about OTA.

## The work

**1. `Domain::System` has no page.** The enum and `domainName()` know about it;
`kNavDomains` does not, so anything registered there - uptime, heap, link
health - is silently invisible. A trap for the next integration author. The
natural first residents are already written: the loop/flush heartbeat and
`scanI2C` in `bsp/display.cpp`, plus `BleBroadcast::frames()`, all of which
are readable today only over a serial cable.

**2. Wi-Fi backhaul is not configurable.** The phone path is done and is BLE,
not Wi-Fi - a phone holds one Wi-Fi association and wireless CarPlay or
Android Auto already owns it, so a SoftAP would cost the car's screen every
time (`decisions/0016`). What remains is a backhaul for a van sitting on a
driveway, where MQTT into Home Assistant is the obvious shape.

The work is an SSID scan, an on-screen keyboard and credential storage, which
is two more screens on a pool with limited slack - and one decision that comes
first: **what the backhaul actually talks to.** Wi-Fi costs ~41 KB against
~74 KB free, so it fits with ~33 KB to spare; coexistence was never the
problem (`decisions/0019`). NVS is not encrypted, so a stored password is
readable by anyone who can dump the flash (`decisions/0009`).

**3. Nothing can be stored on this device, and the RP2040 is the way out.**
Two walls, both measured: the microSD slot is wired to the RP2040 and the
ESP32-S3 has no path to it (`HARDWARE.md`), and the flash filesystem panics
the chip whenever it is touched while the RGB panel refreshes
(`decisions/0014`). `core/datalog.*` and `bsp/storage.*` are written, tested
and switched off at `CFG_LOG_TO_FLASH`; they are waiting on a medium, not on
code.

**The RP2040 link is the highest-value unbuilt thing in the project.** It owns
the SD slot, the buzzer and the Grove ports; it is a separate chip with no
cache to lose; and it is reachable on `RP2040_UART_TX/RX` with PacketSerial
and COBS. One protocol unlocks durable logging, the audible alarm this device
still does not have, and the Grove sensors.

**4. The arduino-cli harness is dead weight.** `tools/build.sh` is Linux-only,
has drifted further out of step - it knows nothing about `android/`,
`include/icd.h` or `core/bthome_encode.cpp` - and looks maintained. Delete it
or put it in CI. `decisions/0006`.

**5. Lighting works on one fixture; the rest is polish.** On, off and colour
reach a real SP107E driving 30 APA102 pixels, and the switch is operable from
the Lighting page. `decisions/0022` has the protocol, `0012` why the
controller lives at the fixture, `0018` the access control these controllers
do not have.

What is left:

- **Colour and brightness have no UI.** `Lighting::setColour` and
  `setBrightness` exist and work; nothing on the panel calls them, so the
  fixture stays on the warm white it boots with. This is really item 9's
  problem - the generic domain page renders a row and a value, and a light
  wants a picker.
- **Only one fixture.** The integration tracks a single address, found by the
  advertised `0xFFB0`. A second means a small table and deciding which is
  which, and the three-connection ceiling then binds: the BMS holds one, so
  two fixtures is the limit before `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` has to
  go up and pay RAM.
- **Per-fixture setup stays in the vendor app.** IC type, segment count and
  LED count are set there once; the firmware reads them back but does not
  write them (`decisions/0022`).
- **Confirm RGBW before ordering more.** The bench strip is APA102, which is
  RGB, and RGB white is muddy in a cabin; WS2814 is the 12 V RGBW part and
  whether this controller drives it is unconfirmed.

**6. There is no way to update the firmware without a cable.** The partition
table already supports it and the image is about half a slot, so nothing about
the flash layout needs changing.

**The blocker is that the Arduino core disarms the bootloader's rollback
protection before `setup()` runs**, so an image that boots and then boot loops
is marked valid and the van is stuck. This project has produced exactly that
image twice. Override the weak hooks and prove a deliberately-broken image
reverts **before** building any transport; then run the "does flash work with
LVGL stopped" experiment; then pick a transport, where SoftAP is the front
runner. `ARCHITECTURE.md` states the rule and `decisions/0020` has the
mechanism, the flash-write hazard at scale and the transport comparison.

**7. The phone can only read, and three things want it to write.** Sensor
names, the clock and the temperature unit all want to travel from the phone to
the controller.

The clock is the prize. There is no RTC, no Wi-Fi and no NTP, so the only
source of time is a person typing it in, and after a power cut the clock is
wrong by however long the power was off. A phone knows the time exactly;
syncing it is what makes the daily highs and lows trustworthy, and it retires
a whole screen. Note the clock is local time with no timezone.

This is the first write path, so it reverses the property that makes
`decisions/0016` safe. Four constraints, and the third decides the shape:

- It spends a connection slot. Keep it transient - connect, write, confirm,
  disconnect - and the ceiling never has to move.
- **Do not use BLE bonding.** NimBLE's bond store writes NVS from the host
  task, which breaks the rule that every NVS write happens on the main loop
  through `Settings`, during panel refresh. An application-layer HMAC keeps
  the rule intact.
- **Sensor identity does not survive the crossing.** The controller keys
  sensors by MAC and iOS never gives an app a MAC. The way out is to stop
  trying: let the controller hand over its whole sensor table and have the app
  display that, with its own scanning as the fallback. That is a bigger change
  than "sync the names" sounds, and it is the right one.
- A beacon from the phone would avoid the connection entirely **on Android
  only**, because iOS gives an app no way to put arbitrary manufacturer data
  in an advertisement. Taking it would quietly make the write path
  Android-only.

This changes the wire format, so it is an ICD change and a major version bump:
`ICD.md`.

**8. There is no "time remaining" estimate.** The single number a person
actually wants - how long the battery lasts at what it is drawing now, as
`hours:mins`. Everything it needs is published already: `battery.remaining` in
amp-hours, signed `battery.current` and `battery.power`.

The arithmetic is not the problem; **a naive estimate is a confident lie.**
Current swings hard as a compressor cycles, so it wants smoothing over
minutes; near zero current the answer is infinite and must say so rather than
print `999:59`; charging is a different question, not a negative answer; and
for the first seconds after a load changes it should refuse to answer, the way
the clock admits to being approximate. It belongs in `src/core/` as
hardware-free logic with host tests.

**Getting it to the phone is an ICD question and should be settled first.**
The controller is the only end that knows the amp-hours, so it should compute
it - two implementations would drift. But the advertisement has three spare
bytes of thirty-one and BTHome has no object id that honestly means "minutes
remaining". This is the first feature that forces the choice `decisions/0017`
deferred: a scan response, payload rotation, or extended advertising.

**9. The app and the controller do not look like the same product.** The
controller is LVGL - a nav bar across the domains, tiles two across, graphs
behind them, amber for stale. The app is a single scrolling column written to
prove the radio worked.

Two cautions before cloning screens. **Mirroring pages one for one produces
empty pages**: Water and Lighting do not exist on either end yet, so the
honest target is the visual language rather than a nav bar with dead entries.
And **the palette is duplicated**, between `src/ui/theme.h` and hardcoded hex
in `MainActivity` - one has to be the source and the other checked against it,
which `tools/check_icd.py` already exists to do for this kind of two-ended
drift.

Not cosmetic, and not a UI problem: the temperature unit is a global NVS
setting on the controller while the app always prints Celsius. Consistency
there is item 7's job.

## Smaller things

- **The BMS now takes about 20 s to connect at boot, against about 6 s
  before.** The lighting task competes for the radio during startup and wins
  the race to connect. It gets there reliably and nothing downstream notices,
  but it is a real cost of the second connector and nobody has tried to
  reduce it.
- **The LVGL pool has not been measured with the Lighting page populated.**
  `refreshDomainPage` builds rows only for the domain being shown, so the
  figure logged at boot does not include them, and `decisions/0013` is
  explicit that the pool does not report its own exhaustion. Two rows of four
  objects is small, and the page has been opened on hardware without a panic,
  but the number is unmeasured. Logging the pool on a domain change would
  settle it and belongs on the System page (1).
- **`WiFi.h` is included for a single line.** `bms_jbd.cpp` pulls in the whole
  Wi-Fi and Networking stack to call `WiFi.macAddress()` for the BLE device
  name, and that include is the only reason `platformio.ini` carries the
  `Network` lib_deps and `-I` workaround. `NimBLEDevice::getAddress()` returns
  the same thing.
- **The BLE MTU never negotiates up.** `MIN_MTU 50` and `BLE_BUFFER_SIZE 512`
  claim an intent the code does not carry out: the BMS link comes up at the
  23-byte default and stays there. A fact about this device **as central** - a
  phone connecting to us drives its own MTU exchange.
- **An entity updated in the first millisecond after boot reads as never
  updated.** `Entity` encodes "never updated" as `updatedMs_ == 0`, which is
  also a legitimate `millis()`. Pinned by `test_entity.cpp:test_epoch_zero`
  rather than fixed; nothing publishes that early, so it has never been
  observable. A separate `everUpdated_` flag is the fix if it ever matters.
- **Bring-up scaffolding is still in `src/bsp/display.cpp`**: the heartbeat
  and `scanI2C`. Both are worth keeping, on the System page (1).
- **`flushCb` rewrites LVGL's buffer in place** to rotate 180 degrees.
  Correct for `RENDER_MODE_PARTIAL` today, but it is a side effect on memory
  LVGL owns and will break quietly if the render mode changes.
  `lv_display_set_rotation()` or a corrected panel init table is the honest
  fix.
- **`Registry::inDomain()` allocates per call**, returning a `std::vector` by
  value from the UI refresh at 4 Hz. An iterator or caller-supplied span costs
  nothing now.
- **Two contracts are documented only inside `CC_NATIVE_TEST` blocks.** That
  the registry holds non-owning pointers, and that alarm slots are never
  reclaimed, are both explained - but next to the test helpers that needed
  them, not next to `Registry::add` and `Alarms::raise` where a caller would
  look.
- **The BMS task's stack is oversized.** 8 KB allocated against about 2 KB
  used, with 6,044 bytes of reported headroom. It could drop to 4 KB; with
  ~74 KB free there is no reason to take that risk yet, and the reporting
  stays so the decision can be evidence-based.

## Known and accepted

- **No audible alarm.** The buzzer is on the RP2040 and needs the UART link,
  which is item 3.

## Done

Newest first. The detail lives in the record named beside each entry, not
here.

- **Lighting, end to end on one fixture** (2026-10-18). On, off and colour
  reach a real SP107E driving 30 APA102 pixels, and the switch is operable
  from the Lighting page. `core/sp107e.*` is the hardware-free codec with 17
  host tests pinned against bytes measured on the bench;
  `integrations/lighting.*` owns a FreeRTOS task, because NimBLE's connect is
  synchronous and would otherwise freeze the UI for five seconds
  (`decisions/0008`), and holds the connection, which is `decisions/0018`'s
  access-control mitigation and also what makes a tap instant.

  Two things had to change underneath it. **The shared scanner's pause is now
  per-owner**: it was a single bool, and a second connector would have let
  the BMS resume a scan the lighting task was relying on staying down. A
  bitmask rather than a count, because callers resume on every pass without
  tracking whether they paused. And **switch rows on the generic domain page
  are now tappable** - the handler calls `command()` and never sets the
  value, so the row still only moves on `confirm()`.

  Verified on hardware 2026-10-18: fixture found in 2.5 s and connected in
  3.5 s, BMS online alongside it, zero refused or failed writes across the
  owner's on and off taps, no panics, LVGL pool unchanged at boot. Flush rate
  84/s on the Power page with both links up, identical to baseline; loops
  20,488/s against ~21,000; heap 67.3 KB, down 6.7 KB for the task stack and
  the client.

  **This closes the two-connection question.** The ceiling note said two
  simultaneous connections had never been made on this board; the BMS and a
  fixture now hold one each with the scanner still running.
- **The shared scanner no longer starves while the BMS retries**
  (2026-10-18). It stayed paused across the whole connect cycle - four
  attempts at a 5 s timeout - so every other listener went silent for twenty
  seconds whenever the BMS struggled, and the temperature sensors went stale.
  It now runs in the gaps; the connect itself still needs the radio.
  Reproduced on the bench by connecting to a deliberately bogus address:
  0 advertisements in 12.5 s of retrying before, 19 after. This was the
  prerequisite for lighting. `ARCHITECTURE.md`, `TROUBLESHOOTING.md`.
- **The roadmap rewritten to be work rather than findings** (2026-10-18).
  A thousand lines of analysis moved into `decisions/0018` through `0021`,
  `ICD.md` and `ARCHITECTURE.md`. Nothing was deleted without a home.
- **An ICD between the controller and the app, enforced at the commit**
  (2026-10-18). Version carried in the advertised name, checked by
  `tools/check_icd.py` in the pre-commit hook. `ICD.md`.
- **Pack power and current on the phone, and sensors read directly**
  (2026-10-18). `decisions/0017`.
- **A phone can read the van, with no Wi-Fi and no app** (2026-10-18).
  `decisions/0016`.
- **A touch UI for setting the clock** (2026-09-17). `decisions/0015`.
- **A 24-hour graph, daily highs and lows, and a clock to hang them on**
  (2026-09-17). Contracts in `ARCHITECTURE.md`; the storage wall that stopped
  the daily file is `decisions/0014`.
- **Climate page: tiles, a per-sensor graph, and a global temperature unit**
  (2026-09-17). The Celsius-until-display rule is in `ARCHITECTURE.md`; the
  LVGL pool cost is `decisions/0013`.
- **Wireless temperature and humidity, end to end** (2026-09-16). A real
  SwitchBot W3400010 on the Climate page. The byte-offset trap that decides
  whether the reading is right is in `core/switchbot.h` and `ICD.md`.
- **Shared BLE scanner, and a BTHome v2 decoder** (2026-09-09). Contract and
  measurements in `ARCHITECTURE.md`; the choice of BTHome over vendor formats
  is `decisions/0011`.
- **Host tests for `src/core/`** (2026-09-08). `env:native`, now 202 cases in
  about five seconds with no board. `ARCHITECTURE.md`.
- **Ported to NimBLE** (2026-09-07). +40 KB of heap and 400 KB of flash.
  `decisions/0010`.
- **The BMS integration owns a task** (2026-09-07). `decisions/0008`.
- **The BMS connects on a remembered address** (2026-09-07). `Settings`.
- **Settings, persisted** (2026-09-07). `decisions/0009`.
- **Touch working** (2026-09-07). The controller answers at `0x48`.
  `HARDWARE.md`.
- **Screen timeout switches the backlight off** (2026-09-07), not down - a dim
  panel at 2am is still a light source. `ARCHITECTURE.md`.
- **`Hub::loop` names slow integrations** (2026-09-07). `ARCHITECTURE.md`.
- **Arduino_GFX bounce-buffer patch made durable** (2026-09-07).
  `decisions/0007` and `PORTING_NOTES.md`.
- **CI, a commit gate, and the documentation standard** (2026-09-07 to -08).
  `CONTRIBUTING.md`.
