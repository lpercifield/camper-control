# Camper Control

Vehicle accessory control on the [Seeed SenseCAP Indicator D1](https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html)
(ESP32-S3 + RP2040, 4" 480x480 touchscreen).

It started as the JBD / Overkill Solar BMS monitor from
`esp32-smartBMSdisplay`, moved onto the Indicator's screen. It now runs on
hardware and does rather more than that:

- **Battery.** A JBD BMS over BLE, connecting on a remembered address: state of
  charge, power, current, voltage, per-cell voltages, temperature and the
  MOSFET states, with alarm thresholds read from the BMS itself.
- **Climate.** Any BTHome v2 broadcaster and the SwitchBot W3400010, on a page
  of tiles with 24-hour graphs, daily highs and lows and a global °C/°F
  setting.
- **Lighting.** Up to four SP107E controllers at the fixtures: power,
  brightness and eight colour presets, with each lamp remembered so it keeps
  its name across a power cut.
- **A phone can read the pack without joining anything.** The van advertises
  itself as BTHome v2 and is never connectable, so it spends none of the BLE
  connection slots. `android/` is a monitor app that reads it.
- **Diagnostics on the panel**, behind the gear: uptime, heap, the LVGL pool,
  loop and flush rates, the I2C bus and every integration's link state.

Water is still an empty page waiting for an integration, and there is a
hand-set clock because the board has no RTC and no NTP.

## Build

PlatformIO is the build system and it is not vendored, so install it first:

```
brew install platformio         # macOS
```

Elsewhere, use `pipx install platformio` or the official installer script. The
Homebrew formula is the least trouble on macOS: it brings its own Python and
keeps PlatformIO's dependencies out of whatever you have in `/usr/local` or a
Framework install.

```
pio run                      # compile
pio test -e native           # 246 host tests, no board, ~6 s
pio run -t upload --upload-port /dev/cu.usbserial-XXXX
pio device monitor --port /dev/cu.usbserial-XXXX -b 115200
```

**Name the upload port.** The board exposes two and only one is the ESP32-S3;
see *Which port* below. For reading the log from a script rather than by eye,
use `tools/readlog.py` - and never `cat` the port, because macOS resets termios
on open and a preceding `stty` is discarded, so you get garbage.

`pio test -e native` is worth knowing about before anything else: everything in
`src/core/` is deliberately free of hardware - entity formatting, the BTHome and
SP107E codecs, the clock, the history ring, the alarm rules - so it compiles and
runs on a laptop in seconds. That is the cheapest insurance in the project and
CI runs it before the firmware build.

The first `pio run` is a long one: it pulls eleven packages and ends up around
3.5 GB in `~/.platformio`, near-silent while it works. The platform installs the
RISC-V toolchain and both GDB builds as well, which is where most of that goes -
the ESP32-S3 only needs the Xtensa half. Budget fifteen minutes. Subsequent
builds are about a minute.

Arduino core 3.1.2, via the pioarduino platform fork (PlatformIO's own
espressif32 platform still ships core 2.x, and Arduino_GFX 1.5.3 needs ESP-IDF
5.x). BLE is NimBLE-Arduino rather than the core's Bluedroid stack, which is
worth 40 KB of heap and 400 KB of flash - see `docs/decisions/0010`. LVGL reads
`include/lv_conf.h`, generated from the 9.2.2 template; that file's header lists
the five settings that differ from stock and why each one moved.

### Which port

The Indicator enumerates *twice* through its internal USB hub, so there are two
plausible-looking serial ports and only one of them is right:

```
1a86:7523   CH340    the ESP32-S3 - this is what you flash
2886:0050   Seeed    the RP2040 co-processor - unused by this firmware
```

On macOS those come up as `/dev/cu.usbserial-*` and `/dev/cu.usbmodem*`
respectively. Auto-detect takes whichever it sees first, which is a coin flip,
so name the port:

```
pio run -t upload --upload-port /dev/cu.usbserial-1110
pio device monitor --port /dev/cu.usbserial-1110 -b 115200
```

If the upload fails with **"Unable to verify flash chip connection (No serial
data received)"**, the baud rate is the first suspect, not the port. The CH340
bridge on this board syncs at 115200, negotiates up, and then goes silent at
921600 - which is why `upload_speed` is pinned to 460800. Confirm the port
itself is fine with a read-only probe:

```
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 \
    --port /dev/cu.usbserial-1110 --baud 460800 flash_id
```

