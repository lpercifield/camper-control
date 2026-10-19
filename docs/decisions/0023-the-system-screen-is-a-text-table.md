# 0023 - Draw the System screen as a text table, not as entity rows

- **Date:** 2026-10-18
- **Status:** Accepted

## Context

`Domain::System` existed in the enum with no page, so anything registered
there was invisible. The obvious fix was to give it the
generic domain page that Lighting and Water already use: one card per entity,
name on the left, value on the right, built lazily and reused.

Built that way and measured on hardware 2026-10-18, the twelve diagnostics
took the LVGL pool from **75% used (15,164 bytes free) to 90% (6,292 free)** -
about 740 bytes per row. The clock setter allocates ~5,500 bytes when the crew
opens it (`decisions/0015`), and the gear is reachable from every page, so
"open System, then set the clock" left under a kilobyte of pool.

That is the failure `decisions/0013` is the record of, and it does not report
itself: LVGL `lv_malloc`s an arc's mask at draw time, never checks the result,
and the board boot loops in a stack trace naming no file of ours.

Twelve 56 px cards do not fit on a 372 px page either, so the obvious version
was also a diagnostics page you had to scroll to read.

## Decision

The System screen is two `lv_label`s - one column of names, one of
right-aligned values, one line per entity, sharing a font and line spacing so
the rows line up. `src/ui/ui.cpp:buildSystem`.

It is built **from the registry**: `refreshSystem` walks
`Registry::inDomain(Domain::System)` and joins the names and the formatted
values with newlines, so an entity registered there by a later integration
appears without the UI changing. That is the whole point of the screen.

Measured the same afternoon: **3,136 bytes resident, 13,252 free and 78% used
with it built, 13,080 and 79% with all twelve lines drawn**, fragmentation
holding at 1% across 26 s of setting those labels at 4 Hz. Four generic rows
would have cost the same.

It lives on an overlay opened from a card in Settings rather than on the nav
bar, which is a separate call and not this record's - `decisions/0009`'s rule
that the nav bar is what the van can do already covers it. Roughly 1.7 KB of
the figure above is that overlay's own title bar and close button, which a
nav page did not need.

## Alternatives rejected

- **The generic entity rows.** Measured above: 8.9 KB, 90% used, and a page
  that scrolls. The consistency was not worth the pool.
- **Build the screen on demand and free it on the way out**, the escape hatch
  `0015` took for the clock setter. It would have worked, and it is still the
  answer for the next big screen - but it costs a close path where every
  cached handle must be nulled synchronously, which has bitten this project
  once and is a lot to buy back 3 KB at 78% used. It is the obvious next move
  if the pool gets tight again.
- **Grow `LV_MEM_SIZE`.** The pool is static internal RAM and the Wi-Fi
  backhaul in `ROADMAP.md` needs ~41 KB of the ~66 KB free heap. Spending pool
  here would quietly spend the backhaul.
- **Fewer figures.** Merging the four integration link rows into one summary
  would have saved three rows and still left the pool near 86%. The row was
  the cost, not the count.
- **Cards with a shared `lv_style_t`** instead of six local style properties
  each. This is probably the real fix for the generic row and it would pay off
  on every page, but it is a refactor of every widget in `ui.cpp` and nothing
  about the System screen needed to wait for it.

## Consequences

**There is no amber for a stale line.** Colour belongs to a widget and these
lines are text inside one, so the project's "stale reads muted" convention does
not reach this page. `SystemInfo` republishes every second so nothing here goes
stale in practice, and an entity that has never reported still prints its own
`--`.

The two columns are aligned by construction - same font, same line spacing,
same number of lines - rather than by a layout engine. A caller that puts a
different font on one of them silently shears the table.

Worth revisiting if a System entity ever needs to be tappable, which a label
line cannot be. At that point the screen wants real rows, and the shared-style
refactor above is the way to afford them.
