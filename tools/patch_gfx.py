"""Run the RGB panel without bounce buffers.

Arduino_GFX hardcodes `bounce_buffer_size_px = 40 * w` with no way to turn it
off through its API. The bounce-buffer refill ISR lives in flash, because the
prebuilt Arduino libraries ship CONFIG_LCD_RGB_ISR_IRAM_SAFE disabled. Any flash
access - Bluedroid's NVS reads, for one - races that ISR and panics with
"Cache disabled but cached memory region accessed", which boot-loops the board.

Setting the size to 0 makes the DMA read the framebuffer straight from PSRAM.
At the 12 MHz default pixel clock that is ~24 MB/s, well inside octal PSRAM's
budget. See docs/decisions/0004.

This is a PlatformIO pre: script rather than a vendored copy of the library: the
fix is one line, and vendoring Arduino_GFX would put 18 MB in the repo, 16 MB of
it CJK fonts we never use. Delete this script and the extra_scripts line if
upstream ever exposes the setting or ships the ISR in IRAM.
"""

import os
import sys

Import("env")  # noqa: F821 - injected by PlatformIO

_TARGET = os.path.join(
    "GFX Library for Arduino", "src", "databus", "Arduino_ESP32RGBPanel.cpp"
)
_ORIGINAL = ".bounce_buffer_size_px = 40 * w,"
_PATCHED = ".bounce_buffer_size_px = 0,  /* camper-control: docs/decisions/0004 */"


def _fail(message):
    sys.stderr.write("\npatch_gfx: %s\n\n" % message)
    env.Exit(1)  # noqa: F821


def main():
    path = os.path.join(
        env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), _TARGET  # noqa: F821
    )

    if not os.path.isfile(path):
        _fail(
            "cannot find %s\n"
            "Dependencies may not be installed yet. Run `pio pkg install`, then\n"
            "build again. Building without this patch boot-loops the board, so\n"
            "this is deliberately an error rather than a warning." % path
        )

    with open(path, "r") as handle:
        source = handle.read()

    if _PATCHED in source:
        return  # already patched; stay quiet on incremental builds

    if _ORIGINAL not in source:
        _fail(
            "expected to find\n    %s\nin %s\nbut it is not there. Arduino_GFX has\n"
            "probably changed. Check whether the bounce buffer can now be\n"
            "configured properly before editing this script - see\n"
            "docs/decisions/0004." % (_ORIGINAL, path)
        )

    with open(path, "w") as handle:
        handle.write(source.replace(_ORIGINAL, _PATCHED, 1))
    print("patch_gfx: disabled RGB bounce buffers (docs/decisions/0004)")


main()
