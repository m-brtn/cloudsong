#!/usr/bin/env bash
# Packages Cloudsong into dist/.
#
#   tools/package.sh            Release build, ctest, dist/Cloudsong-<version>-<hash>-macos.zip with Cloudsong.app,
#                               CREDITS.md and the engine's LICENSE and THIRD_PARTY_LICENSES; then unzips it, runs
#                               the smoke scenario from the unzipped app (offscreen) and saves a capture next to it
#   tools/package.sh --ios      also builds the iOS app (ios-xcode, Automatic signing by a paid team: --team ID or
#                               QUENCH_IOS_TEAM) into dist/Cloudsong-<version>-<hash>-ios/cloudsong.app; installs nothing
#   --no-checks                 skips ctest
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"
IOS=0
CHECKS=1
TEAM="${QUENCH_IOS_TEAM:-}"
while [[ $# -gt 0 ]]; do
    case "$1" in
    --ios) IOS=1 ;;
    --team) TEAM="${2:?--team ID}"; shift ;;
    --no-checks) CHECKS=0 ;;
    -h | --help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

step() { printf '\n=== %s\n' "$*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }

VERSION="$(sed -n 's/^project(cloudsong VERSION \([0-9][0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[[ -n "$VERSION" ]] || fail "cannot read the version from CMakeLists.txt"
HASH="$(git -C "$ROOT" rev-parse --short HEAD)"
[[ -z "$(git -C "$ROOT" status --porcelain)" ]] || HASH="$HASH-dirty"
NAME="Cloudsong-$VERSION-$HASH"
BUILD="$ROOT/build/release"
EXE="$BUILD/cloudsong"

step "Cloudsong $VERSION ($HASH): release build"
cmake --preset release >/dev/null
cmake --build --preset release
ENGINE="$(sed -n 's/^FETCHCONTENT_SOURCE_DIR_QUENCH:PATH=//p' "$BUILD/CMakeCache.txt")"
[[ -n "$ENGINE" ]] || ENGINE="$BUILD/_deps/quench-src"
[[ -f "$ENGINE/THIRD_PARTY_LICENSES" ]] || fail "no engine checkout at $ENGINE"
echo "engine: $ENGINE"

if [[ $CHECKS -eq 1 ]]; then
    step "ctest"
    (cd "$ROOT" && ctest --preset release -j4)
fi

step "assemble Cloudsong.app"
STAGE="$DIST/$NAME-macos"
APP="$STAGE/Cloudsong.app"
ZIP="$DIST/$NAME-macos.zip"
rm -rf "$STAGE" "$ZIP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$EXE" "$APP/Contents/MacOS/cloudsong"
# Plain and cooked assets go to Resources: SDL_GetBasePath() of a bundled app.
cp -R "$BUILD/assets" "$APP/Contents/Resources/assets"
for f in "$ENGINE/LICENSE" "$ENGINE/THIRD_PARTY_LICENSES" "$ROOT/CREDITS.md"; do
    cp "$f" "$APP/Contents/Resources/"
    cp "$f" "$STAGE/"
done

ICONSET="$(mktemp -d -t cloudsong_icon)/Cloudsong.iconset"
mkdir -p "$ICONSET"
for size in 16 32 128 256 512; do
    sips -s format png -z $size $size "$ROOT/icon/cloudsong-daylight.png" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
    sips -s format png -z $((size * 2)) $((size * 2)) "$ROOT/icon/cloudsong-daylight.png" \
        --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/Cloudsong.icns"
rm -rf "$(dirname "$ICONSET")"

# LaunchServices refuses an app whose LSMinimumSystemVersion is above the running macOS: use the binary's minos.
MIN_BINARY="$(vtool -show-build "$APP/Contents/MacOS/cloudsong" | awk '$1 == "minos" { print $2; exit }')"
[[ -n "$MIN_BINARY" ]] || fail "cannot read the binary minos"
cat >"$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleDisplayName</key><string>Cloudsong</string>
    <key>CFBundleExecutable</key><string>cloudsong</string>
    <key>CFBundleIconFile</key><string>Cloudsong</string>
    <key>CFBundleIdentifier</key><string>org.quench.cloudsong</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>CFBundleName</key><string>Cloudsong</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$VERSION</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.adventure-games</string>
    <key>LSMinimumSystemVersion</key><string>$MIN_BINARY</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF
plutil -lint "$APP/Contents/Info.plist" >/dev/null || fail "Info.plist is not valid"
cmp -s "$EXE" "$APP/Contents/MacOS/cloudsong" || fail "bundle executable differs from the build"
if otool -L "$APP/Contents/MacOS/cloudsong" | tail -n +2 | awk '{print $1}' | grep -vE '^(/System/Library/|/usr/lib/)'; then
    fail "non-system dynamic dependency"
fi
codesign --force --deep -s - "$APP"
codesign --verify --deep --strict "$APP"

step "zip"
ditto -c -k --norsrc --noextattr --noqtn --keepParent "$STAGE" "$ZIP"
if zipinfo -1 "$ZIP" | grep -E '(^|/)\._'; then fail "AppleDouble ._ files in $ZIP"; fi
ls -l "$ZIP"
shasum -a 256 "$ZIP"

step "smoke run from a fresh unzip (offscreen)"
UNZIP="$(mktemp -d -t cloudsong_unzip)"
unzip -q "$ZIP" -d "$UNZIP"
UNZIPPED="$UNZIP/$NAME-macos/Cloudsong.app"
codesign --verify --deep --strict "$UNZIPPED"
CAPTURE="$DIST/$NAME-macos-smoke.png"
LOG="$DIST/$NAME-macos-smoke.log"
rm -f "$CAPTURE"
(cd / && "$UNZIPPED/Contents/MacOS/cloudsong" --frame-size 1280x720 --audio-device null \
    --user-dir "$UNZIP/user" --script "$ROOT/scenarios/smoke.txt" --capture "$CAPTURE" --capture-at 2) \
    >"$LOG" 2>&1 || { cat "$LOG"; fail "smoke run failed (log: $LOG)"; }
grep -E "scenario: .*: [0-9]+ passed, 0 failed" "$LOG" || { cat "$LOG"; fail "smoke scenario did not pass"; }
[[ -s "$CAPTURE" ]] || fail "smoke run wrote no capture"
rm -rf "$UNZIP"
echo "capture (look at it): $CAPTURE"

if [[ $IOS -eq 1 ]]; then
    step "iOS: ios-xcode (Automatic signing)"
    [[ -n "$TEAM" ]] || fail "no team: --team ID or QUENCH_IOS_TEAM"
    cmake --preset host-tools >/dev/null
    cmake --build --preset host-tools
    cmake --preset ios-xcode -DQUENCH_IOS_TEAM="$TEAM" -DQUENCH_IOS_BUNDLE_ID_SUFFIX= >/dev/null
    XBUILD="$ROOT/build/ios-xcode"
    xcodebuild -project "$XBUILD/cloudsong.xcodeproj" -scheme cloudsong -configuration Release \
        -destination generic/platform=iOS -allowProvisioningUpdates -quiet build
    IOS_APP="$(find "$XBUILD" -name cloudsong.app -path '*Release-iphoneos*' -maxdepth 3 | head -n 1)"
    [[ -d "$IOS_APP" ]] || fail "no cloudsong.app under $XBUILD"
    codesign --verify --deep --strict "$IOS_APP"
    SIGNATURE="$(codesign -dvv "$IOS_APP" 2>&1)"
    grep -qx "TeamIdentifier=$TEAM" <<<"$SIGNATURE" || fail "the iOS app is not signed by the team"
    [[ -f "$IOS_APP/embedded.mobileprovision" ]] || fail "the iOS app has no provisioning profile"
    rm -rf "$DIST/$NAME-ios"
    mkdir -p "$DIST/$NAME-ios"
    cp -R "$IOS_APP" "$DIST/$NAME-ios/"
    echo "iOS app: $DIST/$NAME-ios/cloudsong.app"
fi
