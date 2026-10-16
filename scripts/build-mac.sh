#!/usr/bin/env bash
# Builds EdgeWidget on macOS: dist-mac/EdgeWidget.app and dist-mac/EdgeWidget-<version>.dmg.
#
#   scripts/build-mac.sh
#
# Needs Xcode command line tools, CMake and Qt for macOS (universal), e.g. from the Qt
# online installer. Environment:
#   QT_DIR          Qt kit, default ~/Qt/6.8.3/macos
#   SIGN_IDENTITY   "Developer ID Application: ..." to sign for distribution (default: ad-hoc,
#                   runs only on this Mac)
#   NOTARY_PROFILE  notarytool keychain profile (xcrun notarytool store-credentials) to notarize
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT_DIR="${QT_DIR:-$HOME/Qt/6.8.3/macos}"
BUILD="$ROOT/build-mac"
DIST="$ROOT/dist-mac"
APP="$DIST/EdgeWidget.app"
VERSION="$(sed -n 's/^project(EdgeWidget VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
DMG="$DIST/EdgeWidget-$VERSION.dmg"

step() { printf '\n==> %s\n' "$1"; }

[[ -x "$QT_DIR/bin/macdeployqt" ]] || { echo "Qt не знайдено в $QT_DIR (задай QT_DIR)"; exit 1; }

step "Налаштування (CMake)"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_DIR"

step "Збірка"
cmake --build "$BUILD" --parallel

# install runs macdeployqt (Qt frameworks into the bundle) via the deploy script in CMakeLists.txt
step "Розгортання в $APP"
rm -rf "$DIST"
cmake --install "$BUILD" --prefix "$DIST"

step "Підпис"
if [[ -n "${SIGN_IDENTITY:-}" ]]; then
    codesign --force --deep --timestamp --options runtime \
        --entitlements "$ROOT/packaging/mac/EdgeWidget.entitlements" \
        --sign "$SIGN_IDENTITY" "$APP"
else
    # Apple silicon runs nothing unsigned; ad-hoc is enough on this Mac, not for others.
    codesign --force --deep --sign - "$APP"
fi
codesign --verify --deep --strict "$APP"

step "Образ $DMG"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications" # drag-to-install
hdiutil create -volname "Edge Widget" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"

if [[ -n "${SIGN_IDENTITY:-}" ]]; then
    codesign --force --timestamp --sign "$SIGN_IDENTITY" "$DMG"
fi
if [[ -n "${NOTARY_PROFILE:-}" ]]; then
    step "Нотаризація"
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$DMG"
fi

printf '\nГотово: %s\n' "$DMG"
