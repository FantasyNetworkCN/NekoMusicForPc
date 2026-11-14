/**
 * @file songmenubuilder.cpp
 * @brief 单曲统一菜单的构建实现（见 songmenubuilder.h 的约定）
 */

#include "songmenubuilder.h"

#include "core/i18n.h"
#include "core/playlistmanager.h"
#include "core/togetherroomclient.h"

namespace SongMenuBuilder {

QList<SongContextMenuPopup::Entry> buildStandard(const MusicInfo &info, const State &state,
                                                 const Handlers &handlers)
{
    QList<SongContextMenuPopup::Entry> entries;
    I18n &i18n = I18n::instance();
    const bool hasTarget = info.id > 0 || !info.localPath.isEmpty();
    if (!hasTarget)
        return entries;

    // 0) 下一首播放（统一放在最前）
    if (handlers.playNext) {
        SongContextMenuPopup::Entry e;
        e.iconName = "PlayNext";
        e.label = i18n.tr(QStringLiteral("playNext"));
        e.action = handlers.playNext;
        entries.append(e);
    }

    // 1) 添加 / 移除收藏
    if (handlers.toggleFavorite && state.canFavorite) {
        SongContextMenuPopup::Entry e;
        e.iconName = state.favorited ? "Favorite" : "FavoriteBorder";
        e.label = state.favorited ? i18n.tr(QStringLiteral("unfavorite"))
                                  : i18n.tr(QStringLiteral("favorite"));
        e.action = handlers.toggleFavorite;
        entries.append(e);
    }

    // 2) 添加 / 移除至播放队列
    if (handlers.toggleQueue && state.canQueue) {
        SongContextMenuPopup::Entry e;
        e.iconName = state.inPlayQueue ? "Delete" : "PlaylistAdd";
        e.label = state.inPlayQueue ? i18n.tr(QStringLiteral("removeFromQueue"))
                                    : i18n.tr(QStringLiteral("addToQueue"));
        e.action = handlers.toggleQueue;
        entries.append(e);
    }

    // 3) 下载（本地文件与已下载曲目省略该项，避免点了没反应）
    if (handlers.download && state.canDownload && !state.downloaded && !info.isLocalFile()) {
        SongContextMenuPopup::Entry e;
        e.iconName = "Download";
        e.label = i18n.tr(QStringLiteral("downloadMusic"));
        e.action = handlers.download;
        entries.append(e);
    }

    return entries;
}

void playNextNow(const MusicInfo &info)
{
    if (info.id <= 0 && info.localPath.isEmpty())
        return;
    if (TogetherRoomClient::instance().isActive()) {
        TogetherRoomClient::instance().queueAsNext(info.id);
        return;
    }
    PlaylistManager::instance().playNext(info);
}

bool queueContains(int musicId)
{
    if (musicId <= 0)
        return false;
    const TogetherRoomClient &room = TogetherRoomClient::instance();
    if (room.isActive()) {
        const QVariantList items =
            room.playlist().value(QStringLiteral("items")).toList();
        for (const QVariant &value : items) {
            if (value.toMap().value(QStringLiteral("track")).toMap()
                    .value(QStringLiteral("id")).toInt()
                == musicId) {
                return true;
            }
        }
        return false;
    }
    const QList<MusicInfo> &queue = PlaylistManager::instance().playlist();
    for (const MusicInfo &item : queue) {
        if (item.id == musicId)
            return true;
    }
    return false;
}

void toggleQueue(const MusicInfo &info)
{
    if (info.id <= 0 && info.localPath.isEmpty())
        return;
    TogetherRoomClient &room = TogetherRoomClient::instance();
    if (room.isActive()) {
        if (info.id <= 0)
            return;
        const QVariantList items =
            room.playlist().value(QStringLiteral("items")).toList();
        for (const QVariant &value : items) {
            const QVariantMap item = value.toMap();
            if (item.value(QStringLiteral("track")).toMap()
                    .value(QStringLiteral("id")).toInt()
                == info.id) {
                room.removeEntry(item.value(QStringLiteral("entryId")).toString());
                return;
            }
        }
        room.addTrack(info.id);
        return;
    }
    PlaylistManager &pm = PlaylistManager::instance();
    if (queueContains(info.id))
        pm.removeFromPlaylist(info.id); // PlaylistManager 内部按 music id 匹配
    else
        pm.addToPlaylist(info); // 自带去重
}

bool addManyToTogetherQueue(const QList<MusicInfo> &songs, int *addedCount)
{
    TogetherRoomClient &room = TogetherRoomClient::instance();
    if (!room.isActive())
        return false;
    QList<int> ids;
    for (const MusicInfo &song : songs) {
        if (song.id > 0 && !song.isLocalFile() && !ids.contains(song.id))
            ids.append(song.id);
        if (ids.size() >= 200)
            break;
    }
    room.addTracks(ids);
    if (addedCount)
        *addedCount = ids.size();
    return true;
}

} // namespace SongMenuBuilder
