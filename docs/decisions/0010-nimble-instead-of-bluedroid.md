# 0010 - NimBLE instead of Bluedroid

- **Date:** 2026-09-07
- **Status:** Accepted. Supersedes the deferral in `decisions/0003`.

## Context

`decisions/0003` kept Bluedroid until the port was proven, so that a failure
during bring-up could not be ambiguous. That reasoning expired: the port is
proven, and Bluedroid had become the thing four separate problems were waiting
on.

1. Free heap was ~55 KB, and the BMS task's stack had just taken 8 KB of it.
2. Bluedroid's `connect()` is synchronous and blocked the cooperative loop for
   up to six seconds, which is what `decisions/0008` had to work around.
3. A BLE temperature sensor needs advertisements, but the scanner stops
   entirely while connected and `BleSerialClient` owns it outright.
4. Wi-Fi needs ~41 KB of internal heap and there were ~48 KB free. Measured
   2026-09-07: bringing Wi-Fi up left 1972 bytes.

## Decision

`BleSerialClient` is rewritten against NimBLE-Arduino 2.5.1. The public
interface is unchanged, so `bms_jbd.cpp` did not move with it, and every
numbered patch in `PORTING_NOTES.md` is preserved.

Measured on hardware, same board, same battery:

| | Bluedroid | NimBLE |
|---|---|---|
| Static RAM | 123,028 | 106,956 |
| Flash | 2,054,600 (61.5%) | 1,644,776 (49.2%) |
| Free heap at boot | 56,724 | **97,116** |
| Time to `online` | 12-19 s | 10 s |

**+40 KB of heap and 400 KB of flash.** Wi-Fi's 41 KB now fits with ~56 KB to
spare, where before it left under 2 KB.

## Two things the port had to learn

**NimBLE will not connect while a scan is running, and `stop()` is
asynchronous.** Connecting straight out of `onResult()` fails every time. The
loop now waits for `isScanning()` to go false.

**HCI 0x3e - "connection failed to be established" - is common and transient.**
Bluedroid reported this as "Unknown ESP_ERR error"; NimBLE gives the code
through `onConnectFail`, which is how it was identified at all. The first
working build retried by rescanning, which costs six seconds per attempt and
took 26 s to connect. Retrying the connect directly - we already know the
address - brought that to under 3 s.

## Alternatives rejected

**Stay on Bluedroid and move the Wi-Fi buffers to PSRAM.**
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` would do it, but the prebuilt Arduino
libraries do not expose sdkconfig - the same wall as `decisions/0004`. It would
also have fixed only the fourth problem of the four.

**Do the shared BLE scanner on Bluedroid first, then port.** That is the same
refactor twice, on the most fragile code in the project.

## Consequences

The Arduino `BLE` library is no longer in the dependency graph at all, which is
where the 400 KB of flash went.

`decisions/0008` - the BMS task - is *not* reversed by this. NimBLE's connect is
still synchronous in the form we call, and the task is now also what keeps the
BLE work off the UI core. The task's stack shows 6,044 bytes of headroom out of
8,192, so it could be trimmed; with 97 KB free there is no reason to take that
risk today.
