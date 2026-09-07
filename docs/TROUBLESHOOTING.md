# Troubleshooting

Organised by **what you can see**, because that is what you have when it breaks.
Every entry here happened; none are hypothetical.

| Symptom | Jump to |
|---|---|
| `pio: command not found` | [Build](#build) |
| `Network.h: No such file or directory` | [Build](#build) |
| `Unable to verify flash chip connection` | [Flashing](#flashing) |
| Screen black, backlight pulsing on and off | [Boot loop](#boot-loop) |
| Screen black, backlight steady, no crash in log | [Frozen loop](#frozen-loop) |
| Serial shows nothing at all | [Serial](#serial) |
| Serial shows binary garbage | [Serial](#serial) |
| A literal `f` where a number should be | [UI](#ui) |
| A label drifts out of position as its value grows | [UI](#ui) |
| Cell bars blink on and off | [Data](#data) |

## First moves

Two checks answer most questions before you start guessing.

**Is the loop running?** `src/bsp/display.cpp:displayLoop` can log a heartbeat:

```
hb: loops=395326 flushes=420 scr=0x3fca45e8 children=6 heap=70736
```

If `loops` climbs (~46k/sec) and `flushes` climbs (~48/sec), the firmware is
alive and rendering, and your problem is above the loop. If `loops` is stuck or
the heartbeat never appears at all, something is blocking inside `loop()` and
nothing else you observe is meaningful yet.

**Is the panel alive?** Put `g_gfx->fillScreen(RED); delay(1000);` immediately
after `g_gfx->begin()` in `displayBegin()`. Red means the panel, pins and DMA
are fine and the fault is in LVGL or above. Still black means it is the panel
path.

Adding the heartbeat first would have saved two days. Do it early.

## Build

**`pio: command not found`** - PlatformIO is not vendored. `brew install
platformio` on macOS. Do not substitute `tools/build.sh`: it is Linux-only, and
a system `arduino-cli` will have the wrong core.

**`fatal error: Network.h: No such file or directory`** - reached from
`WiFi.h`. The core's `WiFi/library.properties` ships no `depends=` line, so
PlatformIO's dependency finder never discovers the Networking library. Fixed in
`platformio.ini` in **two** places, both required: a `lib_deps` entry naming the
bundled `Network` directory so its sources compile and link, and a matching `-I`
in `build_flags` because PlatformIO builds each library in isolation and WiFi
would otherwise still not find the header.

Note this is exactly where the two build systems diverged: `tools/build.sh`
compiled clean because arduino-cli puts every core library on the include path.
See `ROADMAP.md`.

## Flashing

**`A fatal error occurred: Unable to verify flash chip connection (No serial
data received.)`** - almost always the baud rate, not the port. Confirm with a
read-only probe at 460800; see `HARDWARE.md`. If the probe works and the upload
does not, `upload_speed` is too high.

**Uploads to the wrong device** - there are two serial ports. `HARDWARE.md`
has the identification table. Always pass `--upload-port`.

## Boot loop

**Backlight pulsing, nothing on screen** means the chip is panicking and
resetting. Capture the log; you will see a register dump.

```
Guru Meditation Error: Core 1 panic'ed (Cache disabled but cached memory
region accessed)
```

Decode the backtrace against the ELF:

```
~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-addr2line \
    -pfiaC -e .pio/build/indicator_d1/firmware.elf 0x... 0x... 0x...
```

The one we hit decoded to two things colliding: `esp_flash_read` under
`nvs::Storage` at the bottom, and `lcd_rgb_panel_fill_bounce_buffer` under
`gdma_default_tx_isr` at the top. A flash read disables the cache; the RGB
panel's DMA end-of-frame ISR fires inside that window; the ISR lives in flash.
Fix: disable bounce buffers - `decisions/0004`.

Any panic naming *cache* plus an ISR is this class of bug. The general question
is "what flash access raced with what interrupt".

## Frozen loop

**Backlight steady, screen black or stale, no panic, log goes quiet.** The main
loop is blocked. Add the heartbeat; if it never prints, something before
`displayLoop()` in `loop()` is not returning.

The instance we hit: `BleSerialClient::connectToServer` discarded the result of
`pClient->connect()` and logged success regardless, then called `getService()`
on a dead handle, which blocks on a Bluedroid semaphore waiting for a discovery
event that never arrives. The whole cooperative loop stopped, so LVGL never ran
and the panel kept showing nothing.

The tell in the log:

```
[3751][E] gattClientEventHandler(): Failed to connect, status=Unknown ESP_ERR
[3768]    connectToServer():  - Connected to server      <- logged anyway
                                                          then silence
```

An error followed by a success message followed by silence is the signature.
This is the failure mode `ARCHITECTURE.md` rule 1 exists to prevent - any
blocking call inside an integration takes the entire UI with it.

## Serial

**Nothing at all** - `ARDUINO_USB_CDC_ON_BOOT` must be 0 on this board. With it
at 1, `Serial` routes to the ESP32-S3's native USB, which does not enumerate
here, so ESP-IDF log lines appear (they go to UART0) while your own
`Serial.print` silently vanishes. Confusing precisely because it half works.

**Binary garbage** - you used `cat`. macOS resets termios when a process opens
`/dev/cu.*`, discarding any preceding `stty`, so you are reading at 9600. Use
`pio device monitor` or pyserial.

**`pio device monitor` fails with `termios.error: (102, 'Operation not
supported on socket')`** - miniterm needs a real TTY on stdin and will not run
non-interactively. Use pyserial directly in scripts.

**LVGL never logs anything** - `LV_USE_LOG` alone is not enough.
`LV_LOG_PRINTF` must also be 1, or every LVGL warning and error is formatted and
discarded. Both are set in `include/lv_conf.h`.

## UI

**A literal `f` where a number should be** (`f Ah f Wh`, `min f max f`) -
`lv_label_set_text_fmt` goes through LVGL's own vsnprintf, which has no `%f`
support unless `LV_USE_FLOAT` is on. It parses `%.0f`, consumes the `%.0`, finds
no handler, and emits the `f` literally.

Do **not** fix this by setting `LV_USE_FLOAT 1`. That flag also retypes
`lv_value_precise_t` from `int32_t` to `float` across `lv_area.h`,
`lv_draw_arc.h` and `lv_obj_property.h` - it changes LVGL's geometry ABI to fix
a string. Format with `snprintf` into a buffer, or use `Entity::format()`, and
call `lv_label_set_text`. See `decisions/0005`.

**A label drifts out of position as its value grows** - `lv_obj_align_to()`
resolves to fixed coordinates when called and does not follow the object
afterwards. Aligning a label while its text still reads `--` pins the position
for a two-character string, so `100%` later hangs off to the right. Re-align
after setting the text, or give the label a fixed box and centre the text
inside it.

## Touch

**Taps intermittently ignored, but touch basically works.** The digitiser is
fine; the firmware is not looking. `Hub::loop` logs any integration that holds
the loop past 50 ms:

```
integration 'JBD BMS' held the loop for 6010 ms
```

`Touch::read` is called from LVGL's input timer inside the same cooperative
loop, so a stalled integration stops touch being polled at all. Confirm by
correlating the heartbeat: a healthy window is ~80,000 loops and 96 flushes per
two seconds, and a stalled one shows single digits.

The instance we hit was Bluedroid's synchronous `connect()` retrying against an
absent BMS. Fixed by moving that integration to its own task -
`decisions/0008`.

**Touch not detected at all.** Scan the bus before assuming a dead controller;
`displayBegin` logs `i2c scan (boot)` at every startup. This board's controller
is at `0x48`, not the `0x38` its datasheet family uses. See `HARDWARE.md`.

## Data

**Cell bars blink on and off** - `query_0x04_cell_voltages()` in the vendored
BMS library zeroes its cell array when it *sends* the request and only refills
it when the reply lands. Polling faster than that round trip samples the gap and
reads zeros. `bms_jbd.cpp:refresh` now treats zero as "no reply yet this cycle"
and keeps the previous reading.

The giveaway was that the min/max/spread summary stayed on screen while the bars
vanished: those are only written when at least one cell reads non-zero, so they
latched while `volts[]` was being clobbered.

**Connected but no data at all** (`getLength head: 0 tail: 0` forever) - the
client attached but notifications are not arriving. Check that `CFG_BMS_MAC` is
set; with it empty the firmware takes the first device advertising service
`ff00`, which in a campground is somebody else's battery.

**BMS connects, then will not reconnect** - a JBD BMS accepts one client at a
time. The Xiaoxiang/Overkill phone app will hold it. Close the app.
