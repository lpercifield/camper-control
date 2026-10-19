# Architecture

## The shape of it

```
     ┌──────────────────────────────────────────────┐
     │  UI  (src/ui/)                               │   reads only
     │  LVGL screens, one page per domain           │
     └───────────────────┬──────────────────────────┘
                         │ reads entity values
     ┌───────────────────▼──────────────────────────┐
     │  Registry (src/core/registry.*)              │
     │  every entity in the van, in one list        │
     └───────────────────▲──────────────────────────┘
                         │ publishes entities
     ┌───────────────────┴──────────────────────────┐
     │  Integrations (src/integrations/)            │   owns a device
     │  driven by Hub, one loop() slice each        │
     └───────────────────┬──────────────────────────┘
                         │
     ┌───────────────────▼──────────────────────────┐
     │  BSP (src/bsp/)   panel, touch, IO expander  │
     └──────────────────────────────────────────────┘

     Alarms (src/core/alarms.*) sits beside the registry: integrations
     raise and clear by name, the UI shows the worst active one.

     Settings (src/core/settings.*) sits beside both: the only thing that
     persists anything, and the only writer of flash.
```

The arrows only point one way. **Nothing in the UI talks to hardware**, and
nothing in an integration knows a screen exists. That seam is the reason a tank
sender is one new file rather than a change to four.

## The contracts

### Entity - `src/core/entity.h`

One reading, one flag, one switch, one line of text. `NumericEntity`,
`BinaryEntity`, `SwitchEntity`, `TextEntity`. Every entity carries its own
update time, so staleness is a property of the data rather than something each
screen has to track.

- Ids are `domain.thing` and globally unique. `Registry::add` drops duplicates
  with a warning rather than overwriting.
- `format(buf, len)` renders for display and is the **only** correct way to turn
  a value into text. It uses `snprintf`; see `decisions/0005`.
- `SwitchEntity` separates `command()` from `confirm()`. State changes when the
  hardware agrees, not when we ask. A relay node that has gone offline shows the
  truth.
- `TemperatureEntity` is a `NumericEntity` that renders in the crew's chosen
  unit. See below - the rule it exists to enforce is worth stating on its own.
- **`TextEntity` owns its value**, which is the opposite of `id_` and `name_`.
  Those are compile-time literals the entity holds a pointer to; a text value
  is built at runtime by whoever publishes it, usually into a stack buffer, so
  it is copied into the entity's own 32 bytes. It exists for readings that are
  neither a number nor a flag - an uptime, what answered on the I2C bus, an
  integration's own words about its link. An empty string is a value; only an
  entity that has never been set reads as `--`.

### Clock - `src/core/clock.h`

There is no RTC, no Wi-Fi and no NTP on this board, so **the only source of
time is a person typing it into the Settings page.** Three consequences that
everything above this depends on:

- It counts from `millis()`, so it drifts with the crystal - seconds a day,
  irrelevant to a daily bucket. `tick()` moves *whole seconds* out of the
  millisecond counter and keeps the remainder, so an awkward tick rate cannot
  make it run slow, and it is unsigned so the 49-day `millis()` wrap is a
  non-event.
- It is **local time with no timezone.** `dayIndex()` is a local day and
  midnight is a real boundary, which is what makes "today's high" mean what it
  says.
- After a power cut it is wrong by however long the power was off. `Settings`
  restores the last value it saved and the clock reports itself
  `approximate()` until a person confirms it. The Settings row says so in
  amber. A wrong timestamp that admits it is a guess is useful; one that does
  not is worse than none.

### Units - `src/core/units.h`

**Every temperature in this project is stored in Celsius and converted exactly
once, on its way to the screen.** Sensors report Celsius, the BMS reports
Celsius, and alarm thresholds are written in Celsius; the unit the crew reads is
a property of the display, not of the reading.

So `TemperatureEntity::value()` keeps returning Celsius no matter what the
Climate page says, and only `format()` and `unit()` change. Anything that
compares a temperature against a limit therefore keeps working when someone taps
`°F`, which is the failure this shape is here to prevent - a threshold that
silently moves by 1.8x is not a bug anyone finds by looking at the screen.

