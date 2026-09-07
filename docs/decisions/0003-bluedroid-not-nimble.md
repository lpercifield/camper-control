# 0003 - Bluedroid rather than NimBLE, for now

- **Date:** 2026-09-06 (recorded; decided during the port, 2026-09-02)
- **Status:** Accepted

## Context

The vendored `BleSerialClient` is written against Bluedroid. NimBLE would free
roughly 30-40 KB of RAM, which matters on a board with ~65-70 KB of free heap
after boot.

## Decision

Stay on Bluedroid until the port is proven on hardware.

## Alternatives rejected

**Switch to NimBLE during the port.** It changes every callback signature in
`BleSerialClient`. Doing it at the same time as the display bring-up, the entity
rework and the core bump would have made any failure ambiguous - and there were
five separate failures to diagnose before the first screen appeared.

## Consequences

Memory stays tight; see `ROADMAP.md`. Bluedroid's `connect()` is also
synchronous and costs about two seconds on failure, which stutters the
cooperative loop on every retry when the BMS is absent.

Revisit once the BMS link is reliably online, or as soon as a second BLE device
is needed - that is the point where the RAM has to come from somewhere.