That should report an 8MB flash and the chip's MAC. If it does, the port and the
auto-reset wiring are both good. Only if *that* fails is it worth holding BOOT,
tapping RESET, and releasing BOOT to force download mode by hand.

One trap worth knowing why it is already set: `platformio.ini` pins
`-DARDUINO_USB_CDC_ON_BOOT=0`, and it has to. With it at 1, `Serial` goes to the
ESP32-S3's native USB rather than the CH340 - and on this board the native USB
is **not** wired through the internal hub, so it routes the logs to a port that
never enumerates and the firmware appears mute on a board that is running
perfectly. If you ever see a silent CH340 and no new `usbmodem` port, that is
this setting.

### WiFi needs a nudge

`src/integrations/bms_jbd.cpp` includes `<WiFi.h>`, and out of the box that does
not compile: `WiFi.h` pulls in `Network.h`, but the core's
`WiFi/library.properties` ships no `depends=` line, so PlatformIO's dependency
finder never discovers the Networking library. `platformio.ini` works around it
in two places, and both are needed:

- a `lib_deps` entry naming the bundled `Network` directory, so its sources are
  actually compiled and linked;
- a matching `-I` in `build_flags`, because PlatformIO builds each library in
  isolation and WiFi's own sources would otherwise still not find the header.

Drop both if a future core adds the missing `depends=` line. This is also why
the arduino-cli harness disagrees: it puts every core library on the include
path and never has to resolve the dependency at all.

**`LittleFS` needs exactly the same pair of lines**, for exactly the same
reason, and `platformio.ini` carries them too. If you add another bundled core
library and it will not compile, this is the first thing to copy.

`WiFi.h` is there for a single call - `WiFi.macAddress()`, for the BLE device
name - and `NimBLEDevice::getAddress()` would return the same thing and let all
of this go. It is on the roadmap.

### The arduino-cli harness

`tools/build.sh` compiles the same sources with the same core and library
versions, driving arduino-cli instead of PlatformIO. It exists because the
sandbox this was developed in cannot reach the PlatformIO package registry.
`tools/setup_toolchain.sh` builds that toolchain from GitHub and `tools/watch.sh`
rebuilds on every save.

**It is Linux-only, and it has drifted** - `setup_toolchain.sh` fetches the
`Linux_64bit` arduino-cli release asset and nothing else, so it will not run on
macOS, and a system arduino-cli is not a substitute unless it happens to have
esp32 core 3.1.2 installed. It also no longer knows about `android/`,
`include/icd.h` or several files in `src/core/`, which makes it worse than
useless for checking a change: it can pass on a tree that does not build. Use
PlatformIO. Deleting it or putting it in CI is `docs/ROADMAP.md` item 3.

### Before the first flash

If you have just cloned this, turn on the commit-time documentation gate. Git
does not clone hooks, so without this it silently does nothing - and a hook
that silently does nothing is worse than no hook, because you believe you are
covered:

```
git config core.hooksPath .githooks
```

You probably do not need to set your BMS's address. `CFG_BMS_MAC` in
`include/config.h` is a seed, not a commitment: the firmware attaches to the
first JBD BMS it hears, then **remembers that address in NVS** and prefers it
on every later boot, so letting it find the battery once in the driveway is
enough. Settings has a Forget button for when it has latched onto the wrong
one - which is the case the seed exists for, a campground full of other vans.

The light fixtures work the same way and need no configuration at all: each one
is found by its advertisement and its slot is remembered, so "Light 2" stays
the same lamp. `CFG_LIGHTING_MAX_FIXTURES` is how many the firmware will hold,
and it is a BLE connection budget rather than a display limit - read the note
beside it before raising it.

## Layout

```
include/board_indicator_d1.h   pin map for the Indicator D1
include/config.h               everything you tune while commissioning
include/icd.h                  firmware half of the phone wire format
include/lv_conf.h              LVGL configuration, with its five deltas
src/bsp/                       panel, touch, IO expander, LVGL bring-up,
                               flash filesystem (off - see decisions/0014)
src/core/                      entities, registry, alarms, clock, history,
                               units, the BTHome / SwitchBot / SP107E
                               codecs, the light palette - all hardware-free
                               and all host-tested - plus settings.cpp,
                               which is not, because NVS is the board
src/integrations/              one file per device; see below
src/ui/                        LVGL screens and the overlays
lib/OverkillSolarBMS/          vendored BMS protocol library
lib/BleSerialClient/           vendored BLE serial client and the shared
                               BLE scanner, ported to NimBLE
android/                       the phone monitor app, built without Gradle
test/                          host tests, one directory per core module
tools/patch_gfx.py             build-time Arduino_GFX patch (required)
tools/readlog.py               serial capture - use this, never `cat`
tools/check_icd.py             holds the firmware and the app to docs/ICD.md
tools/build.sh                 arduino-cli harness, Linux only, unmaintained
.githooks/pre-commit           refuses undocumented firmware changes
```