The live value is a single global here rather than a member of `Settings`, for
two reasons: `src/core/` has to compile on a laptop and `Settings` needs NVS,
and a second copy is how two temperatures on one screen end up disagreeing.
`Settings::setTempUnit()` writes both - the live value and the flash - and
`Settings::begin()` restores it. **There is deliberately no getter on
`Settings`.**

`tempUnitSuffix()` returns `°C` / `°F` as UTF-8. The built-in Montserrat fonts
are generated with `-r 0x20-0x7F,0xB0,0x2022`, so U+00B0 is one of exactly three
glyphs they carry outside ASCII; `LV_TXT_ENC` is UTF-8. Do not assume any other
non-ASCII character will render.

### History - `src/core/history.h`

A fixed ring of recent readings per sensor, behind the Climate page's graph.
`CFG_HISTORY_SAMPLES` x `CFG_HISTORY_INTERVAL_MS` in `config.h` sets the window;
144 samples ten minutes apart is 24 hours, which is what the graph shows. The
ring runs on **elapsed time, not the wall clock**, so it fills and scrolls
whether or not anyone has set the clock.

Two properties are load-bearing:

- **A sample the sensor did not send is a gap, not a repeat.** `record()` takes
  `haveTemp` / `haveHum` and stores `History::kNoValue`, and the range helpers
  skip them. Drawing a flat line through an outage is a plausible lie, which is
  the failure mode this project cares most about (`decisions/0011`).
- **Samples are `int16_t`** - tenths of a degree, whole percent - because
  internal RAM is the tight resource and a graph a few hundred pixels wide
  cannot show more. 90 samples x 4 bytes x 8 slots is 2.8 KB.

`EnvSensors` owns the rings and they are **written and read only on the main
loop**, deliberately outside `EnvSensor` itself: that struct is written by the
BLE task and a rebind wipes it wholesale. `loop()` clears the ring for a slot
when it sees `needsNameLoad`, which is the flag that already means "this slot
just became a different sensor".

### The daily high and low - `src/core/datalog.h`

`DayExtremes` per sensor, fed once a minute from the live readings and reset at
local midnight. **Not** computed from the graph's ring: that samples every ten
minutes and would walk straight past the hour the sun was on the roof.

It needs the clock, and nothing else - no clock means no midnight, so the
Climate page says `set the clock for daily highs` rather than showing a dash
that looks like a broken sensor.

**It does not survive a reboot**, and the day file that would have fixed that
is disabled. `datalog.*` still carries the whole file format - rows keyed by
the last six hex digits of the MAC rather than by slot index, because slots are
positional and a sensor takes whichever is free - along with rotation and
replay, all under test. It is waiting on a storage medium. See
`decisions/0014`, and the next section for why.

### Storage - `src/bsp/storage.h`, and why the log is off

**The ESP32-S3 on this board cannot store anything durably while the display
runs.** Two independent findings, both settled on hardware on 2026-09-17:

- The **microSD slot belongs to the RP2040**. GPIO 47 floats; the pin budget
  has no room for an SD bus. `EXP_SD_CS` in the pin map is a carried-over
  community constant that leads nowhere. See `HARDWARE.md`.
- **The internal flash filesystem panics the chip** when touched while the RGB
  panel refreshes: the panel's IRAM-safe VSYNC handler calls `gdma_hal_append`,
  which is in flash, during the cache-disabled window of any flash operation.
  Reliably, not as a race. See `decisions/0014`.

`bsp/storage.*` is a complete LittleFS layer and it works - mounting before
`displayBegin()` is the one safe window. It is simply not reachable afterwards,
so `CFG_LOG_TO_FLASH` is 0.

The route to durable logging is the **RP2040**: it owns the SD slot, it is a
separate chip with no cache to lose, and it is reachable on
`RP2040_UART_TX/RX`. It needs firmware and a protocol. Wi-Fi backhaul is the
other route and needs no local storage at all.

**Note this also implicates `Settings`.** NVS writes are the same hazard with a
shorter window - one or two milliseconds, usually between VSYNCs. The project
has been doing it since the start without a panic, which is luck with good
odds. Do not add more flash writes.

### Registry - `src/core/registry.h`

A flat list, a singleton, no ownership. Integrations own their entities as
members and outlive the registry's pointers; the registry never frees anything.

