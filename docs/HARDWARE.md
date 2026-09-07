# Hardware reference

Seeed SenseCAP Indicator D1. ESP32-S3R8 (8 MB flash, 8 MB octal PSRAM) plus an
RP2040 co-processor, behind a 4" 480x480 ST7701S RGB panel with FT6336 touch.

`include/board_indicator_d1.h` is the **only** place pin numbers belong. This
document gives the context that a pin number cannot: where the number came from,
what is confirmed, and what is still a guess.

## Confirmed on hardware

Observed on a real board on 2026-09-06.

| Fact | Value |
|---|---|
| Chip | ESP32-S3 (QFN56) revision v0.2 |
| Flash | 8 MB, Winbond (`ef 4017`), quad in eFuse, 3.3 V |
| PSRAM | 8 MB octal (`qio_opi`) |
| Crystal | 40 MHz |
| Base MAC | `90:70:69:11:be:34` |
| BLE MAC | base + 2 in the last byte, so `...be:36` |
| I2C | SDA 39, SCL 40, 400 kHz |
| Backlight | GPIO 45 via LEDC |
| IO expander | PCA9535 at `0x20` |

The BLE device name is derived as `CamperCtl-` plus the last two bytes of the
Bluetooth MAC, which for this board is `CamperCtl-BE36`.

## USB: there are two ports and only one is right

The Indicator has an internal Terminus USB 2.0 hub (`1a40:0101`). Two devices
hang off it:

| Device | VID:PID | macOS name | What it is |
|---|---|---|---|
| CH340 | `1a86:7523` | `/dev/cu.usbserial-*` | **The ESP32-S3. Flash this one.** |
| Seeed | `2886:0050` | `/dev/cu.usbmodem*` | RP2040 co-processor, unused today |

There is **no Espressif device** (`303a:*`) on that hub. The ESP32-S3's native
USB either is not wired through or does not enumerate, which has one important
consequence: `ARDUINO_USB_CDC_ON_BOOT` must be **0**, or Arduino's `Serial`
goes to a port that does not exist and the firmware's own logging vanishes. The
ESP-IDF logger writes to UART0 regardless, which is why boot messages appeared
even while `Serial.print` did not.

PlatformIO's port auto-detection picks whichever it finds first, which is a coin
flip. Always name the port.

## Flashing

```
pio run -t upload --upload-port /dev/cu.usbserial-1110
```

**`upload_speed` is 460800, not 921600.** The CH340 bridge syncs at 115200,
negotiates up, and then goes silent at 921600 - esptool reports "Unable to
verify flash chip connection (No serial data received)". 460800 is stable.
Verified both ways on hardware.

A read-only probe that confirms the port and the auto-reset wiring without
writing anything:

```
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 \
    --port /dev/cu.usbserial-1110 --baud 460800 flash_id
```

If that reports 8 MB and a MAC, the port is right. Only then is it worth
holding BOOT, tapping RESET and releasing BOOT to force download mode by hand.

## Reading the log

Opening the port toggles DTR/RTS, which **resets the board** - every log capture
restarts the firmware and drops any BLE session. Expect it.

`cat` will not work: macOS reapplies default termios when a process opens
`/dev/cu.*`, so a preceding `stty` is discarded and you get garbage at 9600.
Use a tool that sets the baud on its own handle - `pio device monitor`, or
pyserial with `dtr = False; rts = False`.

## Display path

The panel is driven over the 16-bit RGB parallel interface, but its **chip
select is not on a GPIO** - it hangs off the IO expander. `IndicatorPanelBus` in
`src/bsp/display.cpp` exists solely for that: it subclasses Arduino_GFX's
bit-banged SPI and drives CS over I2C in `beginWrite`/`endWrite`. Only the ST7701
init sequence uses that bus; pixels go over RGB.

Two things about this panel that cost real time:

- **It comes up 180 degrees off.** `st7701_type1_init_operations` in Arduino_GFX
  orients it upside down in this enclosure, so `flushCb` reverses each block and
  remaps the origin. `CFG_DISPLAY_ROTATE_180` switches it off.
- **Bounce buffers must be disabled.** Arduino_GFX hardcodes
  `bounce_buffer_size_px = 40 * w`, whose refill ISR lives in flash. Any flash
  access - Bluedroid's NVS reads, for instance - collides with it and panics.
  `tools/patch_gfx.py` applies this at build time; see `decisions/0004` for
  why and `decisions/0007` for how.

Pixel clock is 12 MHz (Arduino_GFX's octal-PSRAM default), about 24 MB/s of
framebuffer reads, comfortably inside octal PSRAM's budget.

## Touch

**The controller answers at `0x48`, not the `0x38` every FT6336 datasheet and
community pin map gives.** That single wrong constant is why touch appeared
dead for days.

Established 2026-09-06 by three experiments, in this order:

1. A bus scan finds `0x20` (the expander) and `0x48`. No `0x38`, ever.
2. The expander answers and `EXP_TOUCH_INT` idles high, so the expander path and
   the interrupt line are both sound.
3. Driving *every* expander pin high - in case the real reset line was floating
   on a pin we never claimed - changed nothing. The controller was never held in
   reset; it was simply at a different address.

It speaks the ordinary FT5x06/FT6x36 data layout - `0x02` point count, `0x03`
and `0x04` X, `0x05` and `0x06` Y, with the event flag in the top two bits of
`0x03` - so `Touch::read` needed no changes at all once the address was right.
Decoded live touches land inside 0..479 on both axes.

It is **not** a FocalTech part despite the layout: the identity and power
registers (`0x86`-`0x89`, `0xA3`, `0xA6`, `0xA8`) all read `0x00`, so there is
no monitor mode to configure and no vendor id to check. Treat the data
registers as the only documented surface.

`CFG_TOUCH_MIRROR_X` and `_Y` are both on, matching the ESPHome definition for
this board, and must stay consistent with the rotation setting above. **Both are
UNVERIFIED** - no touch has ever been read.

## Still unverified

- **Touch orientation and alignment.** Nothing has been touched yet.
- **Expander pins 9 and 10** in `board_indicator_d1.h` are carried from
  community pin maps, are not driven by this firmware, and should be confirmed
  against your own board before use.
- **Colour order.** If the image is ever blue-tinted, the panel wants big-endian
  pixels: pass `useBigEndian = true` to `Arduino_ESP32RGBPanel`. Colours look
  correct today, so this is settled by observation rather than by datasheet.
- **The RP2040 is idle.** It owns the buzzer, the SD slot and the Grove ports,
  and is reachable over UART on GPIO 19/20 (PacketSerial + COBS). Nothing talks
  to it yet, which is why there is no audible alarm.