The integrations, in the order `setup()` registers them - which is
load-bearing, and `src/main.cpp` says why at each line:

```
bms_jbd.*          the JBD BMS over BLE, on its own FreeRTOS task
env_sensors.*      every BTHome / SwitchBot broadcaster in earshot
lighting.*         up to four SP107E fixtures, one task for all of them
ble_broadcast.*    the van advertising itself to a phone
system_info.*      the board reporting on itself, for the System screen
```

## How it fits together

Nothing in the UI talks to hardware. An **integration** owns a device, runs
non-blocking work in `loop()`, and publishes **entities** - a numeric reading, a
temperature, a binary state, a switch, a line of text - into the **registry**.
Screens read the registry; alarm rules read entity values and raise or clear
named alarms.

Two rules carry most of the weight. **There is one cooperative loop**, so a
blocking call inside any integration freezes the whole UI - that has happened,
and it presents as a blank screen with no crash, which is why `Hub::loop` now
names any integration holding the loop past 50 ms. And **the UI may ask, not
decide**: tapping a switch calls `command()`, and the value on screen only
moves when the device that owns the hardware calls `confirm()`, so a fixture
that has gone offline shows what it last reported rather than what somebody
wished for.

That is the seam for everything still to come. A tank sender on a Grove port,
the diesel heater, the RP2040 link: each is one integration file that registers
entities in its domain, and the corresponding page stops being empty.

`docs/ARCHITECTURE.md` has the contracts and the rules that must not be broken;
`docs/ADDING_AN_INTEGRATION.md` is the short version with a worked example.

## Documentation

`CONTRIBUTING.md` is how to work on this: the review flow, where a new fact
belongs, and what "done" means for a change. Start there.
`docs/README.md` indexes the reference material below.

| | |
|---|---|
| `docs/ARCHITECTURE.md` | how it is put together, and what may not be broken |
| `docs/HARDWARE.md` | the board: pins, USB ports, flashing, panel quirks |
| `docs/ICD.md` | **normative.** what goes over the air to a phone |
| `docs/ADDING_AN_INTEGRATION.md` | adding an accessory |
| `docs/TROUBLESHOOTING.md` | symptom-first runbook of failures we have hit |
| `docs/ROADMAP.md` | what is wrong and what is next |
| `docs/PORTING_NOTES.md` | why the vendored code looks like that |
| `docs/decisions/` | choices where the obvious path was rejected |
| `android/README.md` | the phone app, and why it has no Gradle |

`docs/ICD.md` is the one marked normative because it is a contract with
something outside this repository's control - a phone in somebody's pocket. A
wire-format change recorded anywhere else has drifted. `tools/check_icd.py`
enforces the machine-checkable half of it and the commit hook runs it.

## Status

**Runs on hardware**, with everything in the list at the top of this file
working on a real board. Measured 2026-10-19:

```
flash        51.1%   1,706,700 of 3,342,336 bytes
static RAM   41.2%   135,004 of 327,680 bytes
free heap    ~61 KB after boot
LVGL pool    80% used, 1% fragmented, with every screen built
main loop    ~19.8k iterations/sec, LVGL 84 flushes/sec
host tests   246 cases in about six seconds
```

Those numbers are in the README because they are the budget, not trivia. The
tight resource is internal RAM, not flash: **the LVGL pool holds every screen
at once and exhausting it does not report itself** - it boot loops in a stack
trace naming no file of ours - so `setup()` logs the pool and the System screen
carries it live. Read `docs/decisions/0013` before adding a screen, and the
figure the board logs before designing two.

Three things are known-missing rather than broken: nothing can be stored
durably (both routes were closed by probe - see `docs/decisions/0014` and
`docs/HARDWARE.md`), every update needs a cable, and the BMS sometimes takes
half a minute or more to connect at boot for reasons nobody has yet observed.
All three are in `docs/ROADMAP.md`, which is what is left.

`docs/PORTING_NOTES.md` records what the first boot actually taught us, which
is worth reading before touching the display or the BLE client.
