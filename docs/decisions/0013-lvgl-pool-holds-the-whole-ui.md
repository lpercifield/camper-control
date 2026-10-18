# 0013 - The LVGL pool holds the whole UI, so size it for the whole UI

- **Date:** 2026-09-17
- **Status:** Accepted

## Context

Adding the Climate detail page - a chart and about twenty widgets - boot-looped
the board. The panic was `StoreProhibited` at `EXCVADDR 0x00000000`, decoding to
`circ_calc_aa4` inside `lv_draw_sw_mask_radius_init`, reached from
`lv_arc_draw`: the Power page's SOC arc.

Nothing was wrong with the arc, and nothing was wrong with the new page. LVGL
draws an arc through an anti-aliasing mask it `lv_malloc`s at draw time and
**does not check the result**, so an exhausted pool presents as a null write
inside a vendored file, in a backtrace that names not one file of this project.

`lv_mem_monitor()` at the end of `ui::begin()`, measured on hardware:

```
after the power page      free 25428
after all pages and nav   free 21060
after the detail page     free 15164
after settings            free  6084
after the rename keyboard free  2564   <- 95% used
```

`LV_MEM_SIZE` was 48 KB, cut from the template's 64 KB early in the project for
a reason recorded as "the RGB framebuffer already owns most of PSRAM" - which is
about PSRAM, and this pool is a static array in internal RAM. The UI had been
living on ~9 KB of slack, and one page spent it.

The structural point: **every screen in this project is built once at boot and
kept for the life of the device.** The pool therefore has to hold the entire UI
simultaneously, not a working set. Its size is a function of how many screens
exist, and it grows every time one is added.

## Decision

`LV_MEM_SIZE` returns to the template's 64 KB. `setup()` logs
`lv_mem_monitor()` after `ui::begin()` and keeps doing so, because the number is
now a budget with a known consumer and the failure mode is silent.

## Alternatives rejected

**Build the modal overlays lazily and delete them on close.** The detail page
(5,896 bytes) and the rename keyboard (3,520 bytes) are both modal and rarely
open; creating them on demand would have returned 9 KB to the pool for free, and
cost no RAM at all. Rejected for now because it trades a static, measurable
allocation for a dynamic one on a pool that already fragments, introduces
lifetime rules for every pointer the overlay caches - `g_detailChart`,
`g_renameInput` and the rest - and would put the allocation that can fail at the
moment a finger lands on the glass instead of at boot, where it is visible. It
is the right answer if RAM ever gets tight; it is not the right answer to a
first crash.

**Leave the pool at 48 KB and make the new page smaller.** It would have fitted,
and the next page would not. The evidence says the budget was the problem, not
the page.

**Check the allocation.** Not ours to check - the null write is inside LVGL's
vendored `lv_draw_sw_mask.c`, and patching it would be `PORTING_NOTES.md` entry
we would carry forever to turn a crash into a differently-broken screen.

## Consequences

16 KB of internal RAM, which is the tight resource. Static RAM goes 33.9% ->
38.9% (110,948 -> 127,332 bytes) and free heap at boot 92.5 KB -> 75.9 KB.
Measured on hardware 2026-09-17, the pool then sits at **18,940 free, 69% used,
1% fragmented** with every screen built.

That still clears Wi-Fi's measured 41 KB (`ARCHITECTURE.md`) by ~35 KB, but the
margin the roadmap was counting on is now smaller. Revisit when the System page
and the Wi-Fi credential screens land - between them they are two more screens
on a pool with roughly two screens of slack, and the lazy-overlay alternative
above is what to reach for then.
