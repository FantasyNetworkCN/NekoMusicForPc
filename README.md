# Neko歌姬计划 PC版
![](https://count.getloli.com/get/@:NekoMusicPC?theme=moebooru)

> [!TIP]
> 🐾 **移动端入口**：[点击这里查看 Neko歌姬计划 安卓版仓库](https://github.com/MinecraftNekoServer/NekoMusicForAndroid)
> 🐾 **后端**：[点击这里查看 Neko歌姬计划 后端仓库](https://github.com/FantasyNetworkCN/NekoMusic)

# 本平台唯一官网 https://music.nekocore.cn
- ## 请勿通过非官方渠道获取安装包。本站被大量非法黑产生成盗版客户端诈骗，如你通过第三方平台获取安装包导致被骗本站拒绝一切赔偿

### 获取外部歌单api
获取qq歌单列表`https://music.nekocore.cn/loser1/getSongListDetail?disstid=歌单id`
获取网易云歌单列表`https://music.nekocore.cn/loser/playlist/track/all?id=歌单id`

> [!NOTE]
> 本仓库 **默认分支 `main`** 为 **Qt 6 + C++** 客户端。  
> 旧版 **Electron + Vue** 工程已单独放在 Git 分支 **`old`**（仅存档 / 按需构建），见该分支根目录的 [README](https://github.com/FantasyNetworkCN/NekoMusicForPc/blob/old/README.md)。

---

## 前置要求

| 依赖 | 最低版本 |
| --- | --- |
| CMake | ≥ 3.20 |
| Qt 6 | ≥ 6.2（需 Multimedia、Widgets 等，与 `CMakeLists.txt` 一致） |
| OpenSSL | ≥ 3.0（Linux 客户端实例密钥，需开发头文件） |
| C++17 编译器 | GCC ≥ 9 / MSVC 2019 / Clang ≥ 10 |

**Debian / Ubuntu 示例：**

```bash
sudo apt install cmake qt6-base-dev qt6-multimedia-dev libssl-dev
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

## MCP 服务端

桌面端内置一个本地 MCP（Model Context Protocol）服务器，可被 Claude Desktop、Cursor、Cherry Studio
等支持 MCP 的 AI 客户端调用，用于查询与控制播放。

### 开启方式

进入「设置 → MCP」：

- **启用内置 MCP 服务**：打开后在监听端口上启动服务。
- **监听端口**：默认 `7788`，取值 `1024-65535`。
- **访问令牌**：可选；填写后客户端需携带 `Authorization: Bearer <token>` 头。
- **允许局域网/远程设备访问**：默认仅监听 `127.0.0.1`，开启后监听所有网卡（请务必同时设置访问令牌）。
- **复制客户端配置**：一键把标准 `mcpServers` 配置写入剪贴板。

修改后点击「应用并重启服务」即时生效，状态栏会显示运行中地址或失败原因。

### 传输与端点

| 端点 | 方法 | 说明 |
| --- | --- | --- |
| `/mcp` | POST / GET / DELETE | Streamable HTTP（推荐） |
| `/sse` | GET | 旧版 SSE 连接入口 |
| `/messages?sessionId=…` | POST | 旧版 SSE 消息回传 |
| `/health` | GET | 健康检查 |

### 工具

共 101 个工具，覆盖播放与音质、队列、内容检索、收藏、歌单、本地库、下载、外部歌单导入、
分享视频渲染、账户消息、界面偏好与导航。所有工具均返回 `content` 文本；多数还附带
`structuredContent` 便于程序化处理。

**播放控制**：`get_playback_state`、`get_current_track`、`play`、`pause`、`toggle_playback`、
`stop`、`next_track`、`previous_track`、`seek`、`set_volume`、`set_play_mode`

**播放队列**：`get_queue`、`play_queue_index`、`add_to_queue`、`add_search_result_to_queue`、
`play_next`（插队作为下一首）、`remove_from_queue`、`clear_queue`

**音质/输出/偏好**：`get_audio_quality`、`set_audio_quality`、`get_desktop_lyrics`、
`set_desktop_lyrics`、`get_theme_mode`、`set_theme_mode`、`get_mic_sync`、`set_mic_sync`

**局域网**：`list_lan_devices`、`select_lan_device`、`get_remote_queue`

**搜索与内容**：`search_music`、`play_music`、`play_search_result`、`search_artists`、
`get_ranking`、`get_latest_music`、`get_daily_recommendations`、`get_music_detail`、
`get_lyrics`、`get_comments`

**收藏**：`get_favorites`、`is_favorite`、`add_favorite`、`remove_favorite`、`toggle_favorite`

**在线歌单**：`list_playlists`、`get_playlist_detail`、`get_playlist_tracks`、`play_playlist`、
`create_playlist`、`update_playlist`、`delete_playlist`、`add_music_to_playlist`、
`remove_music_from_playlist`、`list_favorite_playlists`、`favorite_playlist`、`unfavorite_playlist`、
`search_playlists`、`get_uploaded_music`

**本地库**：`list_local_playlists`、`get_local_playlist_tracks`、`create_local_playlist`、
`delete_local_playlist`、`add_music_to_local_playlist`、`remove_music_from_local_playlist`、
`play_local_playlist`、`update_local_playlist`、`get_recently_played`、`clear_recently_played`、
`open_local_file`、`resume_last_played`

**下载**：`list_downloads`、`download_music`、`download_playlist`、`cancel_download`、`remove_download`

**外部歌单导入**：`get_external_playlist`、`start_external_import`、`get_import_status`

**分享视频渲染**：`create_video_render`、`get_video_render_status`、`download_video_render`

**批量操作**：`batch_search_music`、`batch_add_favorites`、`batch_add_to_playlist`

**评论与消息**：`get_comments`、`post_comment`、`delete_comment`、`get_notifications`、
`mark_notifications_read`

**账户**：`get_user_info`、`get_vip_status`、`get_vip_pricing`、`logout`、`change_nickname`

**界面导航 / 一键播放**：`open_page`（`home` / `favorites` / `recent` / `downloads` / `hot` /
`latest` / `daily` / `settings`）、`open_playlist_page`、`open_artist_page`、`check_update`、
`play_ranking`、`play_latest_music`、`play_daily_recommendations`、`play_favorites`

### 客户端配置示例

```json
{
  "mcpServers": {
    "nekomusic": {
      "url": "http://127.0.0.1:7788/mcp",
      "headers": { "Authorization": "Bearer <token>" }
    }
  }
}
```

> `headers` 仅在设置了访问令牌时需要。

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

---

## 法律与声明

- [用户协议](docs/user-agreement.md)
- [隐私政策](docs/privacy-policy.md)

应用内可在「设置 → 关于」中查看上述文档；首次启动需阅读并同意后方可使用本软件。
