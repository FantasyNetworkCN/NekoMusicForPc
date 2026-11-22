#pragma once

/**
 * @file mcpbridge.h
 * @brief 把播放器能力暴露为 MCP 工具（McpToolHost 的桌面端实现）。
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <functional>

#include "core/mcpserver.h"
#include "core/musicinfo.h"

class ApiClient;
class PlayerEngine;

class McpBridge : public QObject, public McpToolHost
{
    Q_OBJECT

public:
    explicit McpBridge(PlayerEngine *engine, ApiClient *api, QObject *parent = nullptr);

    void setPlayCallback(std::function<void(const MusicInfo &)> cb) { m_play = std::move(cb); }
    void setNextCallback(std::function<void()> cb) { m_next = std::move(cb); }
    void setPreviousCallback(std::function<void()> cb) { m_previous = std::move(cb); }
    /** 「下一首播放」：把曲目插到当前曲目之后。 */
    void setPlayNextCallback(std::function<void(const MusicInfo &)> cb) { m_playNext = std::move(cb); }
    /** 用给定列表替换播放队列并播放第 index 首（播放歌单 / 整队播放）。 */
    void setReplaceQueueCallback(std::function<void(const QList<MusicInfo> &, int)> cb) { m_replaceQueue = std::move(cb); }
    /** 查询某曲是否已收藏（主窗口内存缓存）。 */
    void setFavoriteQueryCallback(std::function<bool(int)> cb) { m_isFavorited = std::move(cb); }
    /** 收藏集合变化后刷新主窗口缓存与界面。 */
    void setFavoritesRefreshCallback(std::function<void()> cb) { m_refreshFavorites = std::move(cb); }
    /** 切换主窗口页面（home/favorites/recent/downloads/hot/latest/daily/settings）。 */
    void setOpenPageCallback(std::function<void(const QString &)> cb) { m_openPage = std::move(cb); }
    /** 当前音质偏好（standard/hq/sq/hires/dolby）。 */
    void setAudioQualityQueryCallback(std::function<QString()> cb) { m_audioQuality = std::move(cb); }
    /** 以程序方式切换音质；返回是否接受该档位。 */
    void setAudioQualityCallback(std::function<bool(const QString &)> cb) { m_setAudioQuality = std::move(cb); }
    /** 桌面歌词开关状态。 */
    void setDesktopLyricsQueryCallback(std::function<bool()> cb) { m_desktopLyrics = std::move(cb); }
    /** 开关桌面歌词。 */
    void setDesktopLyricsCallback(std::function<void(bool)> cb) { m_setDesktopLyrics = std::move(cb); }
    /** 打开本地音频/播放列表文件（绝对路径）。 */
    void setOpenLocalFileCallback(std::function<void(const QString &)> cb) { m_openLocalFile = std::move(cb); }
    /** 打开某歌单的详情页。 */
    void setOpenPlaylistPageCallback(std::function<void(int)> cb) { m_openPlaylistPage = std::move(cb); }
    /** 搜索并打开某歌手的详情页。 */
    void setOpenArtistPageCallback(std::function<void(const QString &)> cb) { m_openArtistPage = std::move(cb); }
    /** 触发版本更新检查（有更新时由主窗口弹窗）。 */
    void setCheckUpdateCallback(std::function<void()> cb) { m_checkUpdate = std::move(cb); }

    QJsonArray listTools() const override;
    void callTool(const QString &name, const QJsonObject &arguments,
                  std::function<void(bool ok, const QString &text, const QJsonObject &structuredContent)> done) override;

private:
    using Done = std::function<void(bool, const QString &, const QJsonObject &)>;
    using Handler = std::function<void(const QJsonObject &arguments, Done done)>;

    void initHandlers();

    static QJsonObject trackJson(const MusicInfo &info);
    static QJsonArray tracksJson(const QList<MusicInfo> &tracks);
    static MusicInfo musicInfoFromMap(const QVariantMap &map);
    QJsonObject playbackStateJson() const;

    /** 按 ID 取曲目信息：优先命中播放队列，其次向服务端拉取详情。 */
    void resolveTrack(int musicId,
                      std::function<void(bool ok, const MusicInfo &info, const QString &error)> done);
    void runSearch(const QString &query, int limit,
                   std::function<void(bool ok, const QList<MusicInfo> &tracks, const QString &error)> done);
    /** 收藏 / 取消收藏；成功后刷新主窗口缓存。 */
    void applyFavorite(int musicId, bool favorite, const Done &done);
    /** 用给定列表替换播放队列并从 index 起播。 */
    void playTrackList(const QList<MusicInfo> &tracks, int index, const Done &done, const QString &label);
    QJsonObject importStatusJson() const;

    /** 外部歌单导入的最近一次进度/结果。 */
    struct ExternalImportState {
        bool running = false;
        QString source;
        QString externalId;
        int index = 0;
        int total = 0;
        int imported = 0;
        int existed = 0;
        int failed = 0;
        QString lastTrack;
        QString lastMessage;
        QString error;
    };

    PlayerEngine *m_engine = nullptr;
    ApiClient *m_api = nullptr;
    QHash<QString, Handler> m_handlers;
    std::function<void(const MusicInfo &)> m_play;
    std::function<void()> m_next;
    std::function<void()> m_previous;
    std::function<void(const MusicInfo &)> m_playNext;
    std::function<void(const QList<MusicInfo> &, int)> m_replaceQueue;
    std::function<bool(int)> m_isFavorited;
    std::function<void()> m_refreshFavorites;
    std::function<void(const QString &)> m_openPage;
    std::function<QString()> m_audioQuality;
    std::function<bool(const QString &)> m_setAudioQuality;
    std::function<bool()> m_desktopLyrics;
    std::function<void(bool)> m_setDesktopLyrics;
    std::function<void(const QString &)> m_openLocalFile;
    std::function<void(int)> m_openPlaylistPage;
    std::function<void(const QString &)> m_openArtistPage;
    std::function<void()> m_checkUpdate;
    ExternalImportState m_import;
};
