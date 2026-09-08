# 0009 - Settings live in NVS, written only from the main loop

- **Date:** 2026-09-07
- **Status:** Accepted

## Context

Every tunable was a compile-time constant, so changing the BMS address, the
brightness or the screen timeout meant a reflash. `ROADMAP.md` called this the
largest gap between working firmware and something you live with.

Two constraints shape the answer. NVS writes touch flash, and a flash write
disables the instruction cache - the same mechanism behind the boot loop in
`decisions/0004`. And since `decisions/0008` the BMS runs on its own task, so
the obvious place to save a newly connected BMS address is a context that must
not be writing flash.

## Decision

`cc::Settings` wraps NVS through `Preferences` with typed accessors, and is the
only thing in the project that persists anything.

**All writes happen on the main loop.** A task that wants to save something
hands it over - `rememberBmsMacFromTask()` copies into a slot, issues a barrier
and sets a flag; `Settings::loop()` picks it up and commits. One writer, no
locking, and flash operations stay on a known context.

Stored today: the last BMS address, brightness, and the screen timeout, where
zero means never dim.

The saved BMS address takes precedence over `CFG_BMS_MAC`, which turns that
constant from a commitment into a seed. Forgetting the address in Settings
returns the firmware to attaching to the first JBD BMS it hears.

## Alternatives rejected

**A mutex around the settings object.** Correct, and unnecessary: the only
cross-context write is a 17-byte address that changes about once in the life of
the installation. A handover slot is cheaper to reason about than a lock, and
it keeps every flash operation on one context by construction.

**Writing brightness on every slider event.** The obvious wiring, and it would
put hundreds of NVS writes through a single sweep of the slider. Brightness is
applied live on `VALUE_CHANGED` so it can be judged by eye, and committed only
on `RELEASED`.

**Settings as a fifth nav button.** `Domain::System` exists and has no page, so
it was the tidy place to put this. Rejected because the nav bar is about what
the van can do - lights, water, climate - and settings is not an accessory.
It is a gear in the header opening a full-screen overlay. Note this leaves
`Domain::System` still unused; see `ROADMAP.md`.

## Consequences

Wi-Fi credentials will land here too, which is the point at which this file
starts holding a secret. NVS on this board is not encrypted, so a password
stored here is readable by anyone who can dump the flash. That is worth stating
plainly before it is written, not after.

`Settings::loop()` must be called from `loop()` or a handover is never
committed - it is the first line of `main.cpp`'s loop for that reason.
