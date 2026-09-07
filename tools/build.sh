#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Compile the firmware with the arduino-cli toolchain from setup_toolchain.sh.
#
# This is a verification harness, not the project's build system - platformio.ini
# is. It exists so the firmware can be compiled in environments where the
# PlatformIO registry is unreachable. Keep the core version, library versions and
# build flags below in step with platformio.ini; if they drift, a green build
# here stops meaning anything.
#
# Usage: tools/build.sh [-c]        -c = clean build
# -----------------------------------------------------------------------------
set -o pipefail

PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="${CAMPER_TOOLCHAIN:-$HOME/.camper-toolchain}"
SK="$ROOT/sketch/CamperControl"
BUILDPATH="$ROOT/build"

[ -x "$ROOT/cli/arduino-cli" ] || { echo "No toolchain at $ROOT - run tools/setup_toolchain.sh"; exit 1; }
[ "${1:-}" = "-c" ] && rm -rf "$BUILDPATH"

export ARDUINO_DIRECTORIES_DATA="$ROOT/data"
export ARDUINO_DIRECTORIES_DOWNLOADS="$ROOT/data/staging"
export ARDUINO_DIRECTORIES_USER="$ROOT/user"

# arduino-cli insists on a sketch, so build one around src/. It compiles src/
# recursively, which is the whole project minus lib/ (passed as --libraries).
rm -rf "$SK"; mkdir -p "$SK"
printf '// Build harness entry point. All code lives in src/ - see tools/build.sh.\n' > "$SK/CamperControl.ino"
cp -r "$PROJ/src" "$SK/src"

# Mirrors platformio.ini's build_flags.
DEFS="-DBOARD_HAS_PSRAM -DLV_CONF_INCLUDE_SIMPLE"
# arduino-cli compiles a COPY of the sketch under $BUILDPATH/sketch, so the
# include root has to point there. Pointing it at the original makes every header
# reachable by two paths, which quietly defeats #pragma once.
INCS="-I$BUILDPATH/sketch/src -I$PROJ/include"

FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB,FlashMode=qio,USBMode=hwcdc,CDCOnBoot=cdc,DebugLevel=info"

"$ROOT/cli/arduino-cli" compile \
  --fqbn "$FQBN" \
  --warnings "${WARN:-default}" \
  --libraries "$PROJ/lib" \
  --build-path "$BUILDPATH" \
  --build-property "compiler.cpp.extra_flags=$DEFS $INCS" \
  --build-property "compiler.c.extra_flags=$DEFS $INCS" \
  --build-property "compiler.S.extra_flags=$DEFS $INCS" \
  --build-property "runtime.tools.ctags.path=$ROOT/shims" \
  "$SK" 2>&1 | grep -vE "downloads\.arduino\.cc|Error initializing instance|Downloading index"
exit ${PIPESTATUS[0]}
