# 0006 - PlatformIO is the build system; the arduino-cli harness is not

- **Date:** 2026-09-06
- **Status:** Accepted

## Context

The project carries two ways to build: `platformio.ini`, and `tools/build.sh`
driving arduino-cli. The second exists because the sandbox the port was
developed in could not reach the PlatformIO package registry.

`PORTING_NOTES.md` warned that if the two drift, a green build in the harness
stops meaning anything. They drifted: the harness compiled clean while
PlatformIO failed on `Network.h`, because arduino-cli puts every core library on
the include path and never has to resolve the dependency.

The harness is also Linux-only - `setup_toolchain.sh` fetches only the
`Linux_64bit` arduino-cli asset - so it cannot be run on the development machine
to check.

## Decision

`platformio.ini` is the build system. It is the only build that must pass. The
arduino-cli harness is documented as unmaintained and its green result carries
no weight.

## Alternatives rejected

**Keep both in step by hand.** Already tried; already failed. The failure mode
is silent and produces false confidence, which is worse than no second build.

**Delete `tools/` outright.** Tempting, and still the right end state. Held off
only because the sandbox constraint that created it may recur.

## Consequences

`tools/` is dead weight that looks alive. Anyone reading the README could
reasonably try it and waste an afternoon on a Mac, which is why the README and
`ROADMAP.md` both label it Linux-only. If it is ever wanted as a real second
opinion, it belongs in CI where it runs on every commit - otherwise it should
go.
