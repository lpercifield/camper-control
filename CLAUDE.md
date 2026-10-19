# Working on Camper Control

Operational context for Claude Code. `docs/` explains the project to a human;
this file is the stuff an agent needs loaded before touching anything.

## Commands

```
pio test -e native                                       # 232 cases, ~5 s
pio run                                                  # compile
pio run -t upload --upload-port /dev/cu.usbserial-1110   # flash
$(ls /opt/homebrew/Cellar/platformio/*/libexec/bin/python | head -1) \
    tools/readlog.py /dev/cu.usbserial-1110 --seconds 15 # read the log
./android/build.sh                                       # the phone app's APK
ANDROID_SERIAL=1C241FDEE006MY ./android/build.sh install # onto the dev phone
python3 tools/check_icd.py                               # firmware/app contract
```

**Name the phone too.** The development phone is a **Pixel 6 Pro, serial
`1C241FDEE006MY`** (Android 17, SDK 37) and is the one to install on. The
owner's main phone may also be attached and is loaded by hand, so `adb` with
two devices present fails rather than guessing. `adb` reads `ANDROID_SERIAL`,
so exporting it once covers every call in a session.

**`docs/ICD.md` is the contract between the firmware and the phone app**, and
it is normative - a wire-format change recorded anywhere else drifts. The two
halves it compiles are `include/icd.h` and `Icd.java`;
`tools/check_icd.py` holds them to each other and the pre-commit hook runs it,
including when `SKIP_DOC_CHECK=1` is set. The advertised BLE name is protocol
(it carries the version), which is why it is not in `config.h`.

The Android monitor app in `android/` builds straight from the SDK tools with
no Gradle - it has no dependencies and no resource files. `android/README.md`
says why, and carries the two traps that cost time there (this repository's
path has spaces in it, which breaks `javac $(find ...)`).

**Always name the upload port.** The board exposes two: the CH340
(`1a86:7523`, `/dev/cu.usbserial-*`) is the ESP32-S3; the Seeed device
(`2886:0050`, `/dev/cu.usbmodem*`) is the RP2040 and is not what you want.
Auto-detect picks whichever it finds first.

`upload_speed` is 460800 and must stay there - the CH340 goes silent at 921600.

## Traps that have already cost time

- **Do not `cat` the serial port.** macOS resets termios on open, so a preceding
  `stty` is discarded and you get garbage. Use `tools/readlog.py`.
- **`pio device monitor` cannot run non-interactively** - miniterm needs a TTY.
- **Opening the port resets the board**, dropping any BLE session.
- **`tools/build.sh` is Linux-only** and already out of step with
  `platformio.ini`. Never use it to check a change on this machine.
- **`pio run | tail` hides the exit code.** Capture to a file and check `$?`.
- **`env:native` has no `main()`**, so it cannot be built as a plain env - only
  run as a test. `default_envs = indicator_d1` is what keeps bare `pio run`
  from trying and failing. Ask for the tests by name: `pio test -e native`.
- **Do not read or write a file while the display is running.** LittleFS,
  SPIFFS, anything on the flash filesystem: the RGB panel's VSYNC interrupt
  calls a flash-resident GDMA function during the cache-disabled window of any
  flash access, and the board boot loops. `CFG_LOG_TO_FLASH` is 0 for this
  reason. Mounting before `displayBegin()` is the only safe window. NVS writes
  are the same hazard with a shorter window - do not add more.
  `docs/decisions/0014`.
- **The microSD slot is wired to the RP2040, not the ESP32-S3.** Probed; GPIO 47
  floats. `EXP_SD_CS` in the pin map leads nowhere. `docs/HARDWARE.md`.
- **A boot loop right after adding a screen is the LVGL pool, not your screen.**
  It surfaces as `StoreProhibited` at `EXCVADDR 0x00000000` inside
  `lv_draw_sw_mask_radius_init`, drawing a widget you did not touch - LVGL
  `lv_malloc`s an arc's mask at draw time and never checks it. `setup()` logs
  `lvgl pool: free ... used ...%`; read that before theorising. Every screen is
  built once and kept, so the pool holds the whole UI. `docs/decisions/0013`.
- **Never flip an LVGL config flag to fix a string.** `LV_USE_FLOAT` also
  retypes `lv_value_precise_t` across LVGL's geometry headers. Format floats
  with `Entity::format()` or `snprintf`. See `docs/decisions/0005`.

