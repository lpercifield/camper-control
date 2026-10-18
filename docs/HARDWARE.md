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
this board, and must stay consistent with the rotation setting above. Both are
**confirmed by use** since 2026-09-07: the nav bar, the Settings overlay and the
sensor rename keyboard all hit what they aim at, which a wrong mirror would
break immediately.

## Flash and the RGB panel cannot be used at the same time

Settled 2026-09-17, by a boot loop and a decoded backtrace. **Any flash
operation while the panel is refreshing can panic the chip**, and a sustained
one - a filesystem mount, a directory read - panics it reliably.

A flash access disables the cache. The panel's VSYNC interrupt fires at ~60 Hz
inside that window, and although the handler itself is in IRAM it calls
`gdma_hal_append`, which is not:

```
CONFIG_LCD_RGB_ISR_IRAM_SAFE=y            handler in IRAM      0x4037....
CONFIG_LCD_RGB_RESTART_IN_VSYNC=y         handler restarts DMA every frame
# CONFIG_GDMA_CTRL_FUNC_IN_IRAM is not set    but this is in flash 0x4211....
```

Those are the Arduino prebuilt libraries' own settings, in
`framework-arduinoespressif32-libs/esp32s3/sdkconfig`, and not ours to change -
the same wall as `decisions/0004` and the PSRAM Wi-Fi option.

Consequences, in order of how much they should worry you:

- **LittleFS/SPIFFS is off the table** while the display runs.
  `CFG_LOG_TO_FLASH` is 0. See `decisions/0014`.
- **Mounting before `displayBegin()` is safe**, and is the only safe window -
  the panel is not refreshing yet. Anything the firmware needs off flash it
  must read at boot, before the display comes up.
- **NVS writes are the same hazard, just shorter.** `Preferences` takes one or
  two milliseconds and usually slips between VSYNCs; this project has been
  doing it since the start without a panic. That is luck with good odds, not
  safety. Do not add more of them.

## The SD slot is not reachable from the ESP32-S3

Settled 2026-09-17, by probe. **The microSD slot belongs to the RP2040.** The
ESP32-S3 cannot read or write it, and no firmware on the ESP32-S3 side changes
that.

`board_indicator_d1.h` defines `EXP_SD_CS` on expander pin 8, which reads like
an invitation to drive the card over the panel's SPI bus. It is not one - the
constant is carried from the same community pin map as `EXP_BUZZER_EN` and
`EXP_GROVE_PWR`, both already flagged below as unconfirmed.

The probe drove `EXP_SD_CS` from the expander and ran the SPI-mode init sequence
on `LCD_SPI_SCK/MISO/MOSI` (41/47/48):

```
SD probe: CMD0 -> 0x00, CMD8 -> 0x00
SD probe: idle bytes cs_high=00 00 00 00 cs_low=00 00 00 00
SD probe: gpio47 with pullup=1 pulldown=0 (floating - nothing drives this pin)
```

Three things in that, and the third is conclusive:

- A card in SPI mode answers `CMD0` with `R1 = 0x01`. `0x00` is not an answer -
  it is a low line read as a byte.
- An SD bus with a card present but unselected idles **`0xFF`**, because DO is
  pulled up. Reading `0x00` with CS both high and low means there is no DO.
- **GPIO 47 floats.** Pull-up reads 1, pull-down reads 0, so nothing on the
  board drives that pin. `LCD_SPI_MISO` is a name, not a connection - note that
  `IndicatorPanelBus` never passes it to `Arduino_SWSPI` either.

The pin budget says the same thing independently. The RGB panel takes 0-18 and
21, PSRAM 33-37, flash 26-32, I2C 39/40, UART0 43/44, the RP2040 link 19/20,
backlight 45, button 38, panel SPI 41/47/48. That leaves 42 and 46 - two pins,
where SD needs at least CLK, CMD and DAT0 plus a select.

**Reaching the card means talking to the RP2040** over `RP2040_UART_TX/RX`
(19/20, PacketSerial + COBS), and its stock firmware exposes no file API - so it
means custom RP2040 firmware and a protocol on both sides. Nothing in this
project talks to the RP2040 at all yet.

For on-device logging without that work, the flash already carries an unused
**1.5 MB `spiffs` data partition** (`default_8MB.csv`, `0x670000`), plus a
3.3 MB `app1` OTA slot that nothing uses.

## Still unverified

- **The LoRa pins have never been exercised.** `board_indicator_d1.h` defines
  `EXP_LORA_CS`, `EXP_LORA_RST`, `EXP_LORA_BUSY` and `EXP_LORA_DIO1` on port 0
  of the PCA9535, and nothing in this firmware touches them. They are carried
  from a pin map shared across the Indicator family, and **Seeed fits the
  SX1262 on the D1L and D1Pro, not the D1 this board is documented as** - so
  the footprint is most likely unpopulated. Settle it with a probe rather than
  a model number: the SX1262 answers `GetStatus` over the bit-banged SPI
  (`LCD_SPI_SCK/MOSI/MISO`), and an empty footprint reads back all ones or all
  zeros.

  If it ever is populated, note the shape of the problem before designing
  around it: **`BUSY` and `DIO1` are on the I2C expander, not on GPIOs.** The
  SX1262 wants `BUSY` checked before every SPI command, so each command would
  cost an I2C round trip first, and `DIO1` - the receive interrupt - cannot
  interrupt, so reception would have to be polled over I2C. In a cooperative
  loop that is exactly the shape this project has already been bitten by.
- **Expander pins 9 and 10** in `board_indicator_d1.h` are carried from
  community pin maps, are not driven by this firmware, and should be confirmed
  against your own board before use. Pin 8 (`EXP_SD_CS`) came from the same
  place and has now been probed - see above; treat 9 and 10 with the same
  suspicion.
- **Colour order.** If the image is ever blue-tinted, the panel wants big-endian
  pixels: pass `useBigEndian = true` to `Arduino_ESP32RGBPanel`. Colours look
  correct today, so this is settled by observation rather than by datasheet.
- **The RP2040 is idle.** It owns the buzzer, the SD slot and the Grove ports,
  and is reachable over UART on GPIO 19/20 (PacketSerial + COBS). Nothing talks
  to it yet, which is why there is no audible alarm. Its ownership of the SD
  slot is no longer unverified - see above.
