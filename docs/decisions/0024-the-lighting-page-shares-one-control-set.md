# 0024 - One shared control set for the lights, and a fixed palette

- **Date:** 2026-10-19
- **Status:** Accepted

## Context

Lighting needed to grow from one fixture to four, with colour and brightness
reachable from the panel. Two shapes had to be chosen, and the LVGL pool
decided both.

The obvious layout was a row per fixture with a detail overlay behind each,
the way the Climate page opens a sensor - and it is what was built first.
Measured on hardware 2026-10-19, with the pool at 80% used before it:

| | Free | Used |
|---|---|---|
| Four fixture rows | 5,092 | 92% |
| …plus the detail overlay | **380** | **100%** |

A row was 1,739 bytes and the overlay 4,712. At 100% the board did not boot
loop only because no arc happened to be on screen: an arc's mask is
`lv_malloc`ed at draw time and nothing checks the result, which is
`decisions/0013` and the stack trace in `TROUBLESHOOTING.md` that names no
file of ours.

The colour control had a constraint of its own: **LVGL 9.2 has no colour
wheel.** The widget was dropped after v8, and both ways to rebuild it are
blocked here - a canvas large enough to pick from costs more RAM than the
board has spare, and an arc-based hue ring is the arc-mask hazard above, by
construction.

## Decision

**One set of controls, not one per fixture.** The Lighting page is a fixture
picker across the top - an `lv_buttonmatrix`, so four choices cost one object
- then a single power card, brightness card and colour card acting on
whichever fixture is selected. A summary line under the picker carries every
fixture's state, which is the only thing the per-fixture rows gave that this
does not. `src/ui/ui.cpp:buildLightingPage`.

**Colour is eight presets**, from `core/light_presets.h`, drawn as a second
button matrix. Brightness is a separate axis applied by scaling the chosen
preset, which is the only way this controller does brightness at all
(`decisions/0022`).

Measured, same afternoon: the whole page is **4,212 bytes and does not grow
with the fixture count**, against 11,668 for four rows plus the overlay. The
worst case that can occur - this page with the clock setter open over it -
needed `decisions/0025` to buy back its margin.

## Alternatives rejected

- **A row per fixture with a detail overlay.** Measured above: 100% used. It
  is also the more conventional shape, and that is why it was built first.
- **Rows kept, overlay built on demand.** Still 92% used at rest with four
  fixtures, before the clock setter is opened over the top of it.
- **The page's controls built on demand**, the `decisions/0015` hatch. It
  would have worked, but it churns 4 KB on every tap of the nav bar, and
  `ARCHITECTURE.md` is explicit that repeatedly building and freeing
  fragments this pool. The measurement bears that out: fragmentation read 13%
  with the row-and-overlay version against 1% now.
- **Fewer or cheaper rows.** Dropping the colour chip, or merging the four
  link rows, saved a few hundred bytes each. The row itself was the cost.
- **A hue slider instead of presets.** One object, the whole spectrum, and
  genuinely considered. Rejected because the thing a person actually does at
  3am is pick red so they can find the kettle without waking anybody, and
  that is one tap on a preset against hunting for a hue. The palette can gain
  a hue slider later without the page changing shape.
- **Per-item colour on the swatch matrix.** `lv_buttonmatrix` styles all its
  items together; tinting each one needs an `LV_EVENT_DRAW_TASK_ADDED` hook
  and the item index out of the draw descriptor. Deferred rather than
  rejected - the names are legible in the dark, which the colours would not
  reliably be.

## Consequences

**You cannot see four fixtures' controls at once**, only four fixtures'
states and one fixture's controls. That is the price of the pool, and the
summary line is the mitigation.

A fifth fixture has nowhere to go. `CFG_LIGHTING_MAX_FIXTURES` is a
connection budget, not a layout limit - the picker is sized from it - so
raising it is `platformio.ini` plus the RAM note beside that constant, and
the page follows.

Worth revisiting if a fixture ever needs a control the palette cannot express,
or if the pool gets materially cheaper. The shared-style change to `makeCard`
made while measuring this is the kind of saving that would move it: it cut
every card in the project and bought 560 bytes.