`inDomain()` returns a `std::vector` by value and is called from the UI refresh
at 4 Hz. That is a known wart - see `ROADMAP.md`.

### BleScanner - `lib/BleSerialClient/BleScanner.h`

NimBLE has one scan object and one set of scan callbacks, so whoever calls
`setScanCallbacks()` last wins. `BleScanner` owns them and fans every
advertisement out to registered listeners.

**Never call `NimBLEDevice::getScan()` from anywhere else.** Implement
`BleAdvertisementListener` and register it; the BLE client is one listener and
carries no special status. The advertised-device pointer dies when the call
returns, so copy what you need.

**What goes out over that advertisement is a contract, not an implementation
detail.** It is specified in `docs/ICD.md`, which is normative for both the
firmware and the phone app, and enforced mechanically by
`tools/check_icd.py`. Changing the frame without changing that document is the
one way to put a confidently wrong number on somebody's phone.

**And never call `NimBLEDevice::getAdvertising()` from anywhere else either.**
`integrations/ble_broadcast.*` owns advertising, for the same reason
`BleScanner` owns scanning: there is exactly one advertiser and whoever
reconfigures it last wins. It advertises non-connectably, which is load-bearing
rather than incidental - see the connection budget below, and
`decisions/0016`.

It keeps scanning while a connection is up, which the controller time-slices:
measured at ~3.8 advertisements/sec with the BMS `online`, with the cooperative
loop unchanged (46,729 loops and 168 flushes per heartbeat against 46,821 and
168 before). A scan costs ~1.5-2 KB of heap while a window is in flight.

**The connection budget is three.** `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` defaults
to 3 and this project does not override it, so the BMS leaves two slots. That
is a hard ceiling on connectable accessories - the lighting plan in
`decisions/0012` spends both of them - and raising it costs RAM. Prefer a
broadcaster over a connectable device wherever the accessory allows it; a
listener on this scanner costs nothing but scan time. **UNVERIFIED:** only one
connection has ever been open on this board at once.

The one-client limit on the JBD BMS is a property of the battery, not of this
radio, and the two are easy to confuse - this project's own documentation
asserted "one BLE connection only" for two days on the strength of it.

Connecting is the exception. NimBLE will not connect while scanning and
`stop()` is asynchronous, so a would-be connector calls `pause()`, waits for
`scanning()` to go false, connects, and calls `resume()` either way. `pause()`
stops the current window immediately rather than letting it expire - a
disconnect that waited out a 5 s window took 6,192 ms to reconnect against
3,492 ms with no scanning at all.

**Pause names its owner.** There is more than one connector - the BMS task
and the lighting task both connect - so `pause()` and `resume()` take an owner
bit (`kPauseBms`, `kPauseLighting`) and the scanner runs only when the mask is
empty. It is a bitmask rather than a reference count on purpose: callers
resume on every pass of their loop without tracking whether they had paused,
and unbalanced `resume()` calls would drive a counter negative and let one
connector cancel another's pause. Setting and clearing a bit is idempotent, so
that cannot happen, and it needs no lock.

**Pause for the attempt, not for the cycle.** A connector that retries must
`resume()` between attempts. The connect itself needs the radio to itself; the
waiting does not, and holding the scanner down across a whole retry cycle -
four attempts at a 5 s timeout - starves every other listener for twenty
seconds. `BleSerialClient` gets this right via `kRetryScanMs`; it did not
until 2026-10-18, and the symptom was temperature sensors going stale whenever
the BMS struggled (`TROUBLESHOOTING.md`).

### Integration - `src/core/integration.h`

Everything that touches hardware. Three rules, and the first one is the one that
actually bites:

1. **`loop()` returns promptly.** There is one cooperative loop and no task per
   device. A blocking call inside `loop()` freezes the UI, the touch input and
   every other integration. This has already happened once and left a blank
   screen for two days - see `TROUBLESHOOTING.md`.
2. **`begin()` does not block forever.** Register entities, start the driver,
   return. Slow discovery belongs in `loop()` as a state machine.
3. **All data leaves through entities.** No integration exposes a getter that a
   screen calls directly.

`LinkState` is the integration's own health - Offline, Searching, Connecting,
Online, Fault - and is separate from entity staleness. A link can be Online
while a particular reading is stale.

