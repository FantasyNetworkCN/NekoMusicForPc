#pragma once

/**
 * @file songmenubuilder.h
 * @brief 单曲统一菜单（设计稿）：收藏 / 播放队列 / 下载
 *
 * 所有出现「单曲行」的界面共用这一份构建逻辑，保证菜单项的顺序、图标、文案完全一致；
 * 页面只负责提供「状态查询」与「动作」，不再各自拼菜单。
 *
 * 设计稿约定：
 *   - 入口：单曲行右侧的三点按钮（也兼容右键）
 *   - 菜单项：添加/移除收藏 · 添加/移除至播放队列 · 下载（已下载时省略）
 *   - 缺少动作的项自动省略，避免出现点了没反应的死项
 */

#include <QList>
#include <functional>

#include "core/musicinfo.h"
#include "ui/songcontextmenu.h"

namespace SongMenuBuilder {

/** 菜单项所需的曲目状态（由列表控件/页面查询后提供） */
struct State {
    bool favorited = false;    ///< 已收藏 → 显示「取消收藏」
    bool inPlayQueue = false;  ///< 已在播放队列 → 显示「从播放队列移除」
    bool downloaded = false;   ///< 已下载 → 显示「已下载」
    bool canFavorite = true;   ///< 本地文件、无 id 等场景可置 false
    bool canQueue = true;
    bool canDownload = true;
};

/** 菜单项动作；为空表示该项不可用（不显示） */
struct Handlers {
    std::function<void()> toggleFavorite;
    std::function<void()> toggleQueue;
    std::function<void()> download;
};

/**
 * 生成标准菜单项。
 * @param info     目标曲目（用于本地文件判断等）
 * @param state    状态（决定文案与图标）
 * @param handlers 动作（决定该项是否出现）
 */
QList<SongContextMenuPopup::Entry> buildStandard(const MusicInfo &info, const State &state,
                                                 const Handlers &handlers);

/**
 * 播放队列便捷实现：直接对接全局 PlaylistManager（与 MainWindow 的 addToQueue 同一实现），
 * 页面/列表不需要各自接线即可获得「添加/移除至播放队列」。
 */
bool queueContains(int musicId);
void toggleQueue(const MusicInfo &info);

} // namespace SongMenuBuilder
