# 0015 - Build the clock setter on demand, and free it on close

- **Date:** 2026-09-17
- **Status:** Accepted

## Context

Setting the clock was first built on the shared text-entry overlay: type
`2026-09-17 14:03` on the keyboard. It is the wrong tool. Setting a clock is
picking five numbers, not writing a sentence, and a sixteen-character date on a
touch keyboard is a fiddle - the `-` and `:` are behind a page switch, and this
gets used by someone who has just plugged the van in and wants the screen to
stop arguing.

The replacement is five columns of up/down steppers. Built the way every other
screen in this project is built - once at boot, kept forever - it left the LVGL
pool at **6,148 bytes free, 90% used**, with the Climate graph still to draw on
top of it. `decisions/0013` is the record of what happens next: at 2,556 free,
the Power page's SOC arc could not allocate its mask and boot-looped the board.

Two thirds of that cost was the widgets themselves: ten `lv_button`s, each with
a child label, is twenty objects.

## Decision

Two changes, in this order, because the second is only needed after the first.

**The stepper rows are two `lv_buttonmatrix` widgets, not ten buttons.** One
object per row instead of ten. Same layout, same touch targets, same events.
That alone returned 4.8 KB, to 10,908 free.

**The clock setter is built when it is opened and freed when it is closed.** It
is the first screen in this project that is not built at boot, and it returns
the remaining 5.5 KB: the pool is back to 16,388 free at boot, exactly where it
was before this screen existed.

`decisions/0013` rejected exactly this and said it was "the right answer if RAM
ever gets tight". It got tight. That record is not superseded - its decision was
the 64 KB pool, which stands, and this is the escape hatch it wrote down.

The clock setter is the right screen to start with: opened perhaps once in the
life of the van, so the rebuild cost is irrelevant, and it caches few pointers.
All of them are nulled **synchronously** in `closeClockSet()`, before the delete
is queued. The delete is `lv_obj_delete_async()` because it runs from inside the
Apply button's own event callback, and freeing a widget's ancestor mid-dispatch
is how LVGL is made to read freed memory.

## Alternatives rejected

**Raise `LV_MEM_SIZE` again.** 64 KB to 80 KB would have fitted it with no
thought. Rejected because `decisions/0013` already spent 16 KB of internal RAM
on this pool and free heap is down to ~75 KB; Wi-Fi needs 41 KB of that and
`ROADMAP.md` is counting on it. Buying pool with the Wi-Fi budget to avoid
writing a destructor is a bad trade.

**Keep the keyboard and make the parsing cleverer.** The complaint was not that
the parsing was strict - it accepted bare digits too. The input method was
wrong.

**Rollers (`lv_roller`) instead of steppers.** Prettier, and a drum you flick
is harder to land on a single value with a thumb than a pair of arrows. Also
heavier: a roller keeps its whole option list.

**Make every overlay lazy at once.** The detail page and the rename keyboard
would return 9 KB more. Not done, because the pool is comfortable again and each
one converted is another set of cached pointers to get right. Convert the next
one when the next screen needs the room, not before.

## Consequences

The pool is back where it was, and this project now has one screen whose
pointers can be null. `refreshClockSet()` and `clockStepCb()` both check.
**Anything that caches a widget handle from this overlay must null it in
`closeClockSet()`**, and that is a rule a future reader has no way to infer -
which is what this record is for.

The open/close cycle was measured on hardware rather than assumed: five cycles
left the pool at 16,296 free and 2% fragmented, unchanged from one cycle. If a
future lazy overlay does not behave that way, it is the one that is wrong.