**`Lighting` owns up to `CFG_LIGHTING_MAX_FIXTURES` devices**, which makes it
the first integration holding more than one connection, and three rules come
with that:

- **That constant is a connection budget, not a display limit.** Each fixture
  holds a connection for as long as the van is awake - `decisions/0018`'s
  mitigation for a controller class with no access control, and what makes a
  tap instant - so it plus the BMS's one must not exceed
  `CONFIG_BT_NIMBLE_MAX_CONNECTIONS`. `lighting.cpp` static_asserts the two
  against each other rather than trusting a comment.
- **At most one connect attempt per pass of its task, round-robin.** A connect
  pauses the shared scanner and can block for five seconds, so attempting four
  in one pass would take every other listener off the air for twenty - the
  starvation the scanner fix of 2026-10-18 was about. `connectCursor_` is what
  stops one unreachable fixture starving the others.
- **Slot identity lives in NVS.** A fixture's slot is remembered, so "Light 2"
  is the same lamp after a power cut; without it the scanner's arrival order
  would rename every lamp on every boot. Discovery happens on the BLE task and
  the write happens on the main loop - `needsCommit` is the handover, the same
  shape as `EnvSensor::needsNameLoad`, and `Lighting::loop()` is the only
  reason that integration has a non-empty `loop()` at all.

That `loop()` carries a warning worth repeating: **its body is four atomic
read-modify-writes, and running them on every pass cost 14% of the main loop
rate** - 39,115 loops per heartbeat down to 33,729, measured 2026-10-19. It is
gated on a counter now. `millis()` is the usual culprit named in
`ADDING_AN_INTEGRATION.md`; atomics belong on that list too.

**Two integrations own no device**, and they run in opposite directions.
`BleBroadcast` reads the registry and puts it on the radio, registering nothing
(`decisions/0016`). `SystemInfo` fills the registry from the chip - uptime, free
heap and its low water mark, the LVGL pool, the loop and flush rates, the I2C
bus, the broadcast's frame rate, and one `TextEntity` per integration carrying
its link status. Both are Integrations for the same concrete reason: `Hub` gives
them a loop slice and names them if they ever hold it.

`SystemInfo` is added **last** in `setup()`, and that is load-bearing: its
`begin()` walks `Hub` to build a link row for everything already registered, so
an integration added after it gets no row. It also sets its own link to Online
rather than leaving it Offline, because the header reports the first integration
that is not Online and an honest "not applicable" there would mask a real fault
somewhere else.

Its `loop()` is **gated on a counter rather than on the clock**, the same idiom
`main.cpp` uses for `Clock::tick()` and for the same reason: `millis()` is a
64-bit division and the loop turns over ~19,000 times a second, so asking it
every pass was the entire cost of the integration. Measured on hardware
2026-10-18 - 38,289 loops per heartbeat calling `millis()` every pass, 39,115
with the gate, against ~41,000 before `SystemInfo` existed. The flush rate did
not move, which is the number that says the UI is healthy.

One figure there needed its own window. The phone broadcast refreshes every 2 s,
so a one-second sample can only ever see nought or one frame and the row read 0
and 60 alternately; `kFrameWindowMs` is 10 s, which is five frames and a stable
30/min.

### Hub - `src/core/integration.h`

Owns the list, calls `begin()` once and `loop()` forever. Deliberately tiny. It
does no scheduling, no priorities and no error recovery; if an integration needs
those, it implements them itself.

It times each `loop()` and names anyone holding it past `kSlowLoopWarnMs`. That
timing is not free: the loop runs upwards of 20,000 times a second, so it uses
`esp_timer_get_time()` and takes **one reading per boundary rather than two per
integration**. It used to call `millis()` twice per integration, and `millis()`
is `esp_timer_get_time()/1000` - a 64-bit division. Adding a second integration
whose `loop()` does almost nothing cost 12% of the main loop rate, 46,700 to
40,800 per 2 s heartbeat, essentially all of it in the instrumentation rather
than the work. Measured 2026-09-09.

### Settings - `src/core/settings.h`

The only thing in the project that persists anything. NVS through
`Preferences`, with typed accessors for the saved BMS address, brightness and
screen timeout.

