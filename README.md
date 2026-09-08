# Camper Control

Vehicle accessory control on the [Seeed SenseCAP Indicator D1](https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html)
(ESP32-S3 + RP2040, 4" 480x480 touchscreen).

Step one is the battery: the JBD / Overkill Solar BMS monitor from
`esp32-smartBMSdisplay` moved onto the Indicator's screen. Lighting, water and
climate are stubbed as empty pages that fill themselves in as integrations are
added.

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
pio run                 # compile
pio run -t upload       # flash over USB-C
pio device monitor      # 115200 baud
```

The first `pio run` is a long one: it pulls nine packages, about 1.2 GB of
archives that unpack to roughly 4.4 GB in `~/.platformio`, and it is near-silent
while it works. Note that the platform installs the RISC-V toolchain and both
GDB builds as well, which is where most of that goes - the ESP32-S3 only needs
the Xtensa half. Budget fifteen minutes. Subsequent builds are about a minute.

Arduino core 3.1.2, via the pioarduino platform fork (PlatformIO's own
espressif32 platform still ships core 2.x, and Arduino_GFX 1.5.3 needs ESP-IDF
5.x). BLE is NimBLE-Arduino rather than the core's Bluedroid stack, which is
worth 40 KB of heap and 400 KB of flash - see `docs/decisions/0010`. LVGL reads `include/lv_conf.h`, generated from the 9.2.2 template - that
file's header lists the four settings that differ from stock.

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

One surprise after a successful flash: `ARDUINO_USB_CDC_ON_BOOT=1` in
`platformio.ini` routes `Serial` to the ESP32-S3's native USB rather than to the
CH340, so the CH340 port can sit perfectly silent while a *new* `usbmodem` port
appears. Monitor that one. If no new port shows up, the native USB is not wired
through the hub on this board - set `-DARDUINO_USB_CDC_ON_BOOT=0` and the logs
come back on the CH340.

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

### The arduino-cli harness

`tools/build.sh` compiles the same sources with the same core and library
versions, driving arduino-cli instead of PlatformIO. It exists because the
sandbox this was developed in cannot reach the PlatformIO package registry.
`tools/setup_toolchain.sh` builds that toolchain from GitHub and `tools/watch.sh`
rebuilds on every save.

**It is Linux-only** - `setup_toolchain.sh` fetches the `Linux_64bit` arduino-cli
release asset and nothing else. It will not run on macOS, and a system
arduino-cli is not a substitute unless it happens to have esp32 core 3.1.2
installed. On a normal machine, just use PlatformIO.

### Before the first flash

If you have just cloned this, turn on the commit-time documentation gate. Git
does not clone hooks, so without this it silently does nothing - and a hook
that silently does nothing is worse than no hook, because you believe you are
covered:

```
git config core.hooksPath .githooks
```


Set your BMS's MAC address in `include/config.h` (`CFG_BMS_MAC`). Leave it empty
and the firmware attaches to the first JBD BMS it hears, which is fine in the
driveway and wrong in a campground. The Overkill Solar / Xiaoxiang app shows the
address, or leave it empty for one boot and read it off the serial log.

## Layout

```
include/board_indicator_d1.h   pin map for the Indicator D1
include/config.h               everything you tune while commissioning
src/bsp/                       panel, touch, IO expander, LVGL bring-up
src/core/                      entities, registry, integrations, alarms,
                               NVS-backed settings
src/integrations/bms_jbd.*     the JBD BMS over BLE
src/ui/                        LVGL screens and the settings overlay
lib/OverkillSolarBMS/          vendored BMS protocol library
lib/BleSerialClient/           vendored BLE serial client, ported to NimBLE
tools/patch_gfx.py             build-time Arduino_GFX patch (required)
tools/readlog.py               serial capture
tools/build.sh                 arduino-cli build harness, Linux only, unmaintained
.githooks/pre-commit           refuses undocumented firmware changes
```

## How it fits together

Nothing in the UI talks to hardware. An **integration** owns a device, runs
non-blocking work in `loop()`, and publishes **entities** - a numeric reading, a
binary state, a switch - into the **registry**. Screens read the registry;
alarm rules read entity values and raise or clear named alarms.

That is the seam for everything still to come. A relay board for the lights, a
tank sender on a Grove port, the diesel heater: each is one integration file
that registers entities in its domain, and the corresponding page stops being
empty.

`docs/ARCHITECTURE.md` has the contracts and the rules that must not be broken.

## Documentation

`CONTRIBUTING.md` is how to work on this: the review flow, where a new fact
belongs, and what "done" means for a change. Start there.
`docs/README.md` indexes the reference material below.

| | |
|---|---|
| `docs/ARCHITECTURE.md` | how it is put together, and what may not be broken |
| `docs/HARDWARE.md` | the board: pins, USB ports, flashing, panel quirks |
| `docs/ADDING_AN_INTEGRATION.md` | adding an accessory |
| `docs/TROUBLESHOOTING.md` | symptom-first runbook of failures we have hit |
| `docs/ROADMAP.md` | what is wrong and what is next |
| `docs/PORTING_NOTES.md` | why the vendored code looks like that |
| `docs/decisions/` | choices where the obvious path was rejected |

## Status

**Runs on hardware.** Boots, drives the panel, reads touch, connects to the
JBD BMS over BLE and displays live pack data. Settings persist across a power
cycle. Flash 49.2% (1,645,040 of 3,342,336 bytes), static RAM 32.6% (106,964 of
327,680), ~97 KB of free heap, and one narrowing warning out of the vendored
Arduino_GFX.

`docs/ROADMAP.md` is what is left. `docs/PORTING_NOTES.md` records what the
first boot actually taught us, which is worth reading before touching the
display or the BLE client.
