# Roadmap

Last reviewed 2026-09-07.

## Where it actually is

Runs on hardware. Boots, drives the panel, reads touch, and carries a Settings
overlay whose values survive a power cycle. Flash 61.5%, static RAM 37.5%,
~55 KB free heap. Main loop ~46k iterations/sec, LVGL ~48 flushes/sec, and no
integration holds the loop.

The BMS connects on the remembered address and reaches `online`, so the live
pack display works. **There are no blockers left.** What remains is the
difference between a working device and one worth living with.

## Blockers

None. The device boots, drives the panel, reads touch, connects to the battery
and remembers its settings.

## Design gaps

**2. `Domain::System` has no page.** The enum and `domainName()` know about it;
`kNavDomains` does not. Anything registered there - uptime, heap, link health,
exactly the diagnostics that would have shortened this week - is silently
invisible. A trap for the next integration author.

**3. Wi-Fi backhaul is not configurable.** `core/settings.*` and the Settings
overlay exist now, and the Settings page carries a placeholder row, but nothing
scans, joins or stores a network. Needs an SSID list and an on-screen keyboard,
and it is worth deciding what the backhaul actually talks to before building the
plumbing - that destination is still undefined.

Note NVS on this board is not encrypted, so a Wi-Fi password stored there is
readable by anyone who can dump the flash. See `decisions/0009`.

**4. No tests, no CI.** `src/core/` - entity formatting, registry lookup, alarm
severity and silencing - is pure logic with no hardware dependency. A PlatformIO
`platform = native` environment would test it on a laptop in seconds. For a
codebase whose premise is "adding integrations should be safe", this is the
highest-leverage missing piece.

**5. The arduino-cli harness is dead weight.** Linux-only, already drifted, and
looks maintained. Delete it or put it in CI. See `decisions/0006`.

## Smaller things

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
- **Free heap is down to ~55 KB** from ~67 KB, the cost of the BMS task's 8 KB
  stack. `taskLoop()` reports its high-water mark every 30 s; tune the size
  against that rather than leaving the guess in place. NimBLE would return
  30-40 KB - see `decisions/0003`.

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

1. Add the `native` test environment (4). Cheapest insurance before growth, and
   nothing else on this list gets safer without it.
2. Add the System page (2), and move the heartbeat and bus scan into it - they
   are the numbers that diagnosed most of this week, and they are only visible
   over a serial cable.
3. Wi-Fi backhaul (3). Decide what it talks to before building the plumbing.
4. Resolve the build-system split (5).
5. NimBLE, which is now the answer to two problems: the heap the BMS task took,
   and Bluedroid's synchronous connect.

1-2 make it safe to change. 3-5 make it a product.