**Every write happens on the main loop.** A task hands work over -
`rememberBmsMacFromTask()` fills a slot behind a barrier and raises a flag,
`Settings::loop()` commits it - so flash operations stay on one known context
and no locking is needed. `Settings::loop()` is the first line of `loop()`; skip
it and handovers are silently dropped.

A saved BMS address beats `CFG_BMS_MAC`, making the constant a seed rather than
a commitment. See `decisions/0009`.

It also stores the temperature unit - see **Units** above; the value it writes
lives in `core/units.h` and `Settings` deliberately has no getter for it.

It also stores a display name per BLE sensor, keyed by address, so a sensor can
be called "Fridge" rather than `ATC_1a2b3c`. **NVS keys are capped at 15
characters**, which is why the key is `n` plus the twelve hex digits of the MAC
with its colons stripped - thirteen. A longer key is silently truncated by NVS,
which would quietly make two sensors share a name.

### Alarms - `src/core/alarms.h`

A fixed table of 16, no allocation. Raise and clear by id. Ids are prefixed by
integration (`bms.cell_over`) so a disconnect can drop the whole set with
`clearPrefix()`.

Silencing kills the audible part without clearing the condition, and un-silences
itself once everything clears - so the next fault still gets attention.

**Alarm ids must be string literals with static lifetime.** The table stores the
pointer, and slots are never reclaimed, so ids generated at runtime will both
dangle and exhaust the table.

## The timing model

One cooperative loop, in `src/main.cpp`:

```cpp
void loop() {
  cc::Hub::instance().loop();   // every integration, in order
  serviceButton();
  serviceAlarmOutput();
  cc::ui::tick();               // rate-limited to 4 Hz internally
  bsp::displayLoop();           // lv_timer_handler()
}
```

Healthy is roughly **46,000 iterations/sec and ~48 LVGL flushes/sec**. Those
numbers are the fastest way to tell whether something is blocking: if the loop
count stops climbing, an integration is stuck.

`Hub::loop` times each integration and logs any that exceeds
`Hub::kSlowLoopWarnMs` (50 ms) by name. Rule 1 went unenforced until it was
violated; this is how you find out which integration is at fault rather than
inferring it from a frozen screen.

### The one exception

**`JbdBms` runs on its own FreeRTOS task**, pinned to core 0 while the Arduino
loop runs on core 1. The BLE stack's `connect()` is synchronous and blocked the
cooperative loop for up to six seconds whenever the BMS was absent, which
stopped the touch controller being polled. See `decisions/0008`.

The NimBLE port (`decisions/0010`) did not remove the need for this: connect is
still synchronous in the form we call, and the task now also keeps the BLE work
off the core the UI runs on.

This is the escape hatch, taken once. The cooperative loop remains the default;
a second integration should not copy this by reflex.

It means entities are written from one context and read from another. Aligned
32-bit loads and stores are atomic on this target, so a single reading cannot
tear - what you can see is a set of values from adjacent cycles, which is
invisible at 4 Hz. Anything richer than a scalar needs more care: `Alarms`
publishes a table slot only after filling it, behind a barrier, because
incrementing the count first let a reader reach a slot with a null id.

## What the UI may do

- Read the registry and entity values.
- Read `LinkState` and `Alarms`.
- Call `SwitchEntity::command()`.

That is all. A screen that needs a value it cannot find in the registry is a
sign the integration should publish one, not that the screen should reach
further down.

Page layout constants live in `src/ui/theme.h` and at the top of
`src/ui/ui.cpp`. The screen is 480x480 with a 44px header and a 64px nav bar,
leaving 372px of content.

**Almost every screen is built once in `ui::begin()` and kept for the life of
the device**, overlays included. Refresh functions only change text and colour
on widgets that already exist; a page that rebuilt its children at 4 Hz would
fragment LVGL's pool in minutes. Two consequences follow:

- Rows and tiles are created lazily *as sensors appear* and then reused. The
  click handler is given a pointer to the slot's own `mac` buffer, which is
  stable because the slot array never moves.
- **The LVGL pool has to hold the whole UI at once**, so its size is a function
  of how many screens exist and it grows every time one is added. That is why
  `setup()` logs `lv_mem_monitor()` - exhausting it does not report itself. See
  `decisions/0013`.

