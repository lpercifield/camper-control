# 0014 - Do not touch the flash filesystem while the RGB panel is refreshing

- **Date:** 2026-09-17
- **Status:** Accepted

## Context

The sensor log was to be written to the internal flash filesystem - LittleFS on
the 1.5 MB `spiffs` partition `default_8MB.csv` already reserves - because the
microSD slot belongs to the RP2040 and the ESP32-S3 has no path to it
(`HARDWARE.md`).

It boot-looped the board within seconds of the first filesystem call after the
display came up. Not intermittently: every boot.

```
Guru Meditation Error: Core 1 panic'ed (Cache disabled but cached memory
region accessed).

0x421198bb: gdma_hal_append at hal/gdma_hal_top.c:29
0x40379bb0: lcd_rgb_panel_try_restart_transmission at esp_lcd_panel_rgb.c:1134
 (inlined by) rgb_lcd_default_isr_handler at esp_lcd_panel_rgb.c:1208
0x40377b15: shared_intr_isr
0x4037e1a9: _xt_lowint1                  <- interrupt, mid flash read
0x40388837: spi_flash_hal_poll_cmd_done
0x40375dca: esp_flash_read
0x4205e1d1: esp_partition_read
0x4209ef72: littlefs_esp_part_read
...
src/bsp/storage.cpp:46                   <- ensureDir()
src/integrations/env_sensors.cpp:355     <- openDay()
```

This is `decisions/0004` one layer further down, and the addresses say exactly
why. The Arduino prebuilt libraries ship:

```
CONFIG_LCD_RGB_ISR_IRAM_SAFE=y          the ISR itself is in IRAM   (0x4037...)
CONFIG_GDMA_ISR_IRAM_SAFE=y
CONFIG_LCD_RGB_RESTART_IN_VSYNC=y       so the ISR restarts the DMA
# CONFIG_GDMA_CTRL_FUNC_IN_IRAM is not set   <- but gdma_hal_append is in flash
```

**An IRAM-safe interrupt handler calling a flash-resident function is not
IRAM-safe.** Every flash operation disables the cache; the panel's VSYNC
interrupt fires inside that window at ~60 Hz; it reaches `gdma_hal_append` at
`0x4211...`, which is in the memory-mapped flash region that is currently
switched off.

There is no safe window. `CONFIG_LCD_RGB_RESTART_IN_VSYNC` means every
`draw_bitmap` arms the restart path, and LVGL flushes 84 times a second.

It explains the one thing about `decisions/0004` that never quite added up:
why NVS survived at all. It does not, reliably - a `Preferences` write is one
or two milliseconds and usually slips between VSYNCs. A LittleFS mkdir is tens
of milliseconds and never does. This project has been rolling that die on every
settings change since it started.

## Decision

`CFG_LOG_TO_FLASH` is **0**, and the firmware does not mount or touch the
filesystem after `displayBegin()`. The daily high and low are kept in RAM,
reset at local midnight from the clock, and do not survive a reboot. The
24-hour graph comes from the in-RAM ring it always did.

The format, rotation and replay code stays, behind the flag and under test. It
is not dead weight: the RP2040 route needs exactly the same day files, and it
is the only way to durable storage on this board.

`Settings` keeps writing NVS, because that risk predates this record and the
alternative is no persisted settings at all. It is now a known risk rather than
an unknown one - see Consequences.

## Alternatives rejected

**Write less often.** A flush every five minutes instead of every minute makes
the collision rarer, not impossible. A crash once a week in a van at 3am is the
failure this project exists to avoid, and a rare crash is harder to diagnose
than a reliable one.

**Suspend the LCD interrupt around flash operations.** The correct shape, and
not reachable: the interrupt handle lives inside the `esp_lcd` RGB panel driver,
Arduino_GFX does not expose the panel handle, and a missed VSYNC restart
tears the frame. Worth revisiting only with a patch to Arduino_GFX
(`PORTING_NOTES.md`), not before.

**Set `CONFIG_GDMA_CTRL_FUNC_IN_IRAM`.** The actual fix, one line of sdkconfig,
and unavailable for the same reason as `decisions/0004` and the PSRAM Wi-Fi
option in `ARCHITECTURE.md`: the Arduino libraries are prebuilt. Building a
custom IDF reverses `decisions/0002` and `0006` to add a log file.

**Stop driving the panel from LVGL while writing.** Blanking the screen for
every log write is a worse product than not logging.

## Consequences

The daily high and low reset on every power cycle, and there is no file. That
is a real loss against what was asked for, and the honest position is that
**this board cannot durably log anything on its own** - the storage it has is
behind another chip, and its own flash is behind a broken interrupt.

Two routes out, in order of cost:

- **The RP2040.** It owns the SD slot and is a separate chip, so it has no
  cache to lose. It needs firmware and a PacketSerial/COBS protocol on
  `RP2040_UART_TX/RX`. This is the real answer, and it also unlocks the buzzer
  and the Grove ports that have been idle since the project started.
- **Wi-Fi backhaul.** Ships the readings off the device entirely, and
  `ROADMAP.md` wants it anyway. It does not need local storage to be solved.

And one thing to fix regardless: **`Settings` writes NVS while the panel runs.**
The window is small and the project has been lucky, but this record is the
reason to stop calling it safe. The cheap mitigation is to batch settings writes
to moments the UI is already idle; the real one is the RP2040 or a patched
Arduino_GFX.
