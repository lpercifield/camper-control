# Porting notes

The record of what was changed in somebody else's code and why. Patches
without a reason here get reverted by the next upgrade. See `README.md`
in this directory for where other kinds of fact belong.

## What came across from esp32-smartBMSdisplay

`lib/OverkillSolarBMS` is the Overkill Solar BMS 2 library, unchanged.
`lib/BleSerialClient` is the BLE serial client, patched (below) and since
rewritten against NimBLE - see `decisions/0010`. Every numbered change below
survived that rewrite; they are listed against the original Bluedroid code
because that is where each problem was found. The monitor
logic - protection thresholds, alarm conditions, cell statistics, the status
string - was rewritten as `src/integrations/bms_jbd.cpp` with the same rules and
the same thresholds. The five U8g2 OLED pages became one LVGL screen: the SOC
arc replaces the SOC page, the four tiles cover the voltage and current pages,
and the cell bars replace the cell page.

## Changes made during the port, and why

**The old loop blocked; this one cannot.** The original firmware owned the CPU:
`while (!SerialBT.connected()) SerialBT.bleLoop();` at the top, then a tight
loop that only ever drew the display. A touchscreen with four domains cannot
work that way, so every blocking wait had to go.

1. `BleSerialClient::onDisconnect()` called `delay(5000)` *inside a BLE stack
   callback*. Removed. It also now drops its characteristic pointers, which the
   original left dangling until the next connect.
2. `bleLoop()` restarted scanning with the blocking form of `BLEScan::start()` -
   five seconds of frozen UI on every pass while the BMS was out of range. Now
   asynchronous. It was also rate limited to one attempt every six seconds
   until patch 9 moved scanning out of this class entirely.
3. `scanCompleteCB()` used to restart the scan from inside the callback. The
   objection is that the callback runs on the BLE host task, not that the radio
   was busy - patch 9 deliberately keeps it scanning while connected. Restarts
   are decided by a loop; the callback only releases the results.
4. `flush()` wrote to `TxCharacteristic` without checking it was still valid.
   Guarded.
5. Added `setTargetAddress()`. The original connected to the first device
   advertising service `ff00`. Fine on a bench, not fine parked next to someone
   else's battery. `CFG_BMS_MAC` pins it.
6. `connectToServer()` threw away the result of `pClient->connect()` and logged
   " - Connected to server" either way. A failed connect then fell through to
   `getService()` on a dead client, which blocks waiting on a discovery event
   that never arrives - and since integrations run on the main loop, that froze
   the UI and left the screen blank. It now returns false and lets `bleLoop()`
   retry. The remaining wart: `connect()` is itself synchronous and costs about
   two seconds on failure, so an absent BMS still stutters the loop each retry.
7. Added `peerAddress()`. The client knew which device it had connected to but
   never exposed it, so there was no way to remember a BMS across reboots. It
   is recorded on a successful connect and cleared on disconnect.
9. **The class no longer owns the scan.** `BleScanner`
   (`lib/BleSerialClient/BleScanner.h`) owns `NimBLEDevice::getScan()` and fans
   advertisements out to listeners; `BleSerialClient` implements
   `BleAdvertisementListener` like anything else and calls `pause()`/`resume()`
   around a connect. NimBLE has exactly one set of scan callbacks, so as long
   as this class claimed them no second consumer - a BLE temperature sensor -
   could exist. Worse, the shipped `if (bleConnected) return;` meant the radio
   stopped scanning entirely once the BMS connected. Measured on hardware
   2026-09-09; see `ROADMAP.md` and `ARCHITECTURE.md`.
8. Reading the protection thresholds is *not* free: each
   `get_0x2x_...()` is a blocking round trip that enters and leaves the BMS's
   factory mode, so the original's six back-to-back reads could stall for
   several seconds. They are now spread one per 500 ms cycle after connect.

## Build environment

PlatformIO is the project's build system, but its package registry is blocked by
network policy in the sandbox this was developed from - `pio run` cannot install
anything there, not even `platform = native`. GitHub release assets, git and
raw.githubusercontent are reachable, so `tools/setup_toolchain.sh` assembles an
arduino-cli toolchain out of those instead: arduino-cli itself, the esp32 core
index (trimmed of its `arduino:dfu-util` dependency, which lives on a blocked
host and is only needed for uploading), the core and its compilers, and LVGL and
Arduino_GFX cloned at the versions `platformio.ini` pins.

