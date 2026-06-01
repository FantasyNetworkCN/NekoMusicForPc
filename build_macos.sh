#!/bin/bash
# Neko云音乐 macOS Build & Package Script
# Produces: dist/Neko云音乐-{version}-macos-universal.pkg
#
# Builds a single universal .app (arm64 + x86_64) and packages it with pkgbuild.
#
# Prerequisites:
#   xcode-select --install          # clang, pkgbuild, iconutil, lipo
#   cmake
#   Qt 6 for macOS (Desktop) with Multimedia module
#     https://www.qt.io/download-open-source
#   Recommended: Qt built as universal (both arm64 and x86_64) so one link step works.
#
# Usage:
#   ./build_macos.sh              # Release (default)
#   ./build_macos.sh Debug
#
# Environment:
#   QT_MAC_ROOT=/path/to/Qt/6.x.x/macos   Override Qt install root
#   OSX_ARCHITECTURES="x86_64;arm64"      Default: universal
#   OSX_DEPLOYMENT_TARGET=11.0            Minimum macOS version

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build-macos"
BUILD_TYPE="${1:-Release}"
PKG_IDENTIFIER="${PKG_IDENTIFIER:-com.nekomusic.app}"
OSX_DEPLOYMENT_TARGET="${OSX_DEPLOYMENT_TARGET:-11.0}"
OSX_ARCHITECTURES="${OSX_ARCHITECTURES:-x86_64;arm64}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: build_macos.sh must run on macOS (Darwin)."
    exit 1
fi

# ============================================================
# Qt6 macOS path configuration
# ============================================================
QT_MAC_ROOT="${QT_MAC_ROOT:-}"

if [[ -z "$QT_MAC_ROOT" ]]; then
    if [[ -n "${Qt6_DIR:-}" ]]; then
        QT_MAC_ROOT="$(cd "$(dirname "$Qt6_DIR")/.." && pwd)"
    elif command -v brew >/dev/null 2>&1 && brew --prefix qt >/dev/null 2>&1; then
        QT_MAC_ROOT="$(brew --prefix qt)"
    else
        for path in \
            "$HOME/Qt/6.10.2/macos" \
            "$HOME/Qt/6.9.2/macos" \
            "$HOME/Qt/6.8.2/macos" \
            "$HOME/Qt/6.7.3/macos" \
            "/opt/Qt6/macos"; do
            if [[ -d "$path" && -x "$path/bin/qmake" ]]; then
                QT_MAC_ROOT="$path"
                break
            fi
        done
    fi
fi

if [[ -z "$QT_MAC_ROOT" || ! -d "$QT_MAC_ROOT" ]]; then
    echo "ERROR: Qt6 for macOS not found!"
    echo ""
    echo "Install Qt 6 Desktop for macOS, then either:"
    echo "  export QT_MAC_ROOT=\$HOME/Qt/6.x.x/macos"
    echo "  export Qt6_DIR=\$QT_MAC_ROOT/lib/cmake/Qt6"
    echo ""
    echo "Or install via Homebrew: brew install qt"
    exit 1
fi

QT_MAC_ROOT="$(cd "$QT_MAC_ROOT" && pwd)"
MACDEPLOYQT="$QT_MAC_ROOT/bin/macdeployqt"
if [[ ! -x "$MACDEPLOYQT" ]]; then
    MACDEPLOYQT="$(command -v macdeployqt || true)"
fi

# ============================================================
# Check dependencies
# ============================================================
MISSING=""
for cmd in cmake pkgbuild lipo; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        MISSING="$MISSING $cmd"
    fi
done
if [[ -z "$MACDEPLOYQT" || ! -x "$MACDEPLOYQT" ]]; then
    MISSING="$MISSING macdeployqt"
fi

if [[ -n "$MISSING" ]]; then
    echo "ERROR: Missing tools:$MISSING"
    echo ""
    echo "Install Xcode Command Line Tools: xcode-select --install"
    echo "Install Qt 6 and ensure macdeployqt is on PATH or under QT_MAC_ROOT/bin"
    exit 1
fi

echo "========================================="
echo "  Neko云音乐 macOS Build Script"
echo "========================================="
echo "Build type:           $BUILD_TYPE"
echo "Qt macOS:             $QT_MAC_ROOT"
echo "macdeployqt:          $MACDEPLOYQT"
echo "OSX deployment:       $OSX_DEPLOYMENT_TARGET"
echo "OSX architectures:    $OSX_ARCHITECTURES"
echo ""

# ============================================================
# Generate AppIcon.icns (optional but recommended)
# ============================================================
generate_app_icon() {
    local icns_path="$SCRIPT_DIR/src/resources/macos/AppIcon.icns"
    if [[ -f "$icns_path" ]]; then
        echo "Using existing AppIcon.icns"
        return 0
    fi

    local src=""
    for candidate in \
        "$SCRIPT_DIR/packaging/icons/hicolor/512x512/apps/nekomusic.png" \
        "$SCRIPT_DIR/packaging/icons/hicolor/256x256/apps/nekomusic.png"; do
        if [[ -f "$candidate" ]]; then
            src="$candidate"
            break
        fi
    done

    if [[ -z "$src" ]]; then
        local fallback="/System/Library/CoreServices/CoreTypes.bundle/Contents/Resources/GenericApplicationIcon.icns"
        if [[ -f "$fallback" ]]; then
            mkdir -p "$SCRIPT_DIR/src/resources/macos"
            cp "$fallback" "$icns_path"
            echo "Using system fallback icon for AppIcon.icns"
            return 0
        fi
        echo "WARN: No icon source found; building without custom AppIcon.icns"
        return 0
    fi

    if ! command -v iconutil >/dev/null 2>&1 || ! command -v sips >/dev/null 2>&1; then
        echo "WARN: iconutil/sips unavailable; skipping AppIcon.icns generation"
        return 0
    fi

    mkdir -p "$SCRIPT_DIR/src/resources/macos"
    local iconset
    iconset="$(mktemp -d)/AppIcon.iconset"
    mkdir -p "$iconset"
    for size in 16 32 64 128 256 512; do
        sips -z "$size" "$size" "$src" --out "$iconset/icon_${size}x${size}.png" >/dev/null 2>&1 || true
        sips -z $((size * 2)) $((size * 2)) "$src" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null 2>&1 || true
    done
    iconutil -c icns "$iconset" -o "$icns_path" >/dev/null 2>&1 || true
    echo "Generated AppIcon.icns from $(basename "$src")"
}

