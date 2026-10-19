# 0020 - No over-the-air update until rollback is re-armed

- **Date:** 2026-10-18 (findings dated 2026-09-25)
- **Status:** Accepted.

## Context

Every change so far has been flashed over the CH340 with a laptop in the van.
That is fine on the drive and useless anywhere else, and it decides whether a
fix can ever reach a van that is already out.

The flash layout is not the obstacle. `default_8MB.csv` ships two 3.3 MB app
slots and an `otadata`, and the image is about **50% of one slot**, so both
slots plus the 1.5 MB `spiffs` fit inside 8 MB with room to spare. Nothing
about the partitioning needs changing.

**The obstacle is that the framework disarms rollback before `setup()` runs**,
and nobody would guess it. The prebuilt libraries set both
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` and `CONFIG_APP_ROLLBACK_ENABLE=y`,
so the bootloader *can* revert a bad image. The Arduino core then throws that
away, in `initArduino()`:

```c
if (!verifyRollbackLater()) {            // weak, returns false by default
  if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
    if (verifyOta()) {                   // weak, returns true by default
      esp_ota_mark_app_valid_cancel_rollback();
```

Both hooks are `__attribute__((weak))` with defaults that say "yes, this image
is fine", and they run **before a single line of `setup()`**. An image that
boots and then boot loops is marked valid and the van is stuck, cable-only.

This project has produced exactly that image twice in two weeks - the LVGL
pool exhaustion in `decisions/0013` and the LittleFS cache panic in
`decisions/0014`. Both crashed *after* `setup()` began. Either, shipped over
the air, would have bricked a van until someone arrived with a laptop.

## Decision

**Do not ship OTA until the weak symbols are overridden.**
`verifyRollbackLater()` returns true, and the main loop calls
`esp_ota_mark_app_valid_cancel_rollback()` only once the device has proved
itself.

The health signal already exists and does not need inventing: `displayLoop`'s
heartbeat is this project's own definition of alive - `loops` and `flushes`
both climbing - and `Hub` reports a stalled integration by name. **"Valid
after N heartbeats with flushes climbing"** is the policy, derived from the
debugging protocol in `CLAUDE.md` rather than made up for the occasion.

With that override a bad image costs one reboot. Without it, it costs a
recovery trip.

## Two things that follow, and are not blockers

**Writing 1.7 MB of flash is the `decisions/0014` hazard at scale.** That
record established that any flash access while the RGB panel refreshes panics
the chip; a LittleFS `mkdir` of a few kilobytes did it reliably. An OTA writes
a whole app slot.

The failure is benign, which is the one piece of luck here. `esp_ota_begin()`
erases the target slot and `Update.end()` only repoints `otadata` at the end,
so a panic mid-write leaves the running app untouched and the next boot is the
old firmware. **The risk is an update that never completes, not a brick**,
which makes it safe to attempt before the hazard is solved.

Completing it needs the panel quiet during the write. One hypothesis,
**UNVERIFIED**: `CONFIG_LCD_RGB_RESTART_IN_VSYNC=y` means `draw_bitmap` is
what arms the flash-resident restart path, so if LVGL stops flushing entirely
the interrupt may stop reaching `gdma_hal_append`. The experiment is cheap -
stop calling `lv_timer_handler()`, write a megabyte of scratch to the unused
`spiffs` partition, and see whether it survives. If it does, an OTA is "show a
progress screen, stop the UI, write, reboot" and the hazard never fires. If it
does not, the honest answer is tearing the panel down first, which means
reaching `esp_lcd_panel_del()` through a patch to Arduino_GFX
(`PORTING_NOTES.md`).

**The transport is the easy part.**

| | Needs | Time for 1.7 MB | Note |
|---|---|---|---|
| Wi-Fi SoftAP upload form | Wi-Fi, ~41 KB | seconds | no router, no creds |
| Wi-Fi HTTP pull | Wi-Fi + a server | seconds | wants a release host |
| BLE from a phone | a connection slot | minutes | phone drives the MTU |
| USB cable | nothing | seconds | works today |

SoftAP is the front runner, because an update happens parked and deliberately
and nobody minds that the car's screen is dark for a minute. It used to be
justified by sharing its 41 KB with a phone readings page; `decisions/0016`
removed that, so **it has to justify the radio on its own now**, and it still
can. **Give the AP a WPA2 password**: anyone who can join it can reflash the
van, and `CONFIG_SECURE_BOOT` and flash encryption are both off, so there is
no signature check behind it.

The BLE row was once written as "over an hour", from the 23-byte MTU this
device negotiates **as a central** talking to the BMS. A phone connecting to
us is the central and drives the MTU exchange itself, so that figure does not
apply. **UNVERIFIED** and estimated, not measured.

## Order of work

1. Override the rollback hooks and prove a deliberately-broken image reverts.
   Worth doing **before** any transport exists, because it is the part that
   makes the rest safe, and it is testable over the cable.
2. Run the "does flash work with LVGL stopped" experiment against `spiffs`.
3. Only then pick a transport, on its own merits.

## Consequences

OTA is gated on a few lines of weak-symbol override and one experiment, not on
a transport. Anyone who reaches for the transport first has the order backwards
and is building the dangerous half.
