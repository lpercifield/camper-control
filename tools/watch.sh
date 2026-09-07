#!/usr/bin/env bash
# Rebuild whenever a source file changes. Prints only errors, warnings and the
# size summary, so a clean pass is one line.
#
# Usage: tools/watch.sh [poll_seconds]   (default 3)
set -o pipefail
PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INTERVAL="${1:-3}"

fingerprint() {
  find "$PROJ/src" "$PROJ/include" "$PROJ/lib" -type f \
       \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
       -printf '%p %T@\n' 2>/dev/null | sort | md5sum
}

last=""
echo "watching $PROJ (every ${INTERVAL}s, ctrl-c to stop)"
while true; do
  now="$(fingerprint)"
  if [ "$now" != "$last" ]; then
    last="$now"
    echo "--- $(date +%H:%M:%S) building"
    if out="$("$PROJ/tools/build.sh" 2>&1)"; then
      echo "$out" | grep -E "warning:|Sketch uses|Global variables" || true
      echo "OK"
    else
      echo "$out" | grep -E "error:|Error|undefined reference" | head -40
      echo "FAILED"
    fi
  fi
  sleep "$INTERVAL"
done