generate_app_icon

# ============================================================
# Build universal .app
# ============================================================
if [[ -d "$BUILD_DIR" ]]; then
    echo "Cleaning previous build..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

INSTALL_PREFIX="$SCRIPT_DIR/$BUILD_DIR/install"
PKG_OUTDIR="$SCRIPT_DIR/$BUILD_DIR/pkg"

echo "Configuring with CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$QT_MAC_ROOT" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$OSX_DEPLOYMENT_TARGET" \
    -DCMAKE_OSX_ARCHITECTURES="$OSX_ARCHITECTURES"

echo ""
echo "Building..."
cmake --build . -j"$(sysctl -n hw.ncpu)"

echo ""
echo "Installing to $INSTALL_PREFIX ..."
cmake --install . --prefix "$INSTALL_PREFIX"

APP_DIR="$(find "$INSTALL_PREFIX" -type d -name "*.app" -print | head -n1 || true)"
if [[ -z "$APP_DIR" || ! -d "$APP_DIR" ]]; then
    echo "ERROR: No .app bundle found under $INSTALL_PREFIX"
    find "$INSTALL_PREFIX" -maxdepth 4 -print || true
    exit 1
fi
echo "Found app bundle: $APP_DIR"

APP_BIN="$APP_DIR/Contents/MacOS/NekoMusic"
if [[ ! -f "$APP_BIN" ]]; then
    echo "ERROR: Main executable not found: $APP_BIN"
    exit 1
fi

echo ""
echo "Bundling Qt frameworks with macdeployqt..."
"$MACDEPLOYQT" "$APP_DIR" -always-overwrite

echo ""
echo "Verifying universal binary..."
LIPO_INFO="$(lipo -info "$APP_BIN" 2>&1 || true)"
echo "  $LIPO_INFO"

needs_arm=false
needs_x86=false
[[ "$OSX_ARCHITECTURES" == *arm64* ]] && needs_arm=true
[[ "$OSX_ARCHITECTURES" == *x86_64* ]] && needs_x86=true

if $needs_arm && ! grep -q "arm64" <<<"$LIPO_INFO"; then
    echo "ERROR: Expected arm64 in universal binary but it is missing."
    echo "Your Qt installation may be single-arch only. Install a universal Qt,"
    echo "or build on Apple Silicon with a Qt that includes both architectures."
    exit 1
fi
if $needs_x86 && ! grep -q "x86_64" <<<"$LIPO_INFO"; then
    echo "ERROR: Expected x86_64 in universal binary but it is missing."
    echo "Your Qt installation may be arm64-only (common with Homebrew on Apple Silicon)."
    echo "Use the official Qt online installer (universal) or set QT_MAC_ROOT accordingly."
    exit 1
fi

# ============================================================
# Version + pkgbuild
# ============================================================
VERSION=""
VERSION="$(cmake -LA -N . 2>/dev/null | sed -n 's/^PROJECT_VERSION[^=]*=//p' | head -n1 || true)"
if [[ -z "$VERSION" && -f CMakeCache.txt ]]; then
    VERSION="$(grep -E '^PROJECT_VERSION:STATIC=' CMakeCache.txt 2>/dev/null | sed -E 's/.*=//' | head -n1 || true)"
fi
if [[ -z "$VERSION" ]]; then
    VERSION="$(grep -A1 'project(NekoMusic' ../CMakeLists.txt | grep -oE 'VERSION[[:space:]]+[0-9.]+' | grep -oE '[0-9.]+' | head -n1 || true)"
fi
VERSION="${VERSION:-0.0.0}"
SUFFIX=""
FULL_VERSION="${VERSION}${SUFFIX}"

mkdir -p "$PKG_OUTDIR"
PKG_NAME="Neko云音乐-${FULL_VERSION}-macos-universal.pkg"
PKG_PATH="$PKG_OUTDIR/$PKG_NAME"

echo ""
echo "Creating installer package..."
pkgbuild \
    --component "$APP_DIR" \
    --install-location "/Applications" \
    --identifier "$PKG_IDENTIFIER" \
    --version "$FULL_VERSION" \
    "$PKG_PATH"

if [[ ! -f "$PKG_PATH" ]]; then
    echo "ERROR: pkgbuild did not create $PKG_PATH"
    exit 1
fi

OUTPUT_DIR="$SCRIPT_DIR/dist"
mkdir -p "$OUTPUT_DIR"
cp "$PKG_PATH" "$OUTPUT_DIR/"

echo ""
echo "========================================="
echo "  Build Successful!"
echo "========================================="
echo "App bundle:  $APP_DIR"
echo "Installer:   dist/$PKG_NAME"
echo "Binary:      $LIPO_INFO"
echo ""
echo "Install with:"
echo "  open dist/$PKG_NAME"
echo ""
echo "Or:"
echo "  sudo installer -pkg dist/$PKG_NAME -target /"
