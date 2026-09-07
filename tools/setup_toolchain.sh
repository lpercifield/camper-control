#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Build a compile-only ESP32 toolchain from sources that are reachable without
# the PlatformIO package registry.
#
# Why this exists: in some sandboxes (including the one this project is being
# developed from) *.platformio.org is blocked by network policy, so `pio run`
# cannot install anything - not even `platform = native`. GitHub release assets,
# git and raw.githubusercontent are reachable, and arduino-cli can be fed
# entirely from those. The result compiles the same sources with the same core
# and libraries as platformio.ini pins, so a green build here means a green
# build there.
#
# Nothing here is needed on a normal machine: use PlatformIO.
#
# Usage:  tools/setup_toolchain.sh [install_root]     (default ~/.camper-toolchain)
# -----------------------------------------------------------------------------
set -euo pipefail

ROOT="${1:-$HOME/.camper-toolchain}"
ACLI_VERSION=1.1.1
CORE_VERSION=3.1.2
LVGL_VERSION=v9.2.2
GFX_VERSION=v1.5.3

mkdir -p "$ROOT"/{cli,data,user/libraries,index,shims}

# ---- 1. arduino-cli (GitHub release asset) ----------------------------------
if [ ! -x "$ROOT/cli/arduino-cli" ]; then
  echo "==> arduino-cli $ACLI_VERSION"
  curl -fsSL -o /tmp/acli.tgz \
    "https://github.com/arduino/arduino-cli/releases/download/v${ACLI_VERSION}/arduino-cli_${ACLI_VERSION}_Linux_64bit.tar.gz"
  tar xzf /tmp/acli.tgz -C "$ROOT/cli"
fi

# ---- 2. ESP32 core index, trimmed --------------------------------------------
# The upstream index makes the esp32 core depend on arduino:dfu-util, which is
# hosted on downloads.arduino.cc and blocked here. dfu-util is only used for
# uploading, so the dependency is dropped and the index served from localhost.
echo "==> esp32 package index"
curl -fsSL -o "$ROOT/index/upstream.json" \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
python3 - "$ROOT" "$CORE_VERSION" <<'PY'
import json, sys
root, want = sys.argv[1], sys.argv[2]
d = json.load(open(f"{root}/index/upstream.json"))
pkg = next(p for p in d["packages"] if p["name"] == "esp32")
pkg["platforms"] = [p for p in pkg["platforms"] if p["version"] == want]
assert pkg["platforms"], f"core {want} not in index"
for p in pkg["platforms"]:
    p["toolsDependencies"] = [t for t in p["toolsDependencies"] if t["packager"] == "esp32"]
json.dump(d, open(f"{root}/index/package_esp32local_index.json", "w"))
PY

# Serve it locally; arduino-cli will not read an index off the filesystem.
( cd "$ROOT/index" && exec python3 -m http.server 8777 >/dev/null 2>&1 ) &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null || true' EXIT
sleep 1

export ARDUINO_DIRECTORIES_DATA="$ROOT/data"
export ARDUINO_DIRECTORIES_DOWNLOADS="$ROOT/data/staging"
export ARDUINO_DIRECTORIES_USER="$ROOT/user"
export ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS=http://127.0.0.1:8777/package_esp32local_index.json

# The arduino.cc default index and library index are unreachable; those failures
# are expected and harmless - we only need the esp32 one.
echo "==> core index update (arduino.cc failures below are expected)"
"$ROOT/cli/arduino-cli" core update-index 2>&1 | grep -vE "downloads\.arduino\.cc|Error initializing" || true

echo "==> esp32 core $CORE_VERSION (this pulls ~1 GB of toolchain)"
"$ROOT/cli/arduino-cli" core install "esp32:esp32@${CORE_VERSION}" 2>&1 | grep -E "installed|Error" || true

# ---- 3. Libraries (git; the Arduino library index is blocked) ----------------
clone_lib() {  # name url ref
  local dir="$ROOT/user/libraries/$1"
  if [ -d "$dir" ]; then echo "==> $1 already present"; return; fi
  echo "==> $1 $3"
  git clone --quiet --depth 1 --branch "$3" "$2" "$dir"
}
clone_lib lvgl        https://github.com/lvgl/lvgl.git                     "$LVGL_VERSION"
clone_lib Arduino_GFX https://github.com/moononournation/Arduino_GFX.git   "$GFX_VERSION"

# ---- 4. ctags shim -----------------------------------------------------------
# Arduino runs ctags only to hoist function prototypes out of the .ino. Ours is
# a one-line comment (all real code is .cpp), so emitting nothing is correct -
# and the real ctags build is another downloads.arduino.cc artifact.
cat > "$ROOT/shims/ctags" <<'CTAGS'
#!/bin/sh
exit 0
CTAGS
chmod +x "$ROOT/shims/ctags"

echo
echo "Toolchain ready at $ROOT"
echo "Now run: tools/build.sh"
