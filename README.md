# Neko歌姬计划 PC版
![](https://count.getloli.com/get/@:NekoMusicPC?theme=moebooru)

> [!TIP]
> 🐾 **移动端入口**：[点击这里查看 Neko歌姬计划 安卓版仓库](https://github.com/MinecraftNekoServer/NekoMusicForAndroid)
> 🐾 **后端**：[点击这里查看 Neko歌姬计划 后端仓库](https://github.com/FantasyNetworkCN/NekoMusic)

# 本平台唯一官网 https://music.cnmsb.xin
- ## 请勿通过非官方渠道获取安装包。本站被大量非法黑产生成盗版客户端诈骗，如你通过第三方平台获取安装包导致被骗本站拒绝一切赔偿

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
| Qt 6 | ≥ 6.2（需 Multimedia、Widgets 等，与 `CMakeLists.txt` 一致） |
| C++17 编译器 | GCC ≥ 9 / MSVC 2019 / Clang ≥ 10 |

**Debian / Ubuntu 示例：**

```bash
sudo apt install cmake qt6-base-dev qt6-multimedia-dev
```

---

## 配置与编译

项目可使用 CMake Presets，也可直接用脚本构建：

```bash
# Linux（推荐）
bash build_linux.sh

# Windows：在 Linux 上交叉编译（需自备 MinGW 版 Qt，见脚本内说明）
QT_WIN_ROOT=./qt-win/6.10.2/mingw_64 ./build_windows.sh
```

使用 Presets 时：

```bash
# Linux
cmake --preset linux-debug
cmake --preset linux-release
cmake --build build/linux-release -j"$(nproc)"

# Windows / macOS（需在对应系统或 CI 上）
cmake --preset windows-release
cmake --preset macos-release

# macOS 一键打通用 pkg（arm64 + x86_64）
./build_macos.sh
```

手动配置示例：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### 测试（若启用）

```bash
ctest --test-dir build/linux-debug --output-on-failure
```

### 安装（Linux）

```bash
cmake --install build/linux-release --prefix /usr/local
```

---

## 构建产物

| 平台 | 说明 |
| --- | --- |
| Linux | 可执行文件在构建目录；`build_linux.sh` 可用 CPack 打 deb（若已配置） |
| Windows | `build_windows.sh`（Linux 交叉编译）或 CI 原生构建，产物为 `dist/` 下的 NSIS 安装包 |
| macOS | `build_macos.sh` 完成后在 `dist/` 下生成通用架构 `.pkg`（arm64 + x86_64） |

---

## 贡献与反馈

构建或使用中遇到问题，欢迎提交 **Issue** 或 **Pull Request**。
