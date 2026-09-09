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

One reading, one flag, one switch. `NumericEntity`, `BinaryEntity`,
`SwitchEntity`. Every entity carries its own update time, so staleness is a
property of the data rather than something each screen has to track.

- Ids are `domain.thing` and globally unique. `Registry::add` drops duplicates
  with a warning rather than overwriting.
- `format(buf, len)` renders for display and is the **only** correct way to turn
  a value into text. It uses `snprintf`; see `decisions/0005`.
- `SwitchEntity` separates `command()` from `confirm()`. State changes when the
  hardware agrees, not when we ask. A relay node that has gone offline shows the
  truth.

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
radio, and the two are easy to confuse - `ROADMAP.md` asserted "one BLE
connection only" for two days on the strength of it.

Connecting is the exception. NimBLE will not connect while scanning and
`stop()` is asynchronous, so a would-be connector calls `pause()`, waits for
`scanning()` to go false, connects, and calls `resume()` either way. `pause()`
stops the current window immediately rather than letting it expire - a
disconnect that waited out a 5 s window took 6,192 ms to reconnect against
3,492 ms with no scanning at all.

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

Settings is an overlay above the pages and the nav bar, opened by the gear in
the header, rather than a fifth domain - the nav bar is about what the van can
do. See `decisions/0009`.

**The screen timeout switches the backlight fully off, not down.** A dim panel
at 2am in a van is still a light source. One consequence worth knowing:
`touchReadCb` swallows the touch that wakes a dark screen instead of delivering
it, because the crew could not have seen what was under their finger.

## Memory

The tight resource is **internal RAM**, not flash. Measured 2026-09-07 after
the NimBLE port: 1.6 MB of the 3.3 MB app partition, 107 KB static RAM, and
~97 KB of free heap after boot.

Competing for internal RAM: two 38.4 KB LVGL draw buffers, LVGL's own 48 KB
pool (`LV_MEM_SIZE`), the NimBLE host, and the BMS task's 8 KB stack - of which
about 2 KB is actually used, so it is oversized on purpose while there is room.

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
from 56,724 to 97,116, so Wi-Fi's 41 KB now fits with roughly 56 KB to spare.
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

## `src/core/` runs on a laptop

`entity.cpp`, `registry.cpp`, `alarms.cpp` and `bthome.cpp` contain no
hardware. That is not an accident of how they were written - it is a
constraint, and `env:native` in `platformio.ini` enforces it:

```
pio test -e native        # ~3 s, 73 cases, no board
```

Those four files are the only ones the test environment compiles. Anything
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

The Climate page reads `BtHomeSensors` directly rather than through the
registry, for the same reason the cell screen asks the BMS directly: it is a
variable-length array of like things, and the entity model deliberately does
not carry arrays. It is the same exception, not a new one - and the reason
there is no `Registry::remove()` to worry about when a sensor slot is reused.

`bthome.cpp` is here for the same reason: decoding a BTHome v2 advertisement is
pure byte manipulation, and a wrong byte offset there does not crash anything -
it puts a plausible, wrong temperature on the screen, which is the worst
failure mode available. `BleScanner` hands it the bytes; it never touches a
radio itself.

`settings.cpp` is deliberately outside all of this. It needs `Preferences`,
which is NVS, which is the board.

CI runs the tests before the firmware build - they are 15 s against several
minutes, so a broken core fails while you are still watching.

## Where to extend

- **A new accessory** -> `ADDING_AN_INTEGRATION.md`. One file, one line in
  `setup()`.
- **A new page** -> add to `Domain`, build it in `src/ui/ui.cpp`, add it to
  `kNavDomains`. Note that `Domain::System` exists today with no page, so
  anything registered there is invisible.
- **A new board** -> `include/board_indicator_d1.h` is the only file that should
  know pin numbers.
