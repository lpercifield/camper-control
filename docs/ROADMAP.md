# Roadmap

Last reviewed 2026-09-07.

## Where it actually is

Runs on hardware. Boots, drives the panel, reads touch, and carries a Settings
overlay whose values survive a power cycle. Flash 49.2%, static RAM 32.6%,
**~97 KB free heap** since the NimBLE port. Main loop ~46k iterations/sec, LVGL ~48 flushes/sec, and no
integration holds the loop.

The BMS connects on the remembered address and reaches `online`, so the live
pack display works. **There are no blockers left.** What remains is the
difference between a working device and one worth living with.

## Blockers

None. The device boots, drives the panel, reads touch, connects to the battery
and remembers its settings.

## Design gaps

**2. No shared BLE scanner.** A BLE temperature sensor - indoor and outdoor is
the next feature - needs advertisements, but `BleSerialClient` owns the scanner
outright and stops it while connected, so a second consumer receives nothing.
`NimBLEDevice::getScan()` needs to become shared infrastructure that dispatches
advertisements to registered listeners, with the BLE client as one of them.
Prefer broadcast sensors over connectable ones; a connectable sensor competes
for a connection slot, a broadcasting one costs nothing but scan time.

**3. `Domain::System` has no page.** The enum and `domainName()` know about it;
`kNavDomains` does not. Anything registered there - uptime, heap, link health,
exactly the diagnostics that would have shortened this week - is silently
invisible. A trap for the next integration author.

**4. Wi-Fi backhaul is not configurable.** The memory objection is gone: Wi-Fi
costs ~41 KB and there are now ~97 KB free, so it fits with room to spare, and
coexistence was never the problem. What remains is the work - an SSID scan, an
on-screen keyboard, credential storage - and one decision: **what the backhaul
actually talks to.** MQTT is the obvious default and pairs with Home Assistant,
but nothing should be built until that is settled.

Note NVS is not encrypted, so a stored Wi-Fi password is readable by anyone who
can dump the flash. See `decisions/0009`.
 `core/settings.*` and the Settings
overlay exist now, and the Settings page carries a placeholder row, but nothing
scans, joins or stores a network. Needs an SSID list and an on-screen keyboard,
and it is worth deciding what the backhaul actually talks to before building the
plumbing - that destination is still undefined.

Note NVS on this board is not encrypted, so a Wi-Fi password stored there is
readable by anyone who can dump the flash. See `decisions/0009`.

**5. No tests, no CI.** `src/core/` - entity formatting, registry lookup, alarm
severity and silencing - is pure logic with no hardware dependency. A PlatformIO
`platform = native` environment would test it on a laptop in seconds. For a
codebase whose premise is "adding integrations should be safe", this is the
highest-leverage missing piece.

**6. The arduino-cli harness is dead weight.** Linux-only, already drifted, and
looks maintained. Delete it or put it in CI. See `decisions/0006`.

## Smaller things

- **`WiFi.h` is included for a single line.** `bms_jbd.cpp` pulls in the whole
  Wi-Fi and Networking stack purely to call `WiFi.macAddress()` for the BLE
  device name, and that include is the only reason `platformio.ini` carries the
  `Network` lib_deps and `-I` workaround. `NimBLEDevice::getAddress()` returns
  the same thing. Removing the include should let the workaround go with it -
  worth doing before Wi-Fi lands for real, so the dependency is deliberate
  rather than accidental.
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
The RP2040 is otherwise idle. One BLE connection only. All recorded in
`PORTING_NOTES.md` and `decisions/0003`.

## Suggested order

1. Add the `native` test environment (5). Cheapest insurance before growth, and
   nothing else on this list gets safer without it.
2. Shared BLE scanner (2), then the indoor and outdoor temperature sensors on
   top of it. NimBLE makes this tractable; on Bluedroid it was not.
3. Add the System page (3), and move the heartbeat and bus scan into it - they
   are the numbers that diagnosed most of this week, and they are only visible
   over a serial cable.
4. Wi-Fi backhaul (4). Decide what it talks to before building the plumbing.
5. Resolve the build-system split (6).

1-2 make it safe to change. 3-5 make it a product.
