#!/usr/bin/env python3
"""Capture the board's serial log for a fixed number of seconds.

Why this exists rather than `pio device monitor`: miniterm wants a real TTY on
stdin and dies with `termios.error: (102, 'Operation not supported on socket')`
when run non-interactively, which makes it useless in scripts. And `cat` does
not work either - macOS reapplies default termios when a process opens
/dev/cu.*, so any preceding `stty` is discarded and you read garbage at 9600.

Note that opening the port toggles DTR/RTS and therefore RESETS the board. Every
capture restarts the firmware and drops any BLE session; that is expected. We
clear both lines immediately so the chip is not held in reset while we read.

Needs pyserial. PlatformIO ships one:

    $(ls /opt/homebrew/Cellar/platformio/*/libexec/bin/python | head -1) \
        tools/readlog.py /dev/cu.usbserial-1110 --seconds 15
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit(
        "pyserial not found. Use PlatformIO's interpreter, e.g.\n"
        "  $(ls /opt/homebrew/Cellar/platformio/*/libexec/bin/python | head -1) "
        "tools/readlog.py ..."
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("port", help="e.g. /dev/cu.usbserial-1110 (the CH340, not the RP2040)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=15.0)
    ap.add_argument("--out", help="also write the capture to this file")
    args = ap.parse_args()

    port = serial.Serial(args.port, args.baud, timeout=0.2)
    port.dtr = False
    port.rts = False

    deadline = time.time() + args.seconds
    chunks = []
    while time.time() < deadline:
        chunks.append(port.read(4096))
    port.close()

    text = b"".join(chunks).decode("utf-8", "replace")
    sys.stdout.write(text)
    if args.out:
        with open(args.out, "w") as handle:
            handle.write(text)


if __name__ == "__main__":
    main()
