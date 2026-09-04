# Neko歌姬计划 PC版
![](https://count.getloli.com/get/@:NekoMusicPC?theme=moebooru)

> [!TIP]
> 🐾 **移动端入口**：[点击这里查看 Neko歌姬计划 安卓版仓库](https://github.com/MinecraftNekoServer/NekoMusicForAndroid)
> 🐾 **后端**：[点击这里查看 Neko歌姬计划 后端仓库](https://github.com/FantasyNetworkCN/NekoMusic)

# 官网 https://music.cnmsb.xin

## 关于从外部导入歌单
网易云歌单导入使用的是三方api和Neko官方api调用。[网易云API仓库](https://github.com/kengwang/NeteaseCloudMusicApi-1)
QQ云音乐歌单导入相同 [QQ云API仓库](https://github.com/Rain120/qq-music-api)

### 获取外部歌单api
获取qq歌单列表`https://music.cnmsb.xin/loser1/getSongListDetail?disstid=歌单id`
获取网易云歌单列表`https://music.cnmsb.xin/loser/playlist/track/all?id=歌单id`

> [!NOTE]
> 本仓库 **默认分支 `main`** 为 **Qt 6 + C++** 客户端。  
> 旧版 **Electron + Vue** 工程已单独放在 Git 分支 **`old`**（仅存档 / 按需构建），见该分支根目录的 [README](https://github.com/FantasyNetworkCN/NekoMusicForPc/blob/old/README.md)。

---

## 前置要求

| 依赖 | 最低版本 |
| --- | --- |
| CMake | ≥ 3.20 |
| Qt 6 | ≥ 6.2（Core/Network/Sql/Gui，核心桥使用） |
| C++17 编译器 | GCC ≥ 9 / MSVC 2019 / Clang ≥ 10 |

**Debian / Ubuntu 示例：**

```bash
sudo apt install cmake ninja-build qt6-base-dev libmpv-dev
```

---

## 配置与编译

### Flutter 版构建

> 📖 **详细编译指南（依赖安装/逐平台步骤/运行时打包/常见问题）见 [docs/BUILD.md](docs/BUILD.md)。**

依赖：Flutter SDK 3.44+（**任意官方版本即可，无需补丁 SDK**）、CMake、Ninja、
libmpv 开发文件、Qt6（Core/Network/Sql/Gui）。

```bash
# Debug 构建（默认）
./scripts/build.sh

# Release 构建
./scripts/build.sh release

# 打包可分发的 tar.gz（含 bundle + 桌面入口 + 图标 + 安装器）
./scripts/package.sh

# 桌面集成安装（图标 + 应用入口；可选 --bundle 指定程序目录）
./scripts/install.sh

# 清理构建产物
./scripts/clean.sh
```

SDK 解析顺序：`$NEKO_FLUTTER` → `../flutter-stable` → PATH → `/opt/flutter`。
CI 见 `.github/workflows/build-linux-flutter.yml`（push/PR 自动构建并上传产物）。

## 构建产物

| 平台 | 说明 |
| --- | --- |
| Linux | `scripts/build.sh release` 后按发行版打包：`.deb`（`scripts/package-deb.sh`）、`.rpm`（`scripts/package-rpm.sh`）、Arch（`scripts/package-arch.sh`）、便携 zip（`scripts/package-portable.sh`）；聚合入口 `scripts/package.sh` |
| Linux(通用) | AppImage（linuxdeploy+qt/gtk 插件，内置 Qt6/GTK/mpv）、Flatpak（KDE runtime 内置 Qt/GTK） |
| macOS | `scripts/build.sh release` → `.app`（macdeployqt 内置 Qt）+ zip |
| Windows | `scripts\build.bat release`（需 MPV_DIR 与 Qt6）→ NSIS 安装器 + portable zip（windeployqt 内置 Qt） |
| CI | 全自动矩阵：deb/rpm/arch/nix/appimage/flatpak/macos/windows 全部 `success`（仅调用 scripts/） |

---

## 贡献与反馈

构建或使用中遇到问题，欢迎提交 **Issue** 或 **Pull Request**。
