# Roadmap

Last reviewed 2026-09-07.

## Where it actually is

Runs on hardware. Boots, drives the panel, connects to the JBD BMS over BLE and
displays live pack data. Flash 61%, static RAM 37.5%, ~65-70 KB free heap. Main
loop ~46k iterations/sec, LVGL ~48 flushes/sec.

Not yet usable unattended: **touch does not work**, so the four-domain
navigation cannot be driven.

## Blockers

**1. No version control.** This is not a git repository. A week of bring-up -
seven firmware fixes, a build-system change, a documentation set - exists only
as files in a Dropbox folder, with no history and no way to bisect a regression.
Every other item on this list is harder than it needs to be until this is fixed,
and one bad sync loses the lot. `git init` and a first commit is minutes of
work.

**2. Touch controller: the pin map is wrong.** Not a timing or reset problem.
Three experiments on 2026-09-06 established it:

- Bus scan finds `0x20` (the expander) and `0x48`. There is **no `0x38`**.
- The expander answers, and `EXP_TOUCH_INT` idles high, so the expander path is
  sound.
- Driving *every* expander pin high - in case the real reset line was floating
  on a pin we never claimed - changed nothing.

So the controller is either not an FT6336, or not on this bus.
`board_indicator_d1.h` says its pin map came from community sources, and this is
the part that did not survive contact.

`0x48` is unidentified and reads as twelve stable zero bytes, which is what a
capacitive touch controller reports with nothing touching it. **Next step: touch
the screen while the firmware watches `0x48` for changes.** The probe is in
`display.cpp:probeUnknown` and logs only on change, so the touch and the capture
do not have to be synchronised.

**3. `CFG_BMS_MAC` is empty.** The firmware attaches to the first device
advertising service `ff00`. Fine on a bench, wrong in a campground. Needs the
address, which the log prints once connected.

## Design gaps

**4. `Domain::System` has no page.** The enum and `domainName()` know about it;
`kNavDomains` does not. Anything registered there - uptime, heap, link health,
exactly the diagnostics that would have shortened this week - is silently
invisible. A trap for the next integration author.

**5. No persistence.** Every setting is a compile-time constant, so changing the
BMS MAC means a reflash. NVS-backed settings behind a Settings page is the
largest gap between "working firmware" and "thing you live with", and wants
designing before more constants accumulate.

**6. No tests, no CI.** `src/core/` - entity formatting, registry lookup, alarm
severity and silencing - is pure logic with no hardware dependency. A PlatformIO
`platform = native` environment would test it on a laptop in seconds. For a
codebase whose premise is "adding integrations should be safe", this is the
highest-leverage missing piece.

**7. The arduino-cli harness is dead weight.** Linux-only, already drifted, and
looks maintained. Delete it or put it in CI. See `decisions/0006`.

## Smaller things

- **Bring-up scaffolding is still in `src/bsp/display.cpp`**: the loop/flush
  heartbeat, `scanI2C`, the expander sweep and the `0x48` probe. All of it
  earned its place during bring-up and all of it is temporary. The heartbeat
  and the I2C scan are worth keeping in some form - the natural home is the
  System page (4). The sweep and the probe come out once touch is solved.
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
  sets `active = false`. Fine while ids are static literals, which they must be
  anyway - but the constraint should be stated where `raise()` is declared.
- **Bluedroid `connect()` is synchronous**, costing ~2s per failed retry and
  stuttering the loop whenever the BMS is absent.

## Done

- **Arduino_GFX bounce-buffer patch made durable** (2026-09-07).
  `tools/patch_gfx.py` applies it as a PlatformIO `pre:` script; verified by
  deleting the installed library and rebuilding from clean. See
  `decisions/0004` and `decisions/0007`.

## Known and accepted

No audible alarm - the buzzer is on the RP2040 and needs the UART link.
The RP2040 is otherwise idle. One BLE connection only. All recorded in
`PORTING_NOTES.md` and `decisions/0003`.

## Suggested order

1. `git init` (1). Everything below is safer with history.
2. Identify `0x48` and get touch working (2). Without it there is no UI.
3. Set `CFG_BMS_MAC` (3). One line, once the address is known.
4. Add the `native` test environment (6). Cheapest insurance before growth.
5. Add the System page (4), and move the heartbeat and bus scan into it.
6. Resolve the build-system split (7).
7. NVS-backed settings (5).
8. NimBLE, once the port is proven and the RAM is needed.

1-3 make the device work. 4-6 make it safe to change. 7-8 make it a product.
