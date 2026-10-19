# 0025 - The LVGL pool grows to 68 KB

- **Date:** 2026-10-19
- **Status:** Accepted. Supersedes the 64 KB figure in `decisions/0013`.

## Context

`decisions/0013` set `LV_MEM_SIZE` to 64 KB and established why the number
matters: every screen is built once at boot and kept, so this pool holds the
whole UI rather than a working set, and exhausting it does not report itself.
That premise is unchanged and is the reason this record exists.

What changed is that the UI reached the end of the 64 KB. Measured on hardware
2026-10-19, with the Lighting page of `decisions/0024` built:

| | Free | Used |
|---|---|---|
| Everything built, Lighting page shown | 8,396 | 86% |
| …plus the clock setter open over it | **2,920** | **95%** |

That second row is reachable by a crew standing on the Lighting page tapping
the gear and then Set, and 95% is the condition `0013` wrote down as the one
not to ship. It did not crash in the probe, which is exactly the problem:
the failure is an arc mask `lv_malloc`ed at draw time with the result
unchecked, so it depends on which screen is in front rather than on the
arithmetic.

The Lighting page had already been cut from 11,668 bytes to 4,212 by the time
this was measured (`0024`), and `makeCard` had been changed to share one
`lv_style_t` instead of setting five local properties per card, which bought
560 bytes across about thirty cards. The page is not the slack.

## Decision

`LV_MEM_SIZE` becomes **68 KB**, in `include/lv_conf.h`.

Four kilobytes, not more. The target was the margin that existed *before* the
Lighting page, so that the feature is pool-neutral at the peak rather than
comfortable at the Wi-Fi backhaul's expense:

| | Free | Used |
|---|---|---|
| Before Lighting, clock setter open | 6,548 | 90% |
| **After, clock setter open** | **6,884** | **89%** |
| After, at rest on the Lighting page | 12,360 | 81% |

## Alternatives rejected

- **Leave it at 64 KB and accept 95%.** This is what `decisions/0023`
  rejected growing the pool *for*, and that reasoning still stands for the
  case it was about - a page that could be made cheap instead. It does not
  stand once the page has been made cheap and the peak is still 95%.
- **Grow it by 8 KB or more.** Free heap is ~62 KB after this change and the
  Wi-Fi backhaul measured 41,392 bytes (`decisions/0019`), leaving ~20 KB. A
  second 4 KB would cut that to ~16 KB, which is below what the Wi-Fi and BLE
  stacks want spare on this chip. 4 KB keeps both futures open.
- **Build the Lighting page's controls on demand** and free them on the way
  out. Rejected in `0024`: it churns 4 KB on every nav tap and the
  fragmentation showed up in the measurement.
- **Move the pool into PSRAM** with `LV_MEM_POOL_EXPAND_SIZE` or a custom
  address. The board has 8 MB of octal PSRAM and this would end the argument
  permanently - but LVGL draws from this pool on every frame, PSRAM is slower
  than internal SRAM, and the flush path is already the thing holding 84
  flushes a second. Not tried, and the honest reason is that it needs a
  measurement nobody has taken. **This is the first thing to try if the pool
  binds again.**

## Consequences

Static RAM goes from 130,892 to 135,004 bytes (39.9% to 41.2%) and free heap
at boot from ~65.8 KB to ~61.8 KB. The Wi-Fi backhaul in `ROADMAP.md` still
fits, with ~20 KB rather than ~25 KB to spare, and that is now the number to
check before the next screen.

`0013`'s rules all still apply and are what make this safe to have done:
`setup()` logs `lv_mem_monitor()` after `ui::begin()`, `showDomain()` logs it
on every change of domain, and the escape hatch for a screen that does not
fit is still to build it on demand (`decisions/0015`).

**The next screen is not free.** At 81% at rest there is roughly one more
Climate-sized page in the pool, and the Wi-Fi credential screens
`ROADMAP.md` wants are two. Read the figure the board logs before designing
the third.
