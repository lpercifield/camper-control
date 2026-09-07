# 0001 - One cooperative loop, data through entities

- **Date:** 2026-09-06 (recorded; decided during the port, 2026-09-02)
- **Status:** Accepted

## Context

The firmware this grew out of, `esp32-smartBMSdisplay`, owned the CPU. It opened
with `while (!SerialBT.connected()) SerialBT.bleLoop();` and then span in a loop
that only ever drew an OLED. That works for one device and one screen.

This device has a touchscreen and four domains, and will accumulate a relay
board, tank senders and a heater. Any one of them stalling must not freeze the
others or the UI.

## Decision

One cooperative loop in `main.cpp`. Every device is an `Integration` that gets a
slice of `loop()` and must return promptly. Integrations publish `Entity` values
into a `Registry`; screens read the registry and never call a driver.

## Alternatives rejected

**A FreeRTOS task per integration.** The obvious embedded answer, and it would
remove the "must not block" rule. Rejected because it buys concurrency bugs -
locking around the registry, LVGL's own thread-safety rules - to solve a problem
that does not exist yet at four devices. The escape hatch is still there: an
integration that genuinely must block gets its own task and publishes results
back through entities.

**Letting screens call drivers directly.** Simpler for one device, and it is
what the original did. Rejected because it makes every new accessory a change to
the UI as well.

## Consequences

The "must not block" rule is load-bearing and unenforced by the compiler. It has
already been violated once, by a vendored BLE client that hung the loop and
blanked the screen for two days - see `PORTING_NOTES.md` patch 6 and
`TROUBLESHOOTING.md`.

Worth revisiting if a device appears whose protocol cannot be driven as a state
machine, or if the loop budget stops being comfortable.
