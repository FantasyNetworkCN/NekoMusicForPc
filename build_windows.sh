#!/bin/bash
# Neko云音乐 Windows Cross-Build Script (Linux -> Windows)
# Produces: Neko云音乐-INSTALLER-win-x64.exe
#
# Prerequisites:
#   sudo apt install g++-mingw-w64-x86-64 nsis
#   Qt6 for Windows MinGW (see setup instructions below)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="build-windows"
BUILD_TYPE="${1:-Release}"

# ============================================================
# Qt6 Windows path configuration
# ============================================================
# You need to download Qt6 for Windows MinGW from:
#   https://download.qt.io/official_releases/qt/
# Or use Qt online installer with Wine
#
# Set the path to your Qt6 Windows installation:
# Example: /opt/qt6-mingw or $HOME/Qt/6.x.x/mingw_64
QT_WIN_ROOT="${QT_WIN_ROOT:-}"

if [ -z "$QT_WIN_ROOT" ]; then
    # Try common paths
    for path in \
        "$HOME/Qt/6.10.2/mingw_64" \
        "$HOME/Qt/6.9.2/mingw_64" \
        "$HOME/Qt/6.8.2/mingw_64" \
        "$HOME/Qt/6.7.3/mingw_64" \
        /opt/qt6-mingw \
        /opt/Qt6-mingw; do
        if [ -d "$path" ]; then
            QT_WIN_ROOT="$path"
            break
        fi
    done
fi

if [ -z "$QT_WIN_ROOT" ] || [ ! -d "$QT_WIN_ROOT" ]; then
    echo "ERROR: Qt6 for Windows MinGW not found!"
    echo ""
    echo "Please download Qt6 for Windows MinGW and set QT_WIN_ROOT:"
    echo "  export QT_WIN_ROOT=/path/to/qt6-mingw"
    echo ""
    echo "Common paths checked:"
    echo "  ~/Qt/6.x.x/mingw_64"
    echo "  /opt/qt6-mingw"
    echo ""
    echo "Download from: https://download.qt.io/official_releases/qt/"
    exit 1
fi

# Convert to absolute path
QT_WIN_ROOT="$(cd "$QT_WIN_ROOT" && pwd)"
echo "Using Qt6 for Windows: $QT_WIN_ROOT"

# Linux Qt6 for host tools (moc, uic, rcc)
QT_HOST_ROOT="${QT_HOST_ROOT:-/usr/lib/qt6}"
echo "Using Qt6 host tools:  $QT_HOST_ROOT"

# ============================================================
# Check dependencies
# ============================================================
MISSING=""
for cmd in x86_64-w64-mingw32-g++ x86_64-w64-mingw32-windres makensis cmake; do
    if ! command -v "$cmd" &>/dev/null; then
        MISSING="$MISSING $cmd"
    fi
done

if [ -n "$MISSING" ]; then
    echo "ERROR: Missing tools:$MISSING"
    echo ""
    echo "Install with:"
    echo "  sudo apt install g++-mingw-w64-x86-64 mingw-w64-x86-64-dev nsis cmake"
    exit 1
fi

MINGW_WIN_H="/usr/x86_64-w64-mingw32/include/windows.h"
if [ ! -f "$MINGW_WIN_H" ]; then
    echo "ERROR: MinGW Windows headers not found ($MINGW_WIN_H)"
    echo ""
    echo "Install with:"
    echo "  sudo apt install mingw-w64-x86-64-dev"
    exit 1
fi

# ============================================================
# Build
# ============================================================
echo "========================================="
echo "  Neko云音乐 Windows Cross-Build Script"
echo "========================================="
echo "Build type:    $BUILD_TYPE"
echo "Qt Windows:    $QT_WIN_ROOT"
echo "Qt Host tools: $QT_HOST_ROOT"
echo ""

# Export for CMakeLists.txt
export QT_HOST_ROOT