## Debugging protocol

The rule this project learned the hard way: **get evidence before forming a
theory, and never state a cause you have not observed.**

1. **Is the loop running?** `displayLoop` logs `hb: loops=... flushes=...`.
   Both are **cumulative counters printed every 2 s**, so read the delta
   between two lines, not the number itself. Measured on hardware 2026-10-18:
   healthy is **~39,100 loops and 168 flushes per heartbeat** on the Power
   page, i.e. ~19.6k loops/sec and 84 flushes/sec. If `loops` is stuck, an
   integration is blocking and nothing else you observe means anything yet.

   **That baseline dropped on purpose and is not a regression.** It was
   ~46,800 loops on 2026-09-09, ~41,000 before the System screen. The phone
   broadcast costs about 9% of the loop's spare headroom (`decisions/0016`)
   and `SystemInfo` about 5%, while **the flush rate has not moved once** -
   that is the number that says the UI is healthy. Comparing loops against an
   old figure is how you spend an afternoon chasing nothing. Setting
   `CFG_BLE_BROADCAST` to 0 returns it to ~45,700.

   The loop rate is also **higher on a quiet screen**: ~23k/sec with the
   System overlay up, which flushes 56/sec rather than 84. Compare like
   with like.

   All of these are on the System screen too - the gear, then View on the
   System card - so a van on a driveway needs no cable. The heartbeat stays
   because the screen being blank is exactly when you need it.
2. **Is the panel alive?** `g_gfx->fillScreen(RED); delay(1000);` right after
   `g_gfx->begin()` separates a dead panel from a silent LVGL.
3. Only then reason about the diff.

Two days went into a blank screen that was diagnosed by adding the heartbeat -
after two wrong theories reasoned from the diff alone. Instrument first.

Do not claim a fix works on hardware without a log showing it. "Should now
work" is fine; "this is fixed" needs evidence.

## Documentation

`CONTRIBUTING.md` is the standard and takes precedence over habit - it also
carries the review flow: branch, test, verify on hardware, PR, merge on green
CI. Its routing rule decides where a new fact goes: board facts to
`HARDWARE.md`, rules code
must obey to `ARCHITECTURE.md`, diagnosed failures to `TROUBLESHOOTING.md`
(indexed by *symptom*), patches to other people's code to `PORTING_NOTES.md`,
rejected alternatives to `decisions/`, unfinished work to `ROADMAP.md`.

A change is not done until the documents reflect it. Decision records are
immutable - supersede, do not rewrite, and remember to mark the superseded one.

The commit hook checks that you touched *a* document, not that what is there is
still true. Documents drift anyway; re-read them against the code periodically.

## Git

- **Commits are authored by the repository owner alone.** Do not add
  `Co-Authored-By`, `Generated with`, or any other attribution trailer.
- Do not commit or push unless asked.
- `.pio/` is ignored: ~230 MB of regenerable build output and packages.
- **A commit touching `src/`, `include/`, `lib/`, `tools/` or `platformio.ini`
  must touch a document too.** `.githooks/pre-commit` enforces it and prints
  the routing rule. Use `SKIP_DOC_CHECK=1 git commit` only when the change
  genuinely documents nothing, and say why in the commit message.
- When a change closes a `ROADMAP.md` item, **move it to that file's `Done`
  section with the date** rather than deleting the line, and renumber what is
  left - stale numbering is how the roadmap stopped being trustworthy before.
- After a fresh clone: `git config core.hooksPath .githooks`.

## Architecture in one paragraph

Integrations own devices and publish entities into a registry; screens read the
registry and never touch hardware. One cooperative loop, so **any blocking call
inside an integration freezes the whole UI** - that has happened and it presents
as a blank screen with no crash. `Hub::loop` now logs any integration that holds
the loop past 50 ms, by name.

One exception: `JbdBms` owns a FreeRTOS task on core 0, because the BLE connect
is synchronous (`decisions/0008`). That makes entities cross-context; all NVS
writes still happen on the main loop via `Settings`. BLE is NimBLE, not
Bluedroid (`decisions/0010`).

Full contracts in `docs/ARCHITECTURE.md`; adding an accessory is
`docs/ADDING_AN_INTEGRATION.md`.