**The one exception is the clock setter**, which is built when it is opened and
freed when it is closed (`decisions/0015`). It is the escape hatch `0013` wrote
down for when the pool gets tight, taken once. If you convert another overlay
the same way, the rule that matters is that **every cached handle must be
nulled synchronously in the close path, and the delete must be
`lv_obj_delete_async()`** - it runs from inside a child's event callback.

Measured on hardware 2026-09-17: the pool sat at 16,388 free / 73% used with
everything built, dropped to 10,844 while the clock setter was open, and
returned to 16,296 at 2% fragmentation - unchanged after five open/close
cycles. The resting figure is 14,932 free / 75% used since the System screen
landed; **the ~5,500 bytes the setter takes is the number that matters**, and
it is why `decisions/0023` would not spend the pool on that page.

**The Lighting page is one control set, not one per fixture.** A fixture
picker across the top, then power, brightness and eight colour presets acting
on whichever fixture is selected, with a summary line carrying every fixture's
state. The obvious shape - a row each with a detail overlay - was built first
and measured at **100% pool used, 380 bytes free**; this one is 4,212 bytes
and does not grow with the fixture count. `decisions/0024`.

Two things about it generalise. **`lv_buttonmatrix` is how this project
affords a grid of buttons**: eight presets are one object rather than sixteen,
the same arithmetic that shaped the clock setter. Its map must have static
storage, because the widget keeps the pointer rather than copying it - a stack
array is a dangling read on the first draw. And **`makeCard` shares one
`lv_style_t`** rather than setting five local properties per card; local
styles grow a per-object property array, and sharing bought 560 bytes across
about thirty cards.

**The System screen is two labels**, on an overlay reached from a button in
Settings rather than from the nav bar. One column of names, one of
right-aligned values, one line per entity in `Domain::System`, built from
`Registry::inDomain` so a later integration's diagnostic appears without the UI
changing. It is not the generic card rows the domain pages use, and that is a
measurement: twelve of those rows cost 8.9 KB of the LVGL pool against this
screen's 3.1 KB, and would not have fitted on one screen. `decisions/0023`.

Two consequences. There is no amber for a stale line here, because colour
belongs to a widget and these lines are text inside one. And it refreshes from
`ui::tick()` whenever it is visible rather than from the current page's refresh
- unlike the Climate detail overlay, which is a child of the page that owns it.

**A generic domain row is not cheap**: a styled card and two labels measured
about 740 bytes of the pool on hardware. Worth knowing before putting a dozen
of anything on one of those pages.

`showDomain()` logs the pool on every change of domain, and the reason is that
**the figure `setup()` logs is not the whole UI**: rows on the generic pages
are built on first view rather than at boot, so a page nobody has opened costs
nothing yet. That log is the only thing in the project that can see the
difference, and `decisions/0013` is explicit that exhausting the pool does not
report itself. `showDomain()` also refreshes the page it is about to show
rather than waiting up to a tick for `ui::tick()`, which is what makes the
figure it then logs true.

The Climate page is tiles, two across, scrolling; tapping one opens a detail
overlay with the current readings, today's high and low, and a graph of the last
24 hours. The Rename keyboard is reached from there. Renaming moved off the tile
deliberately: a tap on a reading should show you the sensor, not offer to edit
it.

**Text entry and number entry are different problems.** `showTextEntry()` is one
shared overlay - one `lv_keyboard`, several callers - for things that are
genuinely text: a sensor's name today, Wi-Fi credentials later. Setting the
clock is picking five numbers, and it gets steppers instead; a sixteen-character
date on a touch keyboard was the first attempt and it was not usable. See
`decisions/0015`.

One trap in the shared overlay, because it has already been walked into: the
save callback runs **after** the overlay closes, so `closeEntry()` must not
clear state the callback still needs. It cleared `g_renameMac` once, and every
rename silently did nothing.

Settings is an overlay above the pages and the nav bar, opened by the gear in
the header, rather than a domain of its own - the nav bar is about what the van
can do. See `decisions/0009`. **System is an overlay above Settings** for the
same reason, opened by the View button on its System card: it is a domain with
entities in it, but it is how the van is *doing* rather than what it can do.
`kNavDomains` therefore holds four of the five domains, and that is deliberate
rather than the oversight it used to be - `showDomain()` now logs and refuses a
domain with no page instead of hiding every page and leaving a blank screen.

