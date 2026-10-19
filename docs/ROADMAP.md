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
overlay whose values survive a power cycle. Flash 51.1%, static RAM 41.2%,
**~61 KB free heap**, LVGL pool 80% used, main loop ~19.8k iterations/sec and
LVGL 84 flushes/sec, measured 2026-10-19. No integration holds the loop.

**The diagnostics are on the panel.** A System screen, behind the gear, carries
uptime, free heap and its low water mark, the LVGL pool, the loop and flush
rates, the I2C bus, the broadcast's frame rate and one line per integration -
all of which used to need a serial cable. `decisions/0023`.

The battery is live: the BMS connects on a remembered address and reaches
`online` in about three seconds. Wireless temperature and humidity work - any
BTHome v2 broadcaster and the SwitchBot W3400010, on a Climate page with
24-hour graphs, daily highs and lows, and a global °C/°F setting. There is a
hand-set clock, because the board has no RTC and no NTP.

**The lights work.** Up to four SP107E fixtures, each with power, brightness
and eight colour presets from the Lighting page, and the switch only moves
when the controller confirms. Which lamp is which is remembered in NVS, so
"Light 2" survives a power cut. `decisions/0022`, `0024`.

**A phone can read the pack without joining anything.** The van advertises
state of charge, power, current, voltage and battery temperature as BTHome v2
and is never connectable, so it spends none of the three BLE connection slots.
An Android app in `android/` reads that and the sensors directly.
`decisions/0016`, `decisions/0017`, and `ICD.md` for the contract.

**The device still cannot store anything durably.** Both routes were closed by
probe: the microSD slot belongs to the RP2040, and the flash filesystem panics
the chip while the panel refreshes. Daily highs and lows live in RAM and reset
on every power cycle. See the work item 2.

**Every update still needs a cable**, and the framework silently disables the
bootloader's rollback protection, so shipping a bad image over the air would
strand a van. Item 5, and `decisions/0020`.

## Blockers

None.

## Do these next

1. **The RP2040 link** (2). One protocol unlocks durable logging, the audible
   alarm and the Grove ports.
2. **Re-arm rollback** (5) before anything else about OTA.
3. **Let the crew rename a fixture** (4). Four lamps called "Light 1" to
   "Light 4" is the last thing standing between the lighting work and being
   genuinely usable, and the mechanism already exists for sensors.

## The work

**1. Wi-Fi backhaul is not configurable.** The phone path is done and is BLE,
not Wi-Fi - a phone holds one Wi-Fi association and wireless CarPlay or
Android Auto already owns it, so a SoftAP would cost the car's screen every
time (`decisions/0016`). What remains is a backhaul for a van sitting on a
driveway, where MQTT into Home Assistant is the obvious shape.

The work is an SSID scan, an on-screen keyboard and credential storage, which
is two more screens on a pool with limited slack - and one decision that comes
first: **what the backhaul actually talks to.** Wi-Fi costs ~41 KB against
~61 KB free, so it fits with ~20 KB to spare; coexistence was never the
problem (`decisions/0019`). That margin shrank by 4 KB when the LVGL pool grew
to hold the Lighting page (`decisions/0025`), and the two screens this item
needs come out of the same pool - check what the board logs before designing
them. NVS is not encrypted, so a stored password is
readable by anyone who can dump the flash (`decisions/0009`).

**2. Nothing can be stored on this device, and the RP2040 is the way out.**
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

**3. The arduino-cli harness is dead weight.** `tools/build.sh` is Linux-only,
has drifted further out of step - it knows nothing about `android/`,
`include/icd.h` or `core/bthome_encode.cpp` - and looks maintained. Delete it
or put it in CI. `decisions/0006`.

**4. Lighting is built; what is left is naming and hardware.** Up to four
fixtures, each with power, brightness and eight colour presets from the panel,
slots remembered so a lamp keeps its name across a power cut.
`decisions/0022` has the protocol, `0012` why the controller lives at the
fixture, `0018` the access control these controllers do not have, `0024` the
page.

What is left:

- **Fixtures are called "Light 1" to "Light 4".** The crew cannot rename them,
  so which lamp is which has to be learned rather than read. `EnvSensors`
  already does this for sensors - name keyed by MAC in NVS, edited through the
  shared text-entry overlay - and the slot MACs lighting now stores are the
  same key. It is the one thing that would make four fixtures pleasant rather
  than merely possible.
