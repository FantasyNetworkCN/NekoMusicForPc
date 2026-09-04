# 编译指南（太长不看版）

面向从源码构建 **Neko歌姬计划 PC 版（Flutter）** 的完整说明。
脚本入口统一为 `scripts/`，CI 与本地同一套脚本。

## 目录

- [0. 总览](#0-总览)
- [1. 通用依赖](#1-通用依赖所有平台)
- [2. Linux](#2-linux)
- [3. macOS](#3-macos)
- [4. Windows](#4-windows)
- [5. 手动构建（不依赖脚本）](#5-手动构建不依赖脚本逐步执行)
- [6. 运行 & 开发](#6-运行--开发)
- [7. 常见问题（踩坑记录）](#7-常见问题踩坑记录)
- [8. CI](#8-ci)

---

## 0. 总览

| 平台 | 构建入口 | 产物 |
|------|----------|------|
| Linux   | `scripts/build.sh [debug\|release]` | `flutter/build/linux/x64/<mode>/bundle/` |
| macOS   | `scripts/build.sh release`          | `.app`（`Contents/Frameworks` 内置 Qt/mpv） |
| Windows | `scripts\build.bat [debug\|release]` | `flutter\build\windows\x64\runner\<mode>\` |

> 引擎（`libneko_engine` + `libneko_core`）会在 Flutter 构建前由同一脚本先行编译。

---

## 1. 通用依赖（所有平台）

| 依赖 | 要求 | 说明 |
|------|------|------|
| Flutter SDK | **3.44+**（官方稳定版即可，无需补丁） | 推荐 3.47.x，安装见 [§5-A](#5-手动构建不依赖脚本逐步执行) |
| CMake       | ≥ 3.20 | 引擎与平台工程 |
| Ninja       | 任意近期版 | 生成器 |
| Git         | — | — |

> ⚠️ **必须使用 `--no-tree-shake-icons`**（脚本已内置）。
> Release 构建默认会对图标字体做 tree-shaking 子集化，
> 会把自绘的 `neko_icons.ttf` 裁掉部分字形，导致图标缺失/空白。

---

## 2. Linux

### 2.1 系统依赖

**Debian / Ubuntu**

```bash
sudo apt update
sudo apt install -y cmake ninja-build qt6-base-dev libmpv-dev \
  libayatana-appindicator3-dev gir1.2-ayatanaappindicator3-0.1 \
  clang pkg-config libgtk-3-dev mpv zstd
```

**Arch**

```bash
sudo pacman -S --needed cmake ninja clang qt6-base mpv gtk3 \
  libayatana-appindicator zstd
```

> - Qt 组件需求：`Core / Network / Sql / Gui`（核心桥 `neko_core.so` 使用）
> - `libayatana-appindicator` 为托盘所需（缺失时托盘降级为“直接退出”）

### 2.2 构建

```bash
./scripts/build.sh release
# 产物: flutter/build/linux/x64/release/bundle/
#       （在 bundle 目录内运行: ./neko_music）
```

### 2.3 打包

```bash
bash scripts/package-deb.sh      # .deb
bash scripts/package-rpm.sh      # .rpm
bash scripts/package-arch.sh     # .pkg.tar.zst
bash scripts/package-portable.sh # 便携 tar.gz
bash scripts/package.sh          # 聚合入口
# AppImage / Flatpak 由 CI 产出（见 .github/workflows/build-linux-flutter.yml）
```

---

## 3. macOS

### 3.1 依赖（Homebrew）

```bash
brew install cmake ninja mpv qt dylibbundler
```

> 需要 Xcode 命令行工具：`xcode-select --install`。

### 3.2 构建

```bash
./scripts/build.sh release
```

脚本末尾自动完成后处理：

1. CMake 编译引擎 → 拷贝 `libneko_engine.dylib` / `libneko_core.dylib` 进 `Contents/Frameworks`
2. `macdeployqt`（**默认插件**：QtSql `sqldrivers`、QtGui `imageformats`、QtNetwork `tls`）
3. `dylibbundler` 把 libmpv 及其依赖闭包拷入，改写为 `@executable_path/../Frameworks`
4. **ad-hoc 签名**整个 `.app`（Apple Silicon 上 unsigned dylib 会被拒绝加载）

产物：`flutter/build/macos/Build/Products/Release/*.app`

> 首次打开如被 Gatekeeper 拦截：`xattr -cr xxx.app`

---

## 4. Windows

### 4.1 依赖

| 组件 | 获取 |
|------|------|
| Visual Studio 2022（含 “C++ 桌面开发” 工作负载） | 官方安装器 |
| Qt 6.8.x（msvc2022_64） | [qt.io](https://www.qt.io/download) 或 `aqtinstall`；记下根目录 |
| libmpv 开发包 | [mpv-winbuild](https://github.com/shinchiro/mpv-winbuild-cmake/releases)（`mpv-dev-x86_64-*.7z`）；需含 `include/mpv/client.h`，并把包内 `.a`/`.lib` 改名/拷贝为 `lib\mpv.lib` 供 MSVC 链接 |
| NSIS（仅打包安装器需要） | `choco install nsis` |

环境变量：

```bat
set MPV_DIR=D:\path\to\mpv-dev
set NEKO_FLUTTER=D:\path\to\flutter\bin\flutter   (可选)
```

### 4.2 构建

在 **x64 Native Tools Command Prompt**（或已运行 `vcvars64`）中：

```bat
scripts\build.bat release
```

脚本自动：清空并重建引擎（Ninja + MSVC）→ `flutter build windows --release --no-tree-shake-icons`。

### 4.3 运行时依赖打包（发布必须）

构建产物只是 Flutter runner + 引擎 DLL，**分发前必须补齐运行库**
（CI 自动完成；手动步骤见 [§5-E Windows](#5-手动构建不依赖脚本逐步执行)）。

> 缺这些 DLL 的典型症状：**启动即崩 / 无窗口**（加载器找不到 `Qt6*.dll`），
> 或托盘图标透明。NSIS 安装器与便携 zip 均从补齐后的 Release 目录打包。

### 4.4 打包

```bat
:: 安装器（VERSION/OUT/STAGING 参数见 packaging\nekomusic.nsi）
makensis packaging\nekomusic.nsi
:: 便携 zip：直接压缩 Release 目录
```

---

## 5. 手动构建（不依赖脚本，逐步执行）

> 脚本（`scripts/build.sh` / `build.bat`）本质是以下步骤的封装。
> 想自行控制每一步（或排查问题）时按本节操作。

### A. 安装 Flutter SDK（已装可跳过）

**方式一：git 克隆（推荐，便于切版本）**

```bash
git clone https://github.com/flutter/flutter.git -b stable --depth 1
cd flutter && ./bin/flutter --version   # 首次运行会自动下载 Dart SDK
export PATH="$PWD/flutter/bin:$PATH"    # 写入 ~/.bashrc / ~/.zshrc
```

**方式二：官网下载压缩包**

- 入口：<https://flutter.dev/downloads> 或 <https://docs.flutter.dev/get-started/install>
  （按平台选择 stable 压缩包：`flutter_linux_*.tar.xz` / `flutter_windows_*.zip` / `flutter_macos_*.zip`）
- 解压到目标目录（**避免**含空格/中文的路径），把 `bin` 加入 `PATH`：

  ```bash
  # Linux/macOS 示例
  cd ~/dev && tar -xf ~/Downloads/flutter_linux_*.tar.xz    # macOS 为 unzip *.zip
  export PATH="$HOME/dev/flutter/bin:$PATH"                 # 写入 shell 配置
  ```

  ```bat
  :: Windows：解压 flutter_windows_*.zip 到 D:\dev\flutter 后，
  :: 系统(用户)环境变量 PATH 增加：
  D:\dev\flutter\bin
  ```

> Windows 也可用方式一 git 克隆。

**版本要求**：`flutter --version` ≥ 3.44（推荐 3.47.x）；
旧版本可用 `flutter downgrade` 或 checkout 对应 tag。

**国内网络可选镜像**（克隆 / 下载 Dart SDK 与 pub 包加速）：

```bash
export PUB_HOSTED_URL=https://pub.flutter-io.cn
export FLUTTER_STORAGE_BASE_URL=https://storage.flutter-io.cn
```

**自检**：

```bash
flutter doctor
# 至少 [√] Flutter 无报错；对应桌面平台一行无阻断：
#   Linux   → clang / cmake / ninja / gtk3
#   Windows → Visual Studio - develop Windows apps
#   macOS   → Xcode command line tools
```

### B. 拉取 Dart 依赖

```bash
cd flutter        # 仓库内的 flutter/ 应用目录
flutter pub get
```

### C. 编译原生引擎（libneko_engine + libneko_core）

```bash
# 仓库根目录执行
cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build engine/build --target neko_engine neko_core --parallel
```

| 平台 | 产物 | 备注 |
|------|------|------|
| Linux / macOS | `engine/build/libneko_engine.so\|.dylib`、`libneko_core.so\|.dylib` | Flutter 工程 CMake 会引用 |
| Windows | `engine/build/neko_engine.dll`、`neko_core.dll` | 须在 **MSVC 环境**执行；`MPV_DIR` 指向 mpv 开发包（`lib` 下需有 `mpv.lib`） |

### D. 构建 Flutter 应用（关键参数 --no-tree-shake-icons）

```bash
cd flutter

# Linux
flutter build linux --release --no-tree-shake-icons
#   产物: build/linux/x64/release/bundle/   （cd 进去 ./neko_music 运行）

# macOS
flutter build macos --release --no-tree-shake-icons
#   产物: build/macos/Build/Products/Release/*.app

# Windows（MSVC 环境中）
flutter build windows --release --no-tree-shake-icons
#   产物: build/windows/x64/runner/Release/
```

> ⚠️ 不要省略 `--no-tree-shake-icons`：release 默认的图标 tree-shaking 会裁掉自绘字形，
> 造成部分图标空白。生产构建请始终带上。

### E. 平台后处理（脚本自动做的事，手动需自行完成）

**Linux** —— 无需后处理。bundle 自带引擎 `.so`（`bundle/lib/`），目录内直接运行。

**macOS** —— 四步，缺一不可：

```bash
APP=build/macos/Build/Products/Release/*.app

# 1) 引擎 dylib 拷入 Frameworks
mkdir -p "$APP/Contents/Frameworks"
cp ../engine/build/libneko_engine.dylib \
   ../engine/build/libneko_core.dylib "$APP/Contents/Frameworks/"

# 2) Qt 框架 + 插件（默认插件含 sqldrivers / imageformats / tls）
"$(brew --prefix qt)/bin/macdeployqt" "$APP" -no-strip

# 3) libmpv 依赖闭包打包并改写安装名
dylibbundler -of -b -x "$APP/Contents/Frameworks/libneko_engine.dylib" \
  -d "$APP/Contents/Frameworks" \
  -p @executable_path/../Frameworks

# 4) ad-hoc 签名（Apple Silicon 必须，否则 dylib 拒载）
codesign --force --deep --sign - "$APP"
```

**Windows** —— 运行时 DLL 补齐，缺则启动即崩：

```bat
set REL=build\windows\x64\runner\Release
set QT_ROOT=C:\Qt\6.8.2\msvc2022_64

:: 1) Qt 运行库 + Qt 插件（sqldrivers 等）+ MSVC 运行库
"%QT_ROOT%\bin\windeployqt.exe" --release --no-translations ^
    --compiler-runtime "%REL%\neko_core.dll"

:: 2) mpv 运行库（名称以实际包内为准：mpv-2.dll / libmpv-2.dll）
copy /Y "%MPV_DIR%\..\mpv-2.dll" "%REL%\"
```

检查清单：

```
neko_core.dll  neko_engine.dll  Qt6Core/Network/Sql.dll
sqldrivers\qsqlite.dll  mpv*.dll  msvcp140.dll  vcruntime140.dll
```

### F. 打包分发

- **Linux**：`bundle/` 目录即绿色包；桌面入口/图标参考 `packaging/`，或直接用 `scripts/package-*.sh`
- **macOS**：`.app` 直接压缩分发
- **Windows**：补齐 DLL 后的 `Release/` 目录直接压缩即便携包；NSIS 安装器见 `packaging/nekomusic.nsi`

---

## 6. 运行 & 开发

```bash
cd flutter
flutter pub get
flutter run -d linux      # 或 -d windows / macos
flutter analyze           # 应 0 error
flutter test              # 图标字形渲染回归测试（inked>0）
```

自绘图标字体重建（改了 `resources/icons*/**.svg` 后）：

```bash
./scripts/build-icon-font.sh
# 依赖 Node(npm)：fantasticon
# 产出 flutter/assets/fonts/neko_icons.ttf 与 flutter/lib/ui/neko_icons.dart（含完整性守卫）
```

---

## 7. 常见问题（踩坑记录）

| 症状 | 原因 | 处理 |
|------|------|------|
| Release 下部分图标缺失/空白 | 图标 tree-shaking 裁掉自绘字形 | 构建带 `--no-tree-shake-icons`（脚本已内置） |
| 启动即崩、无窗口（Windows） | 缺 `Qt6*.dll` / MSVC 运行库 / mpv dll | 见 [§5-E Windows](#5-手动构建不依赖脚本逐步执行)；CI 已自动补齐 |
| 图标字体更新后包内仍是旧字形 | `.dart_tool` 陈旧，资产未刷新 | 删 `flutter/build` 与 `flutter/.dart_tool/flutter_build` 后重编；必要时先 `flutter pub get` |
| `Failed to lookup symbol 'neko_core_cmd_*'` | 新增 C 导出未加 `NEKO_CORE_API` 头声明（`-fvisibility=hidden`） | 在 `engine/core/neko_core.h` 补声明 |
| 托盘图标透明 / 无右键菜单（Windows） | png 资产原生不可用 & 右键需手动弹出 | 已内置：`.ico` 落盘 + `popUpContextMenu()`（`lib/core/background_service.dart`） |
| 前台窗口点 X 无反应（Windows） | `setPreventClose(true)` 但插件未就绪，收不到 `onWindowClose` | 已内置：`main()` 首帧前 `windowManager.ensureInitialized()` |
| cmake 报 `build/native_assets/*` 缺失 | 构建缓存被外部清空 | `flutter pub get` 后重试；或整删 `flutter/build` 与 `.dart_tool` 再编 |
| Windows 下 bat 乱码 / 注释被当命令执行 | 批处理含中文且为 LF | `build.bat` 保持 **ASCII + CRLF**（已固化） |
| Linux 托盘不可用 | 无 appindicator | 安装 `libayatana-appindicator`，否则自动降级为直接退出 |

---

## 8. CI

`.github/workflows/build-linux-flutter.yml`：

- push / PR 到 `flutter` 分支 → 自动构建全部平台并上传产物
- `workflow_dispatch` 支持 `only_windows=true` 仅跑 Windows
- 产物断言内置（Windows 依赖清单 + 冒烟启动；macOS Qt/mpv/签名检查），缺依赖直接红灯