**The screen timeout switches the backlight fully off, not down.** A dim panel
at 2am in a van is still a light source. One consequence worth knowing:
`touchReadCb` swallows the touch that wakes a dark screen instead of delivering
it, because the crew could not have seen what was under their finger.

**The UI may ask, and may not decide.** Tapping a `SwitchEntity` row calls
`command()`, which hands the request to whoever owns the hardware. It does
**not** set the value. The row only changes when that owner calls `confirm()`,
so a fixture that has gone offline keeps showing what it last actually
reported rather than what somebody wished for. `ui.cpp:onEntityRowClicked` is
the only place the UI issues a command, and it checks `kind()` first because
rows are reused for whatever entity lands at that index.

## Memory

The tight resource is **internal RAM**, not flash. Measured 2026-10-19:
1.71 MB of the 3.3 MB app partition (51.1%), **131.8 KB static RAM (41.2%)**,
and **~61 KB of free heap after boot**. It was 107 KB static and ~97 KB free on
2026-09-07; most of the difference is `LV_MEM_SIZE` going from 48 KB to 64 KB
(`decisions/0013`) plus 2.8 KB of sensor history, and 2.6 KB of it is the
phone broadcast (`decisions/0016`).

Competing for internal RAM: two 38.4 KB LVGL draw buffers, LVGL's own 68 KB
pool (`LV_MEM_SIZE`, grown from 64 KB by `decisions/0025`), the NimBLE host -
whose connection ceiling is five, costing 456 bytes of static RAM and ~2.4 KB
of heap over the default three - the BMS task's 8 KB stack, of which about
2 KB is used, and the lighting task's 4 KB.

With every screen built, the LVGL pool sits at **12,668 bytes free, 80% used,
1% fragmented** (hardware, 2026-10-19, and unchanged by the phone broadcast,
which creates no LVGL objects). It read 13,252 free and 78% used before the
Lighting page, 16,388 and 73% before the System overlay, and 18,940 and 69%
before the clock setter was added; `decisions/0015` is why that screen is built
on demand rather than kept, and why the figure did not go further.

**Two things are not in that number.** Rows on the generic domain pages are
built on first view, so Lighting's cost nothing until somebody opens it; and
the clock setter allocates ~5,500 bytes while it is open. Those two are why
`decisions/0023` rejected the obvious shape for the System screen - it would
have left 6,292 bytes free with the gear reachable from every page.

The worst combination that can actually occur is **the Lighting page with the
clock setter open over it, at 6,884 bytes free and 89% used** - a crew standing
on that page can reach the gear and then Set. That is what `decisions/0025`
sized the pool for: it is the margin that existed before the Lighting page
(6,548 free, 90%), restored rather than improved on. The System overlay cannot
be open at the same time as the setter, because the Set button is on the
Settings card the overlay covers.

### Wi-Fi does not currently fit

Measured on hardware 2026-09-07, with the BMS connected and the UI running:

```
before Wi-Fi        heap 48084   largest block 31732
after WiFi.mode()   heap  7168   largest block  6644
after a scan        heap  3512   largest block  2548
steady state        heap  1972
```

`WiFi.mode(WIFI_STA)` alone costs about **41 KB of internal heap**. Under
Bluedroid that left 1972 bytes, which is not a system you can build on.

**Resolved by `decisions/0010`.** NimBLE returned 40 KB: free heap at boot went
from 56,724 to 97,116, so Wi-Fi's 41 KB now fits. The margin is smaller than
that today - free heap is ~75 KB since `decisions/0013`, so roughly 35 KB to
spare rather than 56 KB.
The measurements above were taken on Bluedroid and are kept because they are
why the port happened.

Radio coexistence was never the problem - the BMS stayed `online` through a
28-network Wi-Fi scan with no loop stalls.

