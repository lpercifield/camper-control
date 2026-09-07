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

There are no FreeRTOS tasks of our own. Anything that genuinely must block gets
its own task and publishes back through entities - that is the escape hatch, and
it has not been needed yet.

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

## Memory

The tight resource is **internal RAM**, not flash. At the time of writing: 2.0 MB
of the 3.3 MB app partition, 123 KB static RAM, and roughly 65-70 KB of free
heap after boot.

Competing for internal RAM: two 38.4 KB LVGL draw buffers, LVGL's own 48 KB
pool (`LV_MEM_SIZE`), and Bluedroid. Adding a second BLE device or a large new
screen will need one of them to give ground - see `ROADMAP.md`.

## Where to extend

- **A new accessory** -> `ADDING_AN_INTEGRATION.md`. One file, one line in
  `setup()`.
- **A new page** -> add to `Domain`, build it in `src/ui/ui.cpp`, add it to
  `kNavDomains`. Note that `Domain::System` exists today with no page, so
  anything registered there is invisible.
- **A new board** -> `include/board_indicator_d1.h` is the only file that should
  know pin numbers.
