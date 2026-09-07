# 0004 - Run the RGB panel without bounce buffers

- **Date:** 2026-09-06
- **Status:** Accepted

## Context

The firmware boot-looped on first run with `Guru Meditation Error: Core 1
panic'ed (Cache disabled but cached memory region accessed)`. The backtrace
decoded to `esp_flash_read` under `nvs::Storage` colliding with
`lcd_rgb_panel_fill_bounce_buffer` under `gdma_default_tx_isr`.

A flash read disables the instruction cache. The RGB panel's DMA end-of-frame
ISR fires inside that window, and it lives in flash because the prebuilt Arduino
libraries ship `CONFIG_LCD_RGB_ISR_IRAM_SAFE` disabled. Bluedroid's NVS reads
are the flash access; any two of the three are fine, all three panic.

Arduino_GFX hardcodes `bounce_buffer_size_px = 40 * w` with no way to disable it
through its API.

## Decision

Patch Arduino_GFX to `bounce_buffer_size_px = 0`. The DMA then reads the
framebuffer directly from PSRAM. At the 12 MHz default pixel clock that is about
24 MB/s, comfortably inside octal PSRAM's budget.

## Alternatives rejected

**`CONFIG_LCD_RGB_ISR_IRAM_SAFE=y`.** The actual upstream fix, and it would keep
the bandwidth headroom bounce buffers buy. Not reachable with prebuilt Arduino
libraries; pioarduino's `custom_sdkconfig` can do it but rebuilds ESP-IDF from
source. Worth doing if PSRAM bandwidth ever becomes the constraint.

**Avoid flash writes while the panel runs.** Fragile - it constrains every
future integration, not just BLE, and NVS-backed settings are on the roadmap.

## Consequences

Arduino_GFX has to be modified somehow, since the setting is not exposed. How
that patch is carried is `decisions/0007`; it is applied at build time by
`tools/patch_gfx.py`.

If PSRAM bandwidth ever becomes the constraint - a higher pixel clock, a second
framebuffer - the bounce buffers are worth having back, and that means building
ESP-IDF from source with `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y`.