- **A fifth fixture has nowhere to go.** `CFG_LIGHTING_MAX_FIXTURES` is 4
  because each holds a BLE connection and the BMS holds one, against a ceiling
  of five. Raising it is one line in `platformio.ini` and about 1.2 KB of heap
  per connection; past six or seven, `decisions/0019` says ESP-NOW is the
  answer rather than a bigger ceiling.
- **Per-fixture setup stays in the vendor app.** IC type, segment count and
  LED count are set there once; the firmware reads them back but does not
  write them (`decisions/0022`).
- **Confirm RGBW before ordering more.** The bench strip is APA102, which is
  RGB, and RGB white is muddy in a cabin; WS2814 is the 12 V RGBW part and
  whether this controller drives it is unconfirmed. The "Cool" preset is the
  one this would improve.
- **No per-item colour on the swatches.** `lv_buttonmatrix` styles its items
  together, so the eight presets are named rather than coloured. Tinting each
  one needs an `LV_EVENT_DRAW_TASK_ADDED` hook; `decisions/0024` has the
  detail and why the names were enough to ship.

**5. There is no way to update the firmware without a cable.** The partition
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

**6. The phone can only read, and three things want it to write.** Sensor
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

**7. There is no "time remaining" estimate.** The single number a person
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

**8. The app and the controller do not look like the same product.** The
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
there is item 6's job.

## Smaller things

- **The BMS sometimes does not connect at boot at all, and nobody knows why.**
  It was recorded here as "about 20 s, reliably" against about 6 s before the
  lighting connector existed. That was too generous. Measured 2026-10-19 over
  three boots each, 60 s of log apiece:

  | | with one fixture (committed) | with the four-fixture build |
  |---|---|---|
  | boot 1 | online at 21.0 s | online at 24.5 s |
  | boot 2 | **never** | **never** |
  | boot 3 | **never** | **never** |

  **The two builds behave identically, so this is not the lighting work's
  doing** - that comparison is the only reason it is written down here rather
  than guessed at. The scanner log says the BMS is not being starved either:
  it holds its own pause (`paused=0x01`) and retries, so it is reaching the
  radio and the connect itself is failing. Whether the cause is this board,
  this BMS, or a BLE stack interaction is unobserved. It recovers on a later
  attempt or a reboot, and every other integration is unaffected.

  Worth instrumenting before theorising: `BleSerialClient` logs nothing per
  attempt, so there is no evidence about what the failure actually is. That is
  the next step, not a fix.
- **The generic domain page's rows are still unmeasured, and still lazy.**
  Water is the only domain left using it, and it has no entities, so nothing
  builds a row today. `refreshDomainPage` builds them on first view, so the
  boot figure would not include them; `showDomain` logs the pool on every
  change of domain, which is the instrument. Budget about 740 bytes a row
  (`decisions/0023`) and remember a row is not far off a whole Lighting page
  (`0024`).
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
  which is item 2.

## Done

Newest first. The detail lives in the record named beside each entry, not
here.

