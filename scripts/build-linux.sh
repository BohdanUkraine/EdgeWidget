#!/usr/bin/env bash
# Builds EdgeWidget on Linux: dist-linux/EdgeWidget-<version>-x86_64.AppImage, one file that runs
# on Arch, Ubuntu, Fedora, ... (Qt inside, linked dynamically: LGPL-friendly).
#
#   scripts/build-linux.sh
#
# Needs: a C++17 compiler, CMake, Qt 6.5+ (system packages or aqtinstall), Qt DBus, and for
# the keyring libsecret headers (Ubuntu: libsecret-1-dev, Fedora: libsecret-devel, Arch: libsecret).
# Build on the oldest distro you want to support: the AppImage needs its glibc or newer.
# Environment:
#   QT_DIR   Qt kit to use, e.g. ~/Qt/6.8.3/gcc_64 (default: the system Qt)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-linux"
APPDIR="$BUILD/AppDir"
DIST="$ROOT/dist-linux"
TOOLS="$BUILD/tools"
VERSION="$(sed -n 's/^project(EdgeWidget VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"

step() { printf '\n==> %s\n' "$1"; }

cmake_args=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr)
if [[ -n "${QT_DIR:-}" ]]; then
    cmake_args+=(-DCMAKE_PREFIX_PATH="$QT_DIR")
    export QMAKE="$QT_DIR/bin/qmake"
    export LD_LIBRARY_PATH="$QT_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
else
    export QMAKE="$(command -v qmake6 || command -v qmake)"
fi

step "Налаштування (CMake)"
cmake -S "$ROOT" -B "$BUILD" "${cmake_args[@]}"

step "Збірка"
cmake --build "$BUILD" --parallel

step "Розгортання в $APPDIR"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD"
mkdir -p "$APPDIR/usr/translations" # the Qt plugin links qtbase_uk.qm in here

# linuxdeploy + its Qt plugin copy Qt (libs, platform/xcb, tls, image plugins) into the AppDir.
step "Інструменти linuxdeploy"
mkdir -p "$TOOLS"
for tool in linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage; do
    if [[ ! -x "$TOOLS/$tool" ]]; then
        repo="${tool%-x86_64.AppImage}"
        curl -fL -o "$TOOLS/$tool" "https://github.com/linuxdeploy/$repo/releases/download/continuous/$tool"
        chmod +x "$TOOLS/$tool"
    fi
done

step "AppImage"
mkdir -p "$DIST"
export PATH="$TOOLS:$PATH"
# Wayland plugin as a fallback for sessions without XWayland, if this Qt has it.
if [[ -f "$("$QMAKE" -query QT_INSTALL_PLUGINS)/platforms/libqwayland-generic.so" ]]; then
    export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so"
fi
export LDAI_OUTPUT="$DIST/EdgeWidget-$VERSION-x86_64.AppImage"
export APPIMAGE_EXTRACT_AND_RUN=1 # works without FUSE (containers, CI)
"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --plugin qt --output appimage \
    --desktop-file "$APPDIR/usr/share/applications/edgewidget.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/edgewidget.png"

printf '\nГотово: %s\n' "$LDAI_OUTPUT"