# Clean previous build
if [ -d "$BUILD_DIR" ]; then
    echo "Cleaning previous build..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake using cross-compilation toolchain
echo "Configuring with CMake..."
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/cmake/mingw-x64-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$QT_WIN_ROOT" \
    -DCMAKE_FIND_ROOT_PATH="$QT_WIN_ROOT"

# Build
echo ""
echo "Building..."
cmake --build . -j"$(nproc)"

# Verify executable
EXE_PATH="NekoMusic.exe"
if [ ! -f "$EXE_PATH" ]; then
    # Try in subdirectory
    EXE_PATH=$(find . -name "NekoMusic.exe" -type f | head -1)
fi

if [ -z "$EXE_PATH" ] || [ ! -f "$EXE_PATH" ]; then
    echo "ERROR: NekoMusic.exe not found after build!"
    exit 1
fi

echo ""
echo "Built: $EXE_PATH"

# ============================================================
# Deploy Qt dependencies
# ============================================================
echo ""
echo "Deploying Qt dependencies..."

DEPLOY_DIR="deploy"
mkdir -p "$DEPLOY_DIR"

# Copy main executable
cp "$EXE_PATH" "$DEPLOY_DIR/"

# Copy Qt DLLs from Windows Qt installation
QT_BIN="$QT_WIN_ROOT/bin"
if [ ! -d "$QT_BIN" ]; then
    echo "ERROR: Qt bin directory not found: $QT_BIN"
    exit 1
fi

# Required Qt DLLs（与 CMake target_link_libraries 一致；缺任一则安装后无法启动）
QT_DLLS=(
    Qt6Core
    Qt6Gui
    Qt6Widgets
    Qt6Multimedia
    Qt6Network
    Qt6Sql
    Qt6Svg
    Qt6SvgWidgets
    Qt6Concurrent
)

for dll in "${QT_DLLS[@]}"; do
    if [ -f "$QT_BIN/${dll}.dll" ]; then
        cp "$QT_BIN/${dll}.dll" "$DEPLOY_DIR/"
        echo "  Copied ${dll}.dll"
    else
        echo "  ERROR: ${dll}.dll not found under $QT_BIN"
        exit 1
    fi
done

# Qt Multimedia（MinGW 套件）在运行时会加载 FFmpeg，需与 exe 同目录
for pattern in 'avcodec-*.dll' 'avformat-*.dll' 'avutil-*.dll' 'swresample-*.dll' 'swscale-*.dll'; do
    shopt -s nullglob
    for f in "$QT_BIN"/$pattern; do
        cp "$f" "$DEPLOY_DIR/"
        echo "  Copied $(basename "$f")"
    done
    shopt -u nullglob
done

# 图形栈常用同目录依赖（Qt 安装包 bin 内自带；不打包 OpenGL 软渲染）
for extra in d3dcompiler_47.dll; do
    if [ -f "$QT_BIN/$extra" ]; then
        cp "$QT_BIN/$extra" "$DEPLOY_DIR/"
        echo "  Copied $extra"
    fi
done

# 交叉编译链接的是本机 MinGW 运行时，须与 exe 一致（勿假设用户已装 Qt 或自带 PATH）
MINGW_COPY=(libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
for dll in "${MINGW_COPY[@]}"; do
    libpath=$(x86_64-w64-mingw32-g++ --print-file-name="$dll" 2>/dev/null || true)
    if [ -n "$libpath" ] && [ -f "$libpath" ]; then
        cp "$libpath" "$DEPLOY_DIR/"
        echo "  Copied $dll (toolchain)"
    elif [ -f "$QT_BIN/$dll" ]; then
        cp "$QT_BIN/$dll" "$DEPLOY_DIR/"
        echo "  Copied $dll (from Qt bin fallback)"
    else
        echo "  ERROR: $dll not found (MinGW and Qt bin)"
        exit 1
    fi
done

# 若仍漏掉 Qt6*.dll，按 PE 导入表从 Qt bin 补拷（例如将来新增模块）
if command -v x86_64-w64-mingw32-objdump &>/dev/null; then
    while IFS= read -r imp; do
        case "${imp,,}" in
            qt6opengl*.dll|opengl32*.dll)
                echo "  Skip $imp (no OpenGL)"
                ;;
            qt6*.dll)
                if [ -f "$QT_BIN/$imp" ] && [ ! -f "$DEPLOY_DIR/$imp" ]; then
                    cp "$QT_BIN/$imp" "$DEPLOY_DIR/"
                    echo "  Copied $imp (from PE imports)"
                fi
                ;;
        esac
    done < <(x86_64-w64-mingw32-objdump -p "$DEPLOY_DIR/NekoMusic.exe" 2>/dev/null | sed -n 's/.*DLL Name: //p' | sort -u)
