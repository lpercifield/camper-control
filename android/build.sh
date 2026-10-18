#!/usr/bin/env bash
# Builds the monitor APK straight from the Android SDK tools - no Gradle.
#
# Why: this machine has the SDK (platform 35, build-tools 35.0.0) and a JDK but
# no Gradle, and the app has no third-party dependencies, so the whole Gradle
# layer would exist only to download itself. The UI is built in code and there
# are no resources, which is what makes this possible: aapt2 only has a
# manifest to link.
#
#   ./android/build.sh          builds android/build/campercontrol.apk
#   ./android/build.sh install  builds, then adb install -r
#
# If this ever needs an icon, a layout file or a library, that is the point to
# move to Gradle rather than to keep stretching this.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
BT="$SDK/build-tools/35.0.0"
JAR="$SDK/platforms/android-35/android.jar"
OUT="$HERE/build"
MIN_SDK=31

for t in "$BT/aapt2" "$BT/d8" "$BT/zipalign" "$BT/apksigner" "$JAR"; do
    [ -e "$t" ] || { echo "missing: $t" >&2; exit 1; }
done

# The app and the firmware have to agree on the wire format. The commit hook
# runs this too, but catching it here means you find out before you install a
# build that silently misreads the van. See docs/ICD.md.
if command -v python3 >/dev/null 2>&1; then
    echo "==> checking the ICD"
    python3 "$HERE/../tools/check_icd.py"
fi

rm -rf "$OUT"
mkdir -p "$OUT/classes"

echo "==> linking resources (manifest only)"
"$BT/aapt2" link \
    --manifest "$HERE/AndroidManifest.xml" \
    -I "$JAR" \
    --min-sdk-version "$MIN_SDK" \
    --target-sdk-version 35 \
    -o "$OUT/base.apk"

echo "==> compiling java"
# --release 11 rather than 17: d8 desugars, but keeping the class files to a
# level Android has always handled removes a whole category of "works here,
# crashes on the phone".
# Collected into an array, not word-split from $(find): this repository lives
# under a path with spaces in it and unquoted expansion hands javac half a path.
SOURCES=()
while IFS= read -r -d '' f; do SOURCES+=("$f"); done \
    < <(find "$HERE/src" -name '*.java' -print0)

javac --release 11 \
    -encoding UTF-8 \
    -Xlint:-options \
    -classpath "$JAR" \
    -d "$OUT/classes" \
    "${SOURCES[@]}"

echo "==> dexing"
CLASSES=()
while IFS= read -r -d '' f; do CLASSES+=("$f"); done \
    < <(find "$OUT/classes" -name '*.class' -print0)

"$BT/d8" --min-api "$MIN_SDK" --output "$OUT" "${CLASSES[@]}"

echo "==> packaging"
cp "$OUT/base.apk" "$OUT/unsigned.apk"
(cd "$OUT" && zip -q unsigned.apk classes.dex)
"$BT/zipalign" -f -p 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"

# A debug keystore, made once and kept. This signs a personal sideloaded build;
# it is not a release key and must not be treated as one.
KS="$HERE/debug.keystore"
if [ ! -f "$KS" ]; then
    echo "==> creating a debug keystore"
    keytool -genkeypair \
        -keystore "$KS" -storepass android -keypass android \
        -alias campercontrol -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Camper Control debug, O=Camper Control, C=US"
fi

echo "==> signing"
"$BT/apksigner" sign \
    --ks "$KS" --ks-pass pass:android --key-pass pass:android \
    --min-sdk-version "$MIN_SDK" \
    --out "$OUT/campercontrol.apk" "$OUT/aligned.apk"

rm -f "$OUT/base.apk" "$OUT/unsigned.apk" "$OUT/aligned.apk"
"$BT/apksigner" verify --min-sdk-version "$MIN_SDK" "$OUT/campercontrol.apk" \
    && echo "signature verified"

echo
echo "built: $OUT/campercontrol.apk"
ls -lh "$OUT/campercontrol.apk" | awk '{print "size:  " $5}'

if [ "${1:-}" = "install" ]; then
    echo "==> installing"
    "$SDK/platform-tools/adb" install -r "$OUT/campercontrol.apk"
fi