PSRAM is not the answer: it stayed at 7.9 MB free throughout, because the Wi-Fi
and lwIP buffers come from internal RAM. Moving them needs
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`, an sdkconfig option the prebuilt Arduino
libraries do not expose - the same wall as `decisions/0004`.

**So NimBLE is a prerequisite for Wi-Fi, not an optimisation.** It returns
30-40 KB, which is the size of the gap. See `decisions/0003` and `ROADMAP.md`.

## Firmware update, and the trap in front of it

There is no over-the-air update yet, and the reason is a rule rather than
missing work: **the Arduino core disarms the bootloader's rollback protection
before `setup()` runs.** `initArduino()` calls two `__attribute__((weak))`
hooks whose defaults say "this image is fine", so an image that boots and then
boot loops is marked valid and the device is stuck on it.

This project has shipped exactly that image twice - `decisions/0013` and
`decisions/0014` both crashed *after* `setup()` began - so this is a
demonstrated failure, not a theoretical one.

**Any OTA work must override those hooks first**, and mark the image valid
from the main loop only once the device has proved itself. The health signal
already exists: `displayLoop`'s heartbeat with `loops` and `flushes` both
climbing, which is the same definition of alive the debugging protocol in
`CLAUDE.md` uses. `decisions/0020` has the mechanism, the flash-write hazard
at scale, and the transport comparison.

## `src/core/` runs on a laptop

`entity.cpp`, `registry.cpp`, `alarms.cpp`, `bthome.cpp`,
`bthome_encode.cpp`, `switchbot.cpp`, `sp107e.cpp`, `light_presets.cpp`,
`units.cpp`, `history.cpp`, `clock.cpp` and `datalog.cpp` contain no
hardware. That is not an
accident of how they were written - it is a constraint, and
`env:native` in `platformio.ini` enforces it:

```
pio test -e native        # ~6 s, 246 cases, no board
```

Those twelve files are the only ones the test environment compiles. Anything
they include has to exist on a host compiler, so **a `#include` of a driver, a
bus or NimBLE inside `src/core/` breaks the tests by construction** - which is
the point. The rule is the whole reason this layer can be trusted while
integrations churn around it.

Two things make it work:

- **`test_shim/Arduino.h`** supplies the only two things the core actually
  wants from Arduino: `millis()` and the `log_*` macros. Its clock is fake and
  settable, because `Entity::stale()` is a function of elapsed time and a test
  that sleeps 15 real seconds to check a 15-second threshold is a test nobody
  runs.
- **`Registry::testReset()` and `Alarms::testReset()`** are compiled only under
  `-DCC_NATIVE_TEST`, which only `env:native` defines. Both types are
  singletons, so without a way back to empty each test case would inherit the
  last one's state. The firmware binary is byte-identical with and without them.

The Climate page reads `EnvSensors` directly rather than through the registry, for the same reason the cell screen asks the BMS directly: it is a
variable-length array of like things, and the entity model deliberately does
not carry arrays. It is the same exception, not a new one - and the reason
there is no `Registry::remove()` to worry about when a sensor slot is reused.

`bthome.cpp` and `switchbot.cpp` are here for the same reason: decoding an
advertisement is pure byte manipulation, and a wrong byte offset does not crash
anything - it puts a plausible, wrong temperature on the screen, which is the
worst failure mode available. `units.cpp` and `history.cpp` are the same
argument one step further on: a flipped conversion or an off-by-one in a ring
buffer also draws a plausible, wrong picture rather than failing. `BleScanner` hands them the bytes; neither
touches a radio itself.

`settings.cpp` is deliberately outside all of this. It needs `Preferences`,
which is NVS, which is the board. `bsp/storage.cpp` is outside it for the same
reason - which is why `datalog.cpp` holds only the text and the arithmetic, and
the filesystem calls live in `bsp/`.

CI runs the tests before the firmware build - they are 15 s against several
minutes, so a broken core fails while you are still watching.

## Where to extend

- **A new accessory** -> `ADDING_AN_INTEGRATION.md`. One file, one line in
  `setup()`.
- **A new page** -> add to `Domain`, build it in `src/ui/ui.cpp`, add it to
  `kNavDomains` and bump `kNavCount`. A `static_assert` holds those two
  together. If the domain is not something the van *does*, it probably wants
  an overlay off Settings instead, the way System does.
- **A new diagnostic** -> register a `Domain::System` entity from
  `SystemInfo`, or from whichever integration knows the figure. The System
  overlay is built from the registry, so it needs no UI change.
- **A new board** -> `include/board_indicator_d1.h` is the only file that should
  know pin numbers.