- **Four light fixtures, with colour and brightness on the panel**
  (2026-10-19). `CFG_LIGHTING_MAX_FIXTURES` fixtures, each holding its own
  BLE connection, each with power, a brightness slider and eight colour
  presets. `decisions/0024` is the page, `0025` the pool it needed.

  **Which lamp is which is remembered.** Slot MACs go in NVS, so "Light 2" is
  the same lamp after a power cut - without that the scanner's arrival order
  would rename every lamp on every boot, which is worse than not naming them.
  It paid off twice: a remembered fixture connects in **2.1 s** against about
  6 s, because it skips the scan entirely. Settings has a Forget for when a
  fixture is swapped.

  **Colour is eight presets, not a picker**, and that is a constraint as much
  as a choice: LVGL 9.2 dropped the colour wheel, a canvas big enough to pick
  from costs more RAM than the board has, and an arc hue ring is the
  `lv_draw_sw_mask_radius_init` boot loop by construction.
  `core/light_presets.*` is the palette, host-tested, and `decisions/0024`
  has the argument that it is the better control for a van anyway.

  Brightness is still scaling, not a command, and that reaches the screen:
  the controller reports the *product* of colour and brightness, so the base
  colour cannot be read back. `sp107eColourMatches` asks the question that can
  be answered - "is what it reports what we would have sent" - and a fixture
  somebody changed from the vendor app gets no swatch rather than a wrong one.

  **Two things the measurements forced.** The obvious layout - a row per
  fixture with a detail overlay - took the pool to **100% used, 380 bytes
  free**, surviving only because no arc was on screen to allocate its mask.
  One shared control set is 4,212 bytes and does not grow with the fixture
  count. Even then the peak needed `LV_MEM_SIZE` at 68 KB, which restores the
  margin that existed before this feature rather than improving on it.

  **One bug the bench found, not the diff.** `Lighting::loop()` runs on the
  main loop and its body was four atomic read-modify-writes; at ~17,000 passes
  a second that cost **14% of the loop rate**, 39,115 per heartbeat down to
  33,729, while the flush rate stayed at 168 and the log looked healthy.
  Gated on a counter, which is the idiom `ADDING_AN_INTEGRATION.md` already
  told the next author to use - it just had not said that atomics count too.

  Verified on hardware 2026-10-19: fixture remembered and connected in 2.8 s,
  BMS and sensors online alongside it, **flush rate 168 per heartbeat and
  loops 39,686 - both back at baseline** - pool 12,668 free / 80% used / 1%
  fragmented, heap 61.1 KB, no panics and no slow-integration warnings. 246
  host tests. The BMS startup flakiness under Smaller things was measured
  against the committed build too and is **not** this work's doing.
- **The System screen, and the diagnostics off the serial cable**
  (2026-10-18). `Domain::System` had no page, so anything registered there was
  invisible - a trap for the next integration author. It is now an overlay
  behind the gear, opened from a System card in Settings, and a `SystemInfo`
  integration publishes uptime, free heap and its low water mark, the LVGL
  pool, the loop and flush rates, the I2C bus, the phone broadcast's frame
  rate and one line per integration's link.

  **It is not on the nav bar**, and that is the rule `decisions/0009` already
  set for Settings: the nav bar is what the van can do, and this is how the
  van is doing. `kNavDomains` therefore covers four of the five domains on
  purpose, and `showDomain()` now logs and refuses a domain with no page
  rather than hiding every page and leaving a blank screen - which is how the
  original gap would have presented if anything had called it.

  Three things came out of building it.

  **The screen is a text table, not the generic entity rows**, and that was a
  measurement rather than a preference: twelve of those rows took the LVGL
  pool from 75% used to **90%, with 6,292 bytes free** against the ~5,500 the
  clock setter allocates when the gear is tapped - which is `decisions/0013`'s
  silent exhaustion, one tap away. Two labels cost 3,136 bytes with the
  overlay's own title bar, and all twelve lines fit on one screen.
  `decisions/0023` has the four alternatives that were rejected, including
  growing `LV_MEM_SIZE`, which would have quietly spent the Wi-Fi backhaul's
  headroom.

  **`SystemInfo::loop()` is gated on a counter, not the clock.** Asking
  `millis()` every pass - a 64-bit division at ~19,000 passes a second - was
  the entire cost of the integration: 38,289 loops per heartbeat with the call,
  39,115 with the gate, against ~41,000 before. The same idiom `main.cpp`
  already uses for `Clock::tick()`.

  **The phone frame rate needed its own window.** At a 2 s broadcast refresh a
  one-second sample can only ever see nought or one frame, so the row read 0
  and 60 alternately on the bench. Ten seconds is five frames and a stable
  30/min.

  Two new pieces in `src/core/`, both host-tested: `TextEntity` for a reading
  that is neither a number nor a flag, and `Clock::formatDuration` for an
  uptime that has to survive the 49-day `millis()` wrap. 232 cases now.

  Verified on hardware 2026-10-18: all four integrations online with link rows
  reading `connected / listening / connected / advertising`, the I2C row
  reading `0x20 0x48`, uptime ticking, **flush rate 168 per heartbeat -
  identical to every measurement since the broadcast landed** - pool 78% used
  at boot and 79% with the screen drawn, fragmentation holding at 1% across
  26 s of setting its labels at 4 Hz, heap 66.0 KB, no panics and no slow
  integration warnings. The worst pool case that can occur is Settings plus
  the clock setter at ~7,750 bytes free; the System overlay cannot be open
  alongside the setter, because it covers the card the Set button is on.
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
