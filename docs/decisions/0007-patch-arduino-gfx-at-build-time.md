# 0007 - Patch Arduino_GFX at build time rather than vendoring it

- **Date:** 2026-09-06
- **Status:** Accepted

## Context

`decisions/0004` settled *what* the fix is - `bounce_buffer_size_px = 0`. It did
not settle where that patch lives. It sat in `.pio/libdeps/`, which PlatformIO
owns, so `pio pkg update` or a fresh clone silently restored the boot loop.

The obvious answer, and the one first recommended, was to vendor Arduino_GFX
into `lib/` the way `BleSerialClient` and `OverkillSolarBMS` already are. Then
the numbers arrived: Arduino_GFX is 24 MB, of which `src/` is 18 MB and
`src/font/` alone is 16 MB of CJK bitmap fonts that `Arduino_GFX.h` includes
unconditionally and this project never uses. They cost nothing in flash - the
linker drops them - but they would all land in the repository.

The existing vendored libraries are 20 KB and 124 KB. An 18 MB dependency is not
the same kind of thing.

## Decision

Apply the patch from `tools/patch_gfx.py`, a PlatformIO `pre:` script wired in
through `extra_scripts`. It is idempotent, silent on incremental builds, and
**fails the build loudly** if the line it expects has moved - because a silent
skip would ship a boot-looping binary.

Verified from a clean state: deleting the installed library and rebuilding
reinstalls it, patches it and produces a byte-identical firmware size.

## Alternatives rejected

**Vendor the whole library.** Matches the project's convention and involves no
build-time magic, which is a real virtue. Rejected on size: 18 MB in the repo,
16 MB of it fonts, and every upstream update becomes a manual merge.

**Vendor without `src/font/`.** About 2 MB, but it needs a second permanent
patch to `Arduino_GFX.h` to drop seven `#include` lines. That patch fixes
nothing - it exists only to trim size - and it removes those fonts from the
library's API. Carrying a patch that is not a fix is how patch sets rot.

## Consequences

A script that edits a dependency's source is unusual and easy to overlook.
Mitigated by keeping it to one obvious line, naming the decision record in both
the script and `platformio.ini`, and failing hard when the anchor moves.

Delete the script and the `extra_scripts` line when upstream exposes the setting
or ships the ISR in IRAM.