Two details in `tools/build.sh` are worth knowing before touching it. The include
root points at the build directory's copy of the sketch, not at the original
tree - arduino-cli compiles a *copy*, and pointing `-I` at the original makes
every header reachable by two paths, which silently defeats `#pragma once` and
produces a wall of redefinition errors. And the LVGL defines have to reach
`compiler.S.extra_flags` as well as the C and C++ ones, because LVGL ships Helium
assembly files that include `lv_conf_internal.h` too.

The harness and `platformio.ini` duplicate the core version, library versions and
build flags. If they drift, a green build in the harness stops meaning anything.

## Choosing the core version

The port first targeted Arduino core 2.0.17, to keep the vendored Bluedroid BLE
client on the API it was written against. That fell over on the first compile:
Arduino_GFX 1.5.3 uses ESP-IDF 5.x fields (`refresh_on_demand`, `clock_source`)
that core 2.x does not have. Rather than pin an older Arduino_GFX, the project
moved to core 3.1.2 - the combination Seeed's own Indicator tutorial uses. The
BLE client needed exactly one change for it (`BLEAddress::toString()` returns
`String` rather than `std::string`), and the backlight moved to core 3.x's
pin-based `ledcAttach()`.

## What the first boot actually taught us

This section used to say "it compiles, and it has never run". It has run, on
2026-09-06 and since. Recorded here as history because the order these were
worked through is the reason any of them were found.

**Settled by observation:** the panel comes up, the orientation flag is
correct, colours are right (no big-endian swap needed), and the draw buffers
allocate in internal RAM as intended. The touch controller was the one that
did not survive contact - it answers at `0x48`, not the `0x38` the pin map
assumed, which cost two days. See `HARDWARE.md`.

**Still unverified:** expander pins 9 and 10, which nothing drives.

The original list, for the record:

- **Panel comes up at all.** If the screen stays black, the suspect is the LCD
  chip-select on the IO expander (`EXP_LCD_CS`, PCA9535 pin 4) - the panel's CS
  is not on a GPIO, which is what `IndicatorPanelBus` in `src/bsp/display.cpp`
  exists to handle.
- **Orientation.** Arduino_GFX's `st7701_type1_init_operations` brings this panel
  up 180 degrees off, so the flush callback reverses each block. If the image is
  upside down, set `CFG_DISPLAY_ROTATE_180` to 0.
- **Touch alignment.** `CFG_TOUCH_MIRROR_X` / `_Y` are both on, matching the
  ESPHome definition for this board. If taps land mirrored, flip them - and note
  they must stay consistent with the rotation setting above.
- **Colour order.** If everything is blue-tinted, the panel wants big-endian
  pixels: pass `useBigEndian = true` to `Arduino_ESP32RGBPanel`.
- **Memory.** Two 480x40 draw buffers live in internal RAM alongside the BLE
  host. If allocation falls back to PSRAM (there is a log line for it) the UI
  will feel sluggish; drop `kBufLines` in `display.cpp`. This has not happened,
  and there is far more headroom since the NimBLE port.
- **Expander pins 9 and 10** in `board_indicator_d1.h` are guesses carried from
  community pin maps and are not driven by this firmware. Confirm against your
  board before using them.

## Known gaps

- **No audible alarm.** The Indicator's buzzer is on the RP2040 side of the
  board, so it needs the UART link (GPIO19/20, PacketSerial + COBS) before it can
  sound. `serviceAlarmOutput()` in `main.cpp` is the hook; it logs today.
- **The RP2040 is idle.** On a D1 there are no onboard sensors to read, but the
  co-processor still owns the buzzer, the SD slot and the Grove ports.
- **One connection, and one scanner.** The client tracks a single peer, so a
  second connectable BLE device needs its own client. Worse for the planned
  temperature sensors: the scanner belongs to `BleSerialClient` and stops while
  connected, so a broadcast-only sensor receives nothing. `ROADMAP.md` carries
  this as the shared-scanner item.
- **`WiFi.h` is included for one line.** `bms_jbd.cpp` pulls in the whole Wi-Fi
  and Networking stack to call `WiFi.macAddress()` for the BLE device name, and
  that is the only reason `platformio.ini` carries the `Network` workaround.
  Since the NimBLE port, `NimBLEDevice::getAddress()` gives the same thing
  directly - removing the include should let the workaround go too.