fi

# Copy Qt plugins（tls：HTTPS 必需，含 qschannelbackend / qopensslbackend 等）
for plugin in platforms multimedia iconengines imageformats styles translations sqldrivers tls networkinformation generic; do
    if [ -d "$QT_BIN/../plugins/${plugin}" ]; then
        cp -r "$QT_BIN/../plugins/${plugin}" "$DEPLOY_DIR/"
        echo "  Copied plugins/${plugin}"
    fi
done

# OpenSSL DLL（若运行库选用 qopensslbackend；Schannel 后端见 plugins/tls）
find "$QT_WIN_ROOT" -maxdepth 4 \( -name 'libssl-*.dll' -o -name 'libcrypto-*.dll' \) 2>/dev/null | while read -r f; do
    [ -f "$f" ] || continue
    cp "$f" "$DEPLOY_DIR/"
    echo "  Copied $(basename "$f") (OpenSSL)"
done

# ============================================================
# Build NSIS installer
# ============================================================
echo ""
echo "Building NSIS installer..."

cd "$SCRIPT_DIR/packaging"

# Get version
VERSION=$(grep -A1 "project(NekoMusic" "$SCRIPT_DIR/CMakeLists.txt" | grep -oP 'VERSION\s+\K[0-9.]+' | head -1)
SUFFIX=""
FULL_VERSION="${VERSION}${SUFFIX}"

# Temporarily copy deploy files to expected location
DEPLOY_TEMP="$SCRIPT_DIR/build"
rm -rf "$DEPLOY_TEMP"
cp -r "$SCRIPT_DIR/$BUILD_DIR/$DEPLOY_DIR" "$DEPLOY_TEMP"

echo "Running makensis..."
makensis -V2 -DVERSION="$FULL_VERSION" "$SCRIPT_DIR/packaging/nekomusic_installer.nsi"
NSIS_EXIT=$?
echo "makensis exit code: $NSIS_EXIT"

if [ $NSIS_EXIT -ne 0 ]; then
    echo "ERROR: NSIS compilation failed!"
    exit 1
fi

# NSIS OutFile 为 packaging/../*.exe（仓库根目录）；若版本宏展开异常，再按通配兜底
EXE_FILE="$SCRIPT_DIR/Neko云音乐-${FULL_VERSION}-win.exe"
if [ ! -f "$EXE_FILE" ]; then
    EXE_FILE=$(find "$SCRIPT_DIR" -maxdepth 1 -name 'Neko云音乐-*-win.exe' -type f -printf '%T@\t%p\n' 2>/dev/null | sort -n | tail -1 | cut -f2-)
fi

if [ -n "$EXE_FILE" ] && [ -f "$EXE_FILE" ]; then
    OUTPUT_DIR="$SCRIPT_DIR/dist"
    mkdir -p "$OUTPUT_DIR"
    cp "$EXE_FILE" "$OUTPUT_DIR/"
    
    echo ""
    echo "========================================="
    echo "  Build Successful!"
    echo "========================================="
    echo "Output: dist/$(basename "$EXE_FILE")"
else
    echo "ERROR: Installer not created!"
    exit 1
fi
