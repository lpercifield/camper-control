# 0008 - The BMS integration owns a FreeRTOS task

- **Date:** 2026-09-07
- **Status:** Accepted

## Context

Touch worked but taps were intermittently ignored. Instrumenting `Hub::loop`
with a per-integration timer named the cause immediately:

```
integration 'JBD BMS' held the loop for 3731 ms
integration 'JBD BMS' held the loop for 6010 ms
```

Bluedroid's `pClient->connect()` is synchronous. With the BMS absent, every
retry blocked the cooperative loop for 1.5-6 seconds. `Touch::read()` is called
from LVGL's input timer inside that same loop, so its rate collapsed from 28/s
to 3/s and taps landed while nothing was polling the digitiser.

`decisions/0001` chose one cooperative loop and wrote down the escape hatch:
anything that genuinely must block gets its own task and publishes through
entities. This is the first thing that genuinely must block.

## Decision

`JbdBms` creates a FreeRTOS task in `begin()` and its `Integration::loop()` is
empty. The task is pinned to **core 0**; the Arduino loop, and therefore LVGL
and the touch input, runs on core 1, so a blocking BLE call cannot stall the UI
even in principle. Data reaches the rest of the system through entities exactly
as before - the interface did not change, only which context fills it.

Measured before and after, with BLE failing at the same rate in both:

| | Before | After |
|---|---|---|
| Loop stalls in 30 s | 5, of 1.5-6 s | 0 |
| Touch reads | collapsing to 3-6/s | 28/s, every window |

## Alternatives rejected

**Back the retry interval off toward 30 s.** A few lines and very low risk, but
it makes the freeze rarer rather than absent - a 1.5-6 s dead UI still happens,
just less often - and leaves a documented architecture rule violated.

**Move to NimBLE**, whose connect can be asynchronous. It also frees 30-40 KB,
which this board could use. Rejected as the fix for *this* problem because it
changes every callback signature in the vendored BLE client at the same time as
diagnosing a UI fault. It remains the right move later; see `decisions/0003`.

## Consequences

Entities are now written from one context and read from another. On this 32-bit
target, aligned loads and stores of `float` and `uint32_t` are atomic, so an
individual reading cannot tear. What can happen is that the UI renders a set of
values from adjacent refresh cycles, which is invisible on a display updating at
4 Hz.

`Alarms::findOrCreate` needed a real fix: it published a slot by incrementing
`count_` *before* setting the id, so a concurrent reader could reach a slot
whose id was still null and dereference it in `strcmp`. The slot is now filled
completely, then a barrier, then the count.

The task costs 8 KB of stack. Free heap went from ~67 KB to ~55 KB, which is
material on this board - `taskLoop()` reports its high-water mark every 30 s so
the size can be tuned against evidence rather than left at a guess.

**This is now the only place in the project where two contexts touch shared
state.** A second such integration should not copy this by reflex; the
cooperative loop is still the default, and this is the exception that had to be
made.
