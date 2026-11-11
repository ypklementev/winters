#!/usr/bin/env bash
# Builds WinTers for macOS into dist/:
#   dist/WinTers_<version>_macOS.dmg  (drag-to-Applications disk image)
#   dist/WinTers_<version>_macOS.zip  (the same .app, zipped)
#
# Needs only the Xcode Command Line Tools (xcode-select --install) and CMake
# (brew install cmake). The version is set in CMakeLists.txt only.
#
# Options:
#   --arch native|universal   arm64+x86_64 by default; native = this Mac only (faster)
#   --skip-tests              don't build/run the simulation tests
#
# Signing (environment variables):
#   WINTERS_SIGN_IDENTITY   "Developer ID Application: Name (TEAMID)". Without it the app
#                           is ad-hoc signed: it runs on this Mac, but on other Macs
#                           Gatekeeper asks to allow it in System Settings > Privacy & Security.
#   WINTERS_NOTARY_PROFILE  notarytool keychain profile (xcrun notarytool store-credentials).
#                           With it the DMG is notarized and stapled: opens anywhere without warnings.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build/macos-release"
DIST="$ROOT/dist"
ARCHS="arm64;x86_64"
TESTS=ON

while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            case "${2:-}" in
                native) ARCHS="$(uname -m)" ;;
                universal) ARCHS="arm64;x86_64" ;;
                *) echo "unknown --arch '${2:-}' (native|universal)" >&2; exit 2 ;;
            esac
            shift 2 ;;
        --skip-tests) TESTS=OFF; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "build.sh builds the macOS app; on Windows use build.ps1" >&2
    exit 1
fi
command -v cmake >/dev/null || { echo "CMake not found: brew install cmake" >&2; exit 1; }

IDENTITY="${WINTERS_SIGN_IDENTITY:-}"
NOTARY="${WINTERS_NOTARY_PROFILE:-}"
if [[ -n "$NOTARY" && -z "$IDENTITY" ]]; then
    echo "WINTERS_NOTARY_PROFILE needs WINTERS_SIGN_IDENTITY (notarization requires a Developer ID signature)" >&2
    exit 1
fi

step() { printf '\n\033[1m> %s\033[0m\n' "$*"; }

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
step "Configure ($ARCHS)"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=$ARCHS" \
    -DWINTERS_BUILD_TESTS=$TESTS -DWINTERS_MAC_ADHOC_SIGN=OFF
step "Build"
cmake --build "$BUILD" --parallel
if [[ "$TESTS" == ON ]]; then
    step "Test"
    ctest --test-dir "$BUILD" --output-on-failure
fi

VERSION="$(cat "$BUILD/version.txt")"
APP_NAME="WinTers.app"
STAGE="$BUILD/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/dmg" "$DIST"
APP="$STAGE/dmg/$APP_NAME"
cp -R "$BUILD/$APP_NAME" "$APP"
echo "Architectures: $(lipo -archs "$APP/Contents/MacOS/WinTers")"

# ---------------------------------------------------------------------------
# Sign
# ---------------------------------------------------------------------------
if [[ -n "$IDENTITY" ]]; then
    step "Sign: $IDENTITY"
    # Hardened runtime + secure timestamp: required for notarization.
    codesign --force --options runtime --timestamp --sign "$IDENTITY" "$APP"
else
    step "Sign: ad-hoc (set WINTERS_SIGN_IDENTITY for a Developer ID signature)"
    codesign --force --sign - --timestamp=none "$APP"
fi
codesign --verify --strict --verbose=1 "$APP"

# ---------------------------------------------------------------------------
# Package
# ---------------------------------------------------------------------------
ZIP="$DIST/WinTers_${VERSION}_macOS.zip"
DMG="$DIST/WinTers_${VERSION}_macOS.dmg"
rm -f "$ZIP" "$DMG"

step "DMG"
ln -s /Applications "$STAGE/dmg/Applications"
hdiutil create -volname "WinTers $VERSION" -srcfolder "$STAGE/dmg" -fs HFS+ -format UDZO -ov "$DMG" >/dev/null
[[ -n "$IDENTITY" ]] && codesign --force --timestamp --sign "$IDENTITY" "$DMG"

if [[ -n "$NOTARY" ]]; then
    step "Notarize (takes a few minutes)"
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY" --wait
    xcrun stapler staple "$DMG"
    xcrun stapler staple "$APP"  # so the zip below is stapled too
fi

step "ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"

printf '\n\033[1;32mWinTers %s (macOS)\033[0m\n  %s\n  %s\n' "$VERSION" "$DMG" "$ZIP"
