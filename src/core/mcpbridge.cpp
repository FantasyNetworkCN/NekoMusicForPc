/**
 * @file mcpbridge.cpp
 *
 * 把桌面端播放器的各项能力暴露为 MCP 工具。工具按功能分组：
 * 播放控制、播放队列、搜索与内容、收藏、在线歌单、本地库、下载、账户与消息、界面导航。
 */

#include "mcpbridge.h"

#include "core/apiclient.h"
#include "core/landevicemanager.h"
#include "core/localmusicmeta.h"
#include "core/micsynccontroller.h"
#include "core/musicdownloadmanager.h"
#include "core/playerengine.h"
#include "core/playlistdb.h"
#include "core/playlistmanager.h"
#include "core/usermanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"

#include <QJsonValue>
#include <QStandardPaths>
#include <QStringList>
#include <QVariantMap>
#include <memory>

namespace {

QJsonObject objSchema(const QJsonObject &properties, const QJsonArray &required = QJsonArray())
{
    QJsonObject schema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), properties},
    };
    if (!required.isEmpty())
        schema.insert(QStringLiteral("required"), required);
    return schema;
}

QJsonObject toolDef(const QString &name, const QString &description, const QJsonObject &inputSchema)
{
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("description"), description},
        {QStringLiteral("inputSchema"), inputSchema},
    };
}

QJsonObject prop(const QString &type, const QString &description)
{
    return QJsonObject{{QStringLiteral("type"), type}, {QStringLiteral("description"), description}};
}

QJsonObject strProp(const QString &description)
{
    return prop(QStringLiteral("string"), description);
}

QJsonObject numProp(const QString &description, double minimum, double maximum)
{
    QJsonObject p = prop(QStringLiteral("number"), description);
    p.insert(QStringLiteral("minimum"), minimum);
    p.insert(QStringLiteral("maximum"), maximum);
    return p;
}

QJsonObject enumProp(const QString &description, const QStringList &values)
{
    QJsonObject p = prop(QStringLiteral("string"), description);
    QJsonArray arr;
    for (const QString &value : values)
        arr.append(value);
    p.insert(QStringLiteral("enum"), arr);
    return p;
}

QJsonObject arrayProp(const QString &itemType, const QString &description)
{
    QJsonObject p = prop(QStringLiteral("array"), description);
    p.insert(QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), itemType}});
    return p;
}

QString stateName(PlayerEngine::PlaybackState state)
{
    switch (state) {
    case PlayerEngine::Playing: return QStringLiteral("playing");
    case PlayerEngine::Paused: return QStringLiteral("paused");
    default: return QStringLiteral("stopped");
    }
}

bool readPositiveInt(const QJsonObject &args, const QString &key, int *out, QString *error)
{
    const QJsonValue value = args.value(key);
    if (value.isUndefined() || value.isNull()) {
        if (error)
            *error = QStringLiteral("缺少参数 %1").arg(key);
        return false;
    }
    const int parsed = value.toInt(-1);
    if (parsed <= 0) {
        if (error)
            *error = QStringLiteral("参数 %1 必须是正整数").arg(key);
        return false;
    }
    if (out)
        *out = parsed;
    return true;
}

bool readNonEmptyString(const QJsonObject &args, const QString &key, QString *out, QString *error)
{
    const QString value = args.value(key).toString().trimmed();
    if (value.isEmpty()) {
        if (error)
            *error = QStringLiteral("缺少参数 %1").arg(key);
        return false;
    }
    if (out)
        *out = value;
    return true;
}

MusicInfo infoFromMap(const QVariantMap &map)
{
    MusicInfo info;
    info.id = map.value(QStringLiteral("id")).toInt();
    info.title = map.value(QStringLiteral("title")).toString();
    info.artist = map.value(QStringLiteral("artist")).toString();
    info.album = map.value(QStringLiteral("album")).toString();
    info.duration = map.value(QStringLiteral("duration")).toInt();
    info.coverUrl = map.value(QStringLiteral("coverUrl")).toString();
    if (info.coverUrl.isEmpty() && info.id > 0)
        info.coverUrl = QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(info.id);
    return info;
}

QList<MusicInfo> tracksFromMaps(const QList<QVariantMap> &maps)
{
    QList<MusicInfo> tracks;
    for (const QVariantMap &map : maps) {
        const MusicInfo info = infoFromMap(map);
        if (info.id > 0)
            tracks.append(info);
    }
    return tracks;
}

QJsonArray mapsToJson(const QList<QVariantMap> &maps)
{
    QJsonArray arr;
    for (const QVariantMap &map : maps)
        arr.append(QJsonObject::fromVariantMap(map));
    return arr;
}

} // namespace

McpBridge::McpBridge(PlayerEngine *engine, ApiClient *api, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_api(api)
{
    initHandlers();
}

QJsonArray McpBridge::listTools() const
{
    QJsonArray tools;
    const auto add = [&tools](const QString &name, const QString &description,
                              const QJsonObject &properties = QJsonObject(),
                              const QJsonArray &required = QJsonArray()) {
        tools.append(toolDef(name, description, objSchema(properties, required)));
    };
    const auto musicIdProp = [] {
        return numProp(QStringLiteral("曲目 ID（可由 search_music / get_favorites 等获得）"), 1, 1e12);
    };
    const auto musicIdField = [](const QString &extra = QString()) {
        return objSchema(QJsonObject{{QStringLiteral("music_id"),
                                      numProp(extra.isEmpty()
                                                  ? QStringLiteral("曲目 ID（可由 search_music 获得）")
                                                  : extra,
                                              1, 1e12)}},
                         QJsonArray{QStringLiteral("music_id")});
    };

    // ── 播放控制 ───────────────────────────────────────
    add(QStringLiteral("get_playback_state"),
        QStringLiteral("获取当前播放状态：播放/暂停/停止、进度、时长、音量与播放模式。"));
    add(QStringLiteral("get_current_track"),
        QStringLiteral("获取当前正在播放的曲目信息（没有则返回 null）。"));
    add(QStringLiteral("play"), QStringLiteral("开始/继续播放当前曲目。"));
    add(QStringLiteral("pause"), QStringLiteral("暂停播放。"));
    add(QStringLiteral("toggle_playback"), QStringLiteral("在播放与暂停之间切换。"));
    add(QStringLiteral("stop"), QStringLiteral("停止播放。"));
    add(QStringLiteral("next_track"), QStringLiteral("切到下一首。"));
    add(QStringLiteral("previous_track"), QStringLiteral("切到上一首。"));
    add(QStringLiteral("seek"), QStringLiteral("跳转到指定播放位置（秒）。"),
        QJsonObject{{QStringLiteral("position"), numProp(QStringLiteral("目标位置（秒）"), 0, 24 * 3600)}},
        QJsonArray{QStringLiteral("position")});
    add(QStringLiteral("set_volume"), QStringLiteral("设置音量（0-100）。"),
        QJsonObject{{QStringLiteral("volume"), numProp(QStringLiteral("音量百分比"), 0, 100)}},
        QJsonArray{QStringLiteral("volume")});
    add(QStringLiteral("set_play_mode"),
        QStringLiteral("设置播放模式：list=列表循环，single=单曲循环，random=随机。"),
        QJsonObject{{QStringLiteral("mode"),
                     enumProp(QStringLiteral("播放模式"),
                              {QStringLiteral("list"), QStringLiteral("single"), QStringLiteral("random")})}},
        QJsonArray{QStringLiteral("mode")});

    // ── 播放队列 ───────────────────────────────────────
    add(QStringLiteral("get_queue"),
        QStringLiteral("获取当前播放队列（含每首曲目的 ID、标题、歌手）、当前下标与播放模式。"));
    add(QStringLiteral("play_queue_index"), QStringLiteral("播放播放队列中的第 index 首（index 从 1 开始）。"),
        QJsonObject{{QStringLiteral("index"), numProp(QStringLiteral("队列序号（从 1 开始）"), 1, 100000)}},
        QJsonArray{QStringLiteral("index")});
    add(QStringLiteral("add_to_queue"), QStringLiteral("按曲目 ID 追加到播放队列末尾。"),
        QJsonObject{{QStringLiteral("music_id"), musicIdProp()}},
        QJsonArray{QStringLiteral("music_id")});
    add(QStringLiteral("add_search_result_to_queue"),
        QStringLiteral("搜索并把第 index 个结果追加到播放队列末尾（index 从 1 开始）。"),
        QJsonObject{{QStringLiteral("query"), strProp(QStringLiteral("搜索关键词"))},
                    {QStringLiteral("index"), numProp(QStringLiteral("结果序号，默认 1"), 1, 50)}},
        QJsonArray{QStringLiteral("query")});
    add(QStringLiteral("play_next"), QStringLiteral("把曲目插到当前曲目之后，作为下一首播放（插队播放）。"),
        musicIdField());
    add(QStringLiteral("remove_from_queue"), QStringLiteral("按曲目 ID 从播放队列中移除。"), musicIdField());
    add(QStringLiteral("clear_queue"), QStringLiteral("清空播放队列。"));

    // ── 搜索与内容 ─────────────────────────────────────
    add(QStringLiteral("search_music"), QStringLiteral("按关键词搜索曲库，返回匹配的曲目列表（含 ID）。"),
        QJsonObject{{QStringLiteral("query"), strProp(QStringLiteral("搜索关键词"))},
                    {QStringLiteral("limit"), numProp(QStringLiteral("返回数量，默认 10，最大 50"), 1, 50)}},
        QJsonArray{QStringLiteral("query")});
    add(QStringLiteral("play_music"), QStringLiteral("按曲目 ID 播放（ID 可由 search_music 获得）。"),
        musicIdField());
    add(QStringLiteral("play_search_result"),
        QStringLiteral("搜索并按序号播放第 N 个结果（index 从 1 开始）。"),
        QJsonObject{{QStringLiteral("query"), strProp(QStringLiteral("搜索关键词"))},
                    {QStringLiteral("index"), numProp(QStringLiteral("结果序号，默认 1"), 1, 50)}},
        QJsonArray{QStringLiteral("query")});
    add(QStringLiteral("search_artists"), QStringLiteral("按关键词搜索歌手，返回歌手资料。"),
        QJsonObject{{QStringLiteral("query"), strProp(QStringLiteral("歌手关键词"))}},
        QJsonArray{QStringLiteral("query")});
    add(QStringLiteral("get_ranking"), QStringLiteral("获取热门排行榜曲目列表。"));
    add(QStringLiteral("get_latest_music"), QStringLiteral("获取最新上架的曲目列表。"),
        QJsonObject{{QStringLiteral("limit"), numProp(QStringLiteral("返回数量，默认 30，最大 100"), 1, 100)}});
    add(QStringLiteral("get_daily_recommendations"), QStringLiteral("获取当前登录用户的每日推荐（需登录）。"));
    add(QStringLiteral("get_music_detail"), QStringLiteral("获取指定曲目的详细信息。"), musicIdField());
    add(QStringLiteral("get_lyrics"), QStringLiteral("获取指定曲目的歌词文本。"), musicIdField());
    add(QStringLiteral("get_comments"), QStringLiteral("获取指定曲目的评论列表。"),
        QJsonObject{{QStringLiteral("music_id"), musicIdProp()},
                    {QStringLiteral("page"), numProp(QStringLiteral("页码，默认 1"), 1, 10000)},
                    {QStringLiteral("page_size"), numProp(QStringLiteral("每页数量，默认 20，最大 50"), 1, 50)}},
        QJsonArray{QStringLiteral("music_id")});

    // ── 收藏 ───────────────────────────────────────────
    add(QStringLiteral("get_favorites"), QStringLiteral("获取当前登录用户的收藏曲目列表（需登录）。"));
    add(QStringLiteral("is_favorite"), QStringLiteral("判断某曲目是否已收藏。"), musicIdField());
    add(QStringLiteral("add_favorite"), QStringLiteral("收藏指定曲目（需登录）。"), musicIdField());
    add(QStringLiteral("remove_favorite"), QStringLiteral("取消收藏指定曲目（需登录）。"), musicIdField());
    add(QStringLiteral("toggle_favorite"), QStringLiteral("切换指定曲目的收藏状态（需登录）。"), musicIdField());

    // ── 在线歌单 ───────────────────────────────────────
    add(QStringLiteral("list_playlists"), QStringLiteral("获取当前登录用户创建的歌单列表（需登录）。"));
    add(QStringLiteral("get_playlist_detail"), QStringLiteral("获取指定歌单的详情。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("playlist_id")});
    add(QStringLiteral("get_playlist_tracks"), QStringLiteral("获取指定歌单内的曲目列表。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("playlist_id")});
    add(QStringLiteral("play_playlist"), QStringLiteral("用指定歌单的曲目替换播放队列并开始播放。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("playlist_id")});
    add(QStringLiteral("create_playlist"), QStringLiteral("新建歌单（需登录）。"),
        QJsonObject{{QStringLiteral("name"), strProp(QStringLiteral("歌单名称"))},
                    {QStringLiteral("description"), strProp(QStringLiteral("歌单描述，可选"))}},
        QJsonArray{QStringLiteral("name")});
    add(QStringLiteral("update_playlist"), QStringLiteral("修改歌单名称/描述（需登录）。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)},
                    {QStringLiteral("name"), strProp(QStringLiteral("新名称，可选"))},
                    {QStringLiteral("description"), strProp(QStringLiteral("新描述，可选"))}},
        QJsonArray{QStringLiteral("playlist_id")});
    add(QStringLiteral("delete_playlist"), QStringLiteral("删除歌单（需登录，不可恢复）。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("playlist_id")});
    add(QStringLiteral("add_music_to_playlist"), QStringLiteral("把曲目加入指定歌单（需登录）。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)},
                    {QStringLiteral("music_id"), musicIdProp()}},
        QJsonArray{QStringLiteral("playlist_id"), QStringLiteral("music_id")});
    add(QStringLiteral("remove_music_from_playlist"), QStringLiteral("把曲目移出指定歌单（需登录）。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)},
                    {QStringLiteral("music_id"), musicIdProp()}},
        QJsonArray{QStringLiteral("playlist_id"), QStringLiteral("music_id")});
    add(QStringLiteral("list_favorite_playlists"), QStringLiteral("获取当前用户收藏的歌单列表（需登录）。"));
    add(QStringLiteral("favorite_playlist"), QStringLiteral("收藏指定歌单（需登录）。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("playlist_id")});
    add(QStringLiteral("unfavorite_playlist"), QStringLiteral("取消收藏指定歌单（需登录）。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("playlist_id")});

    // ── 下载 ───────────────────────────────────────────
    add(QStringLiteral("list_downloads"), QStringLiteral("列出已下载与正在下载的曲目。"));
    add(QStringLiteral("download_music"), QStringLiteral("把指定曲目加入下载队列。"), musicIdField());
    add(QStringLiteral("cancel_download"), QStringLiteral("取消指定曲目的下载。"), musicIdField());

    // ── 账户与消息 ─────────────────────────────────────
    add(QStringLiteral("get_user_info"), QStringLiteral("获取当前登录用户的资料。"));
    add(QStringLiteral("get_vip_status"), QStringLiteral("获取当前用户的会员状态。"));
    add(QStringLiteral("get_notifications"), QStringLiteral("获取站内消息列表（需登录）。"),
        QJsonObject{{QStringLiteral("limit"), numProp(QStringLiteral("返回数量，默认 20，最大 50"), 1, 50)}});
    add(QStringLiteral("get_recently_played"), QStringLiteral("获取最近播放记录。"),
        QJsonObject{{QStringLiteral("limit"), numProp(QStringLiteral("返回数量，默认 100，最大 1000"), 1, 1000)}});

    // ── 本地库 ─────────────────────────────────────────
    add(QStringLiteral("list_local_playlists"), QStringLiteral("获取本地歌单列表。"));
    add(QStringLiteral("get_local_playlist_tracks"), QStringLiteral("获取本地歌单内的曲目列表。"),
        QJsonObject{{QStringLiteral("local_id"), numProp(QStringLiteral("本地歌单 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("local_id")});
    add(QStringLiteral("create_local_playlist"), QStringLiteral("新建本地歌单。"),
        QJsonObject{{QStringLiteral("name"), strProp(QStringLiteral("歌单名称"))},
                    {QStringLiteral("description"), strProp(QStringLiteral("歌单描述，可选"))}},
        QJsonArray{QStringLiteral("name")});
    add(QStringLiteral("delete_local_playlist"), QStringLiteral("删除本地歌单（不可恢复）。"),
        QJsonObject{{QStringLiteral("local_id"), numProp(QStringLiteral("本地歌单 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("local_id")});
    add(QStringLiteral("add_music_to_local_playlist"), QStringLiteral("把曲目加入本地歌单。"),
        QJsonObject{{QStringLiteral("local_id"), numProp(QStringLiteral("本地歌单 ID"), 1, 1e12)},
                    {QStringLiteral("music_id"), musicIdProp()}},
        QJsonArray{QStringLiteral("local_id"), QStringLiteral("music_id")});
    add(QStringLiteral("remove_music_from_local_playlist"), QStringLiteral("把曲目移出本地歌单。"),
        QJsonObject{{QStringLiteral("local_id"), numProp(QStringLiteral("本地歌单 ID"), 1, 1e12)},
                    {QStringLiteral("music_id"), musicIdProp()}},
        QJsonArray{QStringLiteral("local_id"), QStringLiteral("music_id")});
    add(QStringLiteral("play_local_playlist"), QStringLiteral("用本地歌单的曲目替换播放队列并开始播放。"),
        QJsonObject{{QStringLiteral("local_id"), numProp(QStringLiteral("本地歌单 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("local_id")});

    // ── 界面导航 ───────────────────────────────────────
    add(QStringLiteral("open_page"), QStringLiteral("切换主窗口页面。"),
        QJsonObject{{QStringLiteral("page"),
                     enumProp(QStringLiteral("页面"),
                              {QStringLiteral("home"), QStringLiteral("favorites"),
                               QStringLiteral("recent"), QStringLiteral("downloads"),
                               QStringLiteral("hot"), QStringLiteral("latest"),
                               QStringLiteral("daily"), QStringLiteral("settings")})}},
        QJsonArray{QStringLiteral("page")});

    // ── 音质 / 桌面歌词 / 主题 / 麦克风 / 局域网 ─────────
    add(QStringLiteral("get_audio_quality"), QStringLiteral("获取当前播放音质偏好。"));
    add(QStringLiteral("set_audio_quality"), QStringLiteral("切换播放音质。"),
        QJsonObject{{QStringLiteral("quality"),
                     enumProp(QStringLiteral("音质档位"),
                              {QStringLiteral("standard"), QStringLiteral("hq"),
                               QStringLiteral("sq"), QStringLiteral("hires")})}},
        QJsonArray{QStringLiteral("quality")});
    add(QStringLiteral("get_desktop_lyrics"), QStringLiteral("获取桌面歌词开关状态。"));
    add(QStringLiteral("set_desktop_lyrics"), QStringLiteral("打开/关闭桌面歌词。"),
        QJsonObject{{QStringLiteral("enabled"), prop(QStringLiteral("boolean"), QStringLiteral("是否开启"))}},
        QJsonArray{QStringLiteral("enabled")});
    add(QStringLiteral("get_theme_mode"), QStringLiteral("获取界面主题模式。"));
    add(QStringLiteral("set_theme_mode"), QStringLiteral("设置界面主题模式。"),
        QJsonObject{{QStringLiteral("mode"),
                     enumProp(QStringLiteral("主题"),
                              {QStringLiteral("system"), QStringLiteral("dark"),
                               QStringLiteral("light")})}},
        QJsonArray{QStringLiteral("mode")});
    add(QStringLiteral("get_mic_sync"), QStringLiteral("获取麦克风同步（把音乐混入虚拟麦克风）状态。"));
    add(QStringLiteral("set_mic_sync"), QStringLiteral("打开/关闭麦克风同步。"),
        QJsonObject{{QStringLiteral("enabled"), prop(QStringLiteral("boolean"), QStringLiteral("是否开启"))}},
        QJsonArray{QStringLiteral("enabled")});
    add(QStringLiteral("list_lan_devices"), QStringLiteral("列出局域网内发现的其他客户端设备。"));
    add(QStringLiteral("select_lan_device"),
        QStringLiteral("选择要同步/查看的局域网设备（device_id 传空串表示切回本机）。"),
        QJsonObject{{QStringLiteral("device_id"), strProp(QStringLiteral("设备 ID，空串=本机"))}},
        QJsonArray{QStringLiteral("device_id")});
    add(QStringLiteral("get_remote_queue"), QStringLiteral("获取所选局域网设备的远程播放队列快照。"));

    // ── 站内歌单广场 / 我的上传 ─────────────────────────
    add(QStringLiteral("search_playlists"), QStringLiteral("按关键词搜索站内歌单。"),
        QJsonObject{{QStringLiteral("query"), strProp(QStringLiteral("搜索关键词"))}},
        QJsonArray{QStringLiteral("query")});
    add(QStringLiteral("get_uploaded_music"), QStringLiteral("获取当前用户上传并审核通过的曲目（需登录）。"));

    // ── 评论 / 消息 ────────────────────────────────────
    add(QStringLiteral("post_comment"), QStringLiteral("发表评论或回复（需登录）。"),
        QJsonObject{{QStringLiteral("music_id"), musicIdProp()},
                    {QStringLiteral("content"), strProp(QStringLiteral("评论内容"))},
                    {QStringLiteral("parent_id"), numProp(QStringLiteral("父评论 ID，0 表示发表新评论"), 0, 1e12)}},
        QJsonArray{QStringLiteral("music_id"), QStringLiteral("content")});
    add(QStringLiteral("delete_comment"), QStringLiteral("删除自己的评论（需登录）。"),
        QJsonObject{{QStringLiteral("comment_id"), numProp(QStringLiteral("评论 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("comment_id")});
    add(QStringLiteral("mark_notifications_read"),
        QStringLiteral("把站内消息标记为已读；不传 ids 表示全部已读（需登录）。"),
        QJsonObject{{QStringLiteral("ids"),
                     arrayProp(QStringLiteral("number"), QStringLiteral("消息 ID 列表，缺省=全部已读"))}});

    // ── 批量操作 ───────────────────────────────────────
    add(QStringLiteral("batch_search_music"),
        QStringLiteral("按 标题/歌手 列表批量匹配曲目，返回命中的曲目 ID（用于导入）。"),
        QJsonObject{{QStringLiteral("items"),
                     QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                 {QStringLiteral("description"), QStringLiteral("待匹配曲目列表")},
                                 {QStringLiteral("items"),
                                  QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                              {QStringLiteral("properties"),
                                               QJsonObject{{QStringLiteral("title"), strProp(QStringLiteral("标题"))},
                                                           {QStringLiteral("artist"), strProp(QStringLiteral("歌手"))}}}}}}}},
        QJsonArray{QStringLiteral("items")});
    add(QStringLiteral("batch_add_favorites"), QStringLiteral("批量收藏曲目（需登录）。"),
        QJsonObject{{QStringLiteral("music_ids"),
                     arrayProp(QStringLiteral("number"), QStringLiteral("曲目 ID 列表"))}},
        QJsonArray{QStringLiteral("music_ids")});
    add(QStringLiteral("batch_add_to_playlist"), QStringLiteral("批量把曲目加入歌单（需登录）。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)},
                    {QStringLiteral("music_ids"),
                     arrayProp(QStringLiteral("number"), QStringLiteral("曲目 ID 列表"))}},
        QJsonArray{QStringLiteral("playlist_id"), QStringLiteral("music_ids")});

    // ── 本地音乐 ───────────────────────────────────────
    add(QStringLiteral("open_local_file"), QStringLiteral("用客户端打开本地音频/播放列表文件（绝对路径）。"),
        QJsonObject{{QStringLiteral("path"), strProp(QStringLiteral("本地文件绝对路径"))}},
        QJsonArray{QStringLiteral("path")});
    add(QStringLiteral("clear_recently_played"), QStringLiteral("清空最近播放记录。"));
    add(QStringLiteral("resume_last_played"), QStringLiteral("继续播放上次退出时正在播放的曲目。"));
    add(QStringLiteral("update_local_playlist"), QStringLiteral("修改本地歌单名称/描述。"),
        QJsonObject{{QStringLiteral("local_id"), numProp(QStringLiteral("本地歌单 ID"), 1, 1e12)},
                    {QStringLiteral("name"), strProp(QStringLiteral("新名称，可选"))},
                    {QStringLiteral("description"), strProp(QStringLiteral("新描述，可选"))}},
        QJsonArray{QStringLiteral("local_id")});

    // ── 下载扩展 ───────────────────────────────────────
    add(QStringLiteral("download_playlist"), QStringLiteral("把整张歌单加入下载队列。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("playlist_id")});
    add(QStringLiteral("remove_download"), QStringLiteral("移除某曲目的下载记录。"), musicIdField());

    // ── 外部歌单导入 ───────────────────────────────────
    add(QStringLiteral("get_external_playlist"),
        QStringLiteral("预览外部平台歌单（网易云/QQ/酷狗/汽水）的曲目列表。"),
        QJsonObject{{QStringLiteral("source"),
                     enumProp(QStringLiteral("来源平台"),
                              {QStringLiteral("netease"), QStringLiteral("qq"),
                               QStringLiteral("kugou"), QStringLiteral("qishui")})},
                    {QStringLiteral("playlist_id"), strProp(QStringLiteral("外部歌单 ID"))}},
        QJsonArray{QStringLiteral("source"), QStringLiteral("playlist_id")});
    add(QStringLiteral("start_external_import"),
        QStringLiteral("启动外部歌单导入（后台把站外曲目匹配入库并加入目标歌单），立即返回；"
                       "进度用 get_import_status 查询。target_playlist_id 与 target_name 至少给一个。"),
        QJsonObject{{QStringLiteral("source"),
                     enumProp(QStringLiteral("来源平台"),
                              {QStringLiteral("netease"), QStringLiteral("qq"),
                               QStringLiteral("kugou"), QStringLiteral("qishui")})},
                    {QStringLiteral("external_id"), strProp(QStringLiteral("外部歌单 ID"))},
                    {QStringLiteral("target_playlist_id"), numProp(QStringLiteral("站内目标歌单 ID，可选"), 1, 1e12)},
                    {QStringLiteral("target_name"), strProp(QStringLiteral("新建站内歌单名称，可选"))}},
        QJsonArray{QStringLiteral("source"), QStringLiteral("external_id")});
    add(QStringLiteral("get_import_status"), QStringLiteral("查询最近一次外部歌单导入的进度与结果。"));

    // ── 分享视频渲染 ───────────────────────────────────
    add(QStringLiteral("create_video_render"), QStringLiteral("创建分享视频渲染任务（需登录且有额度）。"),
        QJsonObject{{QStringLiteral("music_id"), musicIdProp()},
                    {QStringLiteral("start_sec"), numProp(QStringLiteral("起始秒数，默认 0"), 0, 3600)},
                    {QStringLiteral("watermarked"), prop(QStringLiteral("boolean"), QStringLiteral("是否加水印，默认 true"))}},
        QJsonArray{QStringLiteral("music_id")});
    add(QStringLiteral("get_video_render_status"), QStringLiteral("查询视频渲染任务状态。"),
        QJsonObject{{QStringLiteral("job_id"), strProp(QStringLiteral("任务 ID"))}},
        QJsonArray{QStringLiteral("job_id")});
    add(QStringLiteral("download_video_render"), QStringLiteral("下载渲染完成的视频到本地。"),
        QJsonObject{{QStringLiteral("job_id"), strProp(QStringLiteral("任务 ID"))},
                    {QStringLiteral("save_path"), strProp(QStringLiteral("保存路径，缺省存到下载目录"))}},
        QJsonArray{QStringLiteral("job_id")});

    // ── 账户 / 更新 ────────────────────────────────────
    add(QStringLiteral("logout"), QStringLiteral("退出当前登录账号。"));
    add(QStringLiteral("get_vip_pricing"), QStringLiteral("获取会员套餐与价格（需登录）。"));
    add(QStringLiteral("check_update"), QStringLiteral("检查客户端版本更新。"));

    // ── 页面跳转 / 一键播放 ────────────────────────────
    add(QStringLiteral("open_playlist_page"), QStringLiteral("打开指定歌单的详情页。"),
        QJsonObject{{QStringLiteral("playlist_id"), numProp(QStringLiteral("歌单 ID"), 1, 1e12)}},
        QJsonArray{QStringLiteral("playlist_id")});
    add(QStringLiteral("open_artist_page"), QStringLiteral("搜索并打开歌手详情页。"),
        QJsonObject{{QStringLiteral("query"), strProp(QStringLiteral("歌手名"))}},
        QJsonArray{QStringLiteral("query")});
    add(QStringLiteral("play_ranking"), QStringLiteral("用热门排行榜整队播放。"));
    add(QStringLiteral("play_latest_music"), QStringLiteral("用最新音乐整队播放。"),
        QJsonObject{{QStringLiteral("limit"), numProp(QStringLiteral("数量，默认 30，最大 100"), 1, 100)}});
    add(QStringLiteral("play_daily_recommendations"), QStringLiteral("用每日推荐整队播放（需登录）。"));
    add(QStringLiteral("play_favorites"), QStringLiteral("用收藏曲目整队播放（需登录）。"));

    return tools;
}

void McpBridge::callTool(const QString &name, const QJsonObject &arguments, Done done)
{
    if (!done)
        return;
    const auto it = m_handlers.constFind(name);
    if (it == m_handlers.constEnd()) {
        done(false, QStringLiteral("未知工具：%1").arg(name), QJsonObject());
        return;
    }
    // 兜底保证回调只触发一次，避免异步分支重复回复。
    auto called = std::make_shared<bool>(false);
    Done guard = [done, called](bool ok, const QString &text, const QJsonObject &structured) {
        if (*called)
            return;
        *called = true;
        done(ok, text, structured);
    };
    it.value()(arguments, std::move(guard));
}

void McpBridge::initHandlers()
{
    // ── 播放控制 ───────────────────────────────────────
    m_handlers.insert(QStringLiteral("get_playback_state"), [this](const QJsonObject &, Done done) {
        if (!m_engine) {
            done(false, QStringLiteral("播放引擎不可用"), QJsonObject());
            return;
        }
        const QJsonObject state = playbackStateJson();
        done(true,
             QStringLiteral("状态：%1，音量 %2%，进度 %3s/%4s")
                 .arg(state.value(QStringLiteral("state")).toString())
                 .arg(state.value(QStringLiteral("volume")).toInt())
                 .arg(state.value(QStringLiteral("position_seconds")).toInt())
                 .arg(state.value(QStringLiteral("duration_seconds")).toInt()),
             state);
    });

    m_handlers.insert(QStringLiteral("get_current_track"), [this](const QJsonObject &, Done done) {
        if (!m_engine) {
            done(false, QStringLiteral("播放引擎不可用"), QJsonObject());
            return;
        }
        const MusicInfo &current = m_engine->currentMusic();
        if (current.title.isEmpty() && current.id == 0) {
            done(true, QStringLiteral("当前没有正在播放的曲目"),
                 QJsonObject{{QStringLiteral("track"), QJsonValue()}});
            return;
        }
        done(true, QStringLiteral("正在播放：%1 - %2").arg(current.title, current.artist), trackJson(current));
    });

    m_handlers.insert(QStringLiteral("play"), [this](const QJsonObject &, Done done) {
        if (!m_engine) {
            done(false, QStringLiteral("播放引擎不可用"), QJsonObject());
            return;
        }
        m_engine->play();
        done(true, QStringLiteral("已继续播放"), QJsonObject());
    });
    m_handlers.insert(QStringLiteral("pause"), [this](const QJsonObject &, Done done) {
        if (!m_engine) {
            done(false, QStringLiteral("播放引擎不可用"), QJsonObject());
            return;
        }
        m_engine->pause();
        done(true, QStringLiteral("已暂停"), QJsonObject());
    });
    m_handlers.insert(QStringLiteral("toggle_playback"), [this](const QJsonObject &, Done done) {
        if (!m_engine) {
            done(false, QStringLiteral("播放引擎不可用"), QJsonObject());
            return;
        }
        if (m_engine->playbackState() == PlayerEngine::Playing)
            m_engine->pause();
        else
            m_engine->play();
        done(true, QStringLiteral("当前状态：%1").arg(stateName(m_engine->playbackState())),
             QJsonObject{{QStringLiteral("state"), stateName(m_engine->playbackState())}});
    });
    m_handlers.insert(QStringLiteral("stop"), [this](const QJsonObject &, Done done) {
        if (!m_engine) {
            done(false, QStringLiteral("播放引擎不可用"), QJsonObject());
            return;
        }
        m_engine->stop();
        done(true, QStringLiteral("已停止播放"), QJsonObject());
    });
    m_handlers.insert(QStringLiteral("next_track"), [this](const QJsonObject &, Done done) {
        if (!m_next) {
            done(false, QStringLiteral("当前未接入切歌回调"), QJsonObject());
            return;
        }
        m_next();
        done(true, QStringLiteral("已切到下一首"), QJsonObject());
    });
    m_handlers.insert(QStringLiteral("previous_track"), [this](const QJsonObject &, Done done) {
        if (!m_previous) {
            done(false, QStringLiteral("当前未接入切歌回调"), QJsonObject());
            return;
        }
        m_previous();
        done(true, QStringLiteral("已切到上一首"), QJsonObject());
    });
    m_handlers.insert(QStringLiteral("seek"), [this](const QJsonObject &arguments, Done done) {
        if (!m_engine) {
            done(false, QStringLiteral("播放引擎不可用"), QJsonObject());
            return;
        }
        const double seconds = arguments.value(QStringLiteral("position")).toDouble(-1);
        if (seconds < 0) {
            done(false, QStringLiteral("缺少参数 position"), QJsonObject());
            return;
        }
        m_engine->setPosition(static_cast<qint64>(seconds * 1000));
        done(true, QStringLiteral("已跳转到 %1 秒").arg(seconds),
             QJsonObject{{QStringLiteral("position_seconds"), seconds}});
    });
    m_handlers.insert(QStringLiteral("set_volume"), [this](const QJsonObject &arguments, Done done) {
        if (!m_engine) {
            done(false, QStringLiteral("播放引擎不可用"), QJsonObject());
            return;
        }
        const double volume = arguments.value(QStringLiteral("volume")).toDouble(-1);
        if (volume < 0 || volume > 100) {
            done(false, QStringLiteral("volume 需在 0-100 之间"), QJsonObject());
            return;
        }
        m_engine->setVolume(static_cast<float>(volume / 100.0));
        done(true, QStringLiteral("音量已设为 %1%").arg(static_cast<int>(volume)),
             QJsonObject{{QStringLiteral("volume"), static_cast<int>(volume)}});
    });
    m_handlers.insert(QStringLiteral("set_play_mode"), [](const QJsonObject &arguments, Done done) {
        const QString mode = arguments.value(QStringLiteral("mode")).toString();
        if (mode != QLatin1String("list") && mode != QLatin1String("single")
            && mode != QLatin1String("random")) {
            done(false, QStringLiteral("mode 仅支持 list / single / random"), QJsonObject());
            return;
        }
        PlaylistManager::instance().setPlayMode(mode);
        done(true, QStringLiteral("播放模式已设为 %1").arg(mode),
             QJsonObject{{QStringLiteral("play_mode"), mode}});
    });

    // ── 播放队列 ───────────────────────────────────────
    m_handlers.insert(QStringLiteral("get_queue"), [](const QJsonObject &, Done done) {
        const QList<MusicInfo> &queue = PlaylistManager::instance().playlist();
        const int index = PlaylistManager::instance().currentIndex();
        done(true, QStringLiteral("队列共 %1 首，当前第 %2 首").arg(queue.size()).arg(index + 1),
             QJsonObject{{QStringLiteral("current_index"), index},
                         {QStringLiteral("play_mode"), PlaylistManager::instance().playMode()},
                         {QStringLiteral("tracks"), tracksJson(queue)}});
    });

    m_handlers.insert(QStringLiteral("play_queue_index"), [this](const QJsonObject &arguments, Done done) {
        const QList<MusicInfo> &queue = PlaylistManager::instance().playlist();
        const int index = arguments.value(QStringLiteral("index")).toInt(0);
        if (index < 1 || index > queue.size()) {
            done(false, QStringLiteral("index 超出队列范围（1-%1）").arg(queue.size()), QJsonObject());
            return;
        }
        if (!m_play) {
            done(false, QStringLiteral("当前未接入播放回调"), QJsonObject());
            return;
        }
        PlaylistManager::instance().setCurrentIndex(index - 1);
        const MusicInfo info = queue.at(index - 1);
        m_play(info);
        done(true, QStringLiteral("播放队列第 %1 首：%2 - %3").arg(index).arg(info.title, info.artist),
             QJsonObject{{QStringLiteral("index"), index}, {QStringLiteral("track"), trackJson(info)}});
    });

    m_handlers.insert(QStringLiteral("add_to_queue"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        resolveTrack(musicId, [done](bool, const MusicInfo &info, const QString &err) {
            if (!err.isEmpty()) {
                done(false, err, QJsonObject());
                return;
            }
            PlaylistManager::instance().addToPlaylist(info);
            done(true, QStringLiteral("已加入播放队列：%1 - %2").arg(info.title, info.artist),
                 QJsonObject{{QStringLiteral("track"), trackJson(info)},
                             {QStringLiteral("queue_size"), PlaylistManager::instance().count()}});
        });
    });

    m_handlers.insert(QStringLiteral("add_search_result_to_queue"), [this](const QJsonObject &arguments, Done done) {
        const QString query = arguments.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) {
            done(false, QStringLiteral("缺少参数 query"), QJsonObject());
            return;
        }
        const int index = qMax(1, arguments.value(QStringLiteral("index")).toInt(1));
        runSearch(query, index, [done, index](bool success, const QList<MusicInfo> &tracks, const QString &error) {
            if (!success) {
                done(false, error, QJsonObject());
                return;
            }
            if (tracks.size() < index) {
                done(false, QStringLiteral("搜索结果不足 %1 条").arg(index), QJsonObject());
                return;
            }
            const MusicInfo &info = tracks.at(index - 1);
            PlaylistManager::instance().addToPlaylist(info);
            done(true, QStringLiteral("已加入播放队列：%1 - %2").arg(info.title, info.artist),
                 QJsonObject{{QStringLiteral("track"), trackJson(info)}});
        });
    });

    m_handlers.insert(QStringLiteral("play_next"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        resolveTrack(musicId, [this, done](bool, const MusicInfo &info, const QString &err) {
            if (!err.isEmpty()) {
                done(false, err, QJsonObject());
                return;
            }
            if (m_playNext) {
                m_playNext(info);
            } else {
                const bool queued = PlaylistManager::instance().playNext(info);
                if (!queued && m_play)
                    m_play(info);
            }
            done(true, QStringLiteral("已设为下一首播放：%1 - %2").arg(info.title, info.artist),
                 QJsonObject{{QStringLiteral("track"), trackJson(info)}});
        });
    });

    m_handlers.insert(QStringLiteral("remove_from_queue"), [](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        PlaylistManager::instance().removeFromPlaylist(musicId);
        done(true, QStringLiteral("已从播放队列移除曲目 %1").arg(musicId),
             QJsonObject{{QStringLiteral("music_id"), musicId},
                         {QStringLiteral("queue_size"), PlaylistManager::instance().count()}});
    });

    m_handlers.insert(QStringLiteral("clear_queue"), [](const QJsonObject &, Done done) {
        PlaylistManager::instance().clearPlaylist();
        done(true, QStringLiteral("已清空播放队列"), QJsonObject());
    });

    // ── 搜索与内容 ─────────────────────────────────────
    m_handlers.insert(QStringLiteral("search_music"), [this](const QJsonObject &arguments, Done done) {
        const QString query = arguments.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) {
            done(false, QStringLiteral("缺少参数 query"), QJsonObject());
            return;
        }
        const int limit = qBound(1, arguments.value(QStringLiteral("limit")).toInt(10), 50);
        runSearch(query, limit, [done](bool success, const QList<MusicInfo> &tracks, const QString &error) {
            if (!success) {
                done(false, error, QJsonObject());
                return;
            }
            done(true, QStringLiteral("找到 %1 首匹配曲目").arg(tracks.size()),
                 QJsonObject{{QStringLiteral("tracks"), tracksJson(tracks)}});
        });
    });

    m_handlers.insert(QStringLiteral("play_music"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_play) {
            done(false, QStringLiteral("当前未接入播放回调"), QJsonObject());
            return;
        }
        resolveTrack(musicId, [this, musicId, done](bool, const MusicInfo &info, const QString &err) {
            if (!err.isEmpty()) {
                done(false, err, QJsonObject());
                return;
            }
            m_play(info);
            if (!info.title.isEmpty())
                done(true, QStringLiteral("正在播放：%1 - %2").arg(info.title, info.artist),
                     QJsonObject{{QStringLiteral("track"), trackJson(info)}});
            else
                done(true, QStringLiteral("已请求播放曲目 %1").arg(musicId),
                     QJsonObject{{QStringLiteral("music_id"), musicId}});
        });
    });

    m_handlers.insert(QStringLiteral("play_search_result"), [this](const QJsonObject &arguments, Done done) {
        const QString query = arguments.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) {
            done(false, QStringLiteral("缺少参数 query"), QJsonObject());
            return;
        }
        const int index = qMax(1, arguments.value(QStringLiteral("index")).toInt(1));
        if (!m_play) {
            done(false, QStringLiteral("当前未接入播放回调"), QJsonObject());
            return;
        }
        runSearch(query, index, [this, index, done](bool success, const QList<MusicInfo> &tracks, const QString &error) {
            if (!success) {
                done(false, error, QJsonObject());
                return;
            }
            if (tracks.size() < index) {
                done(false, QStringLiteral("搜索结果不足 %1 条").arg(index), QJsonObject());
                return;
            }
            const MusicInfo &info = tracks.at(index - 1);
            m_play(info);
            done(true, QStringLiteral("正在播放：%1 - %2").arg(info.title, info.artist),
                 QJsonObject{{QStringLiteral("track"), trackJson(info)}});
        });
    });

    m_handlers.insert(QStringLiteral("search_artists"), [this](const QJsonObject &arguments, Done done) {
        QString query;
        QString error;
        if (!readNonEmptyString(arguments, QStringLiteral("query"), &query, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("搜索接口不可用"), QJsonObject());
            return;
        }
        m_api->searchArtists(query, [done](bool ok, const QVariantMap &artist) {
            if (!ok) {
                done(false, QStringLiteral("歌手搜索失败，请稍后重试"), QJsonObject());
                return;
            }
            done(true, QStringLiteral("找到歌手：%1").arg(artist.value(QStringLiteral("name")).toString()),
                 QJsonObject{{QStringLiteral("artist"), QJsonObject::fromVariantMap(artist)}});
        });
    });

    m_handlers.insert(QStringLiteral("get_ranking"), [this](const QJsonObject &, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchRanking([done](bool ok, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取排行榜失败，请稍后重试"), QJsonObject());
                return;
            }
            const QList<MusicInfo> tracks = tracksFromMaps(list);
            done(true, QStringLiteral("排行榜共 %1 首").arg(tracks.size()),
                 QJsonObject{{QStringLiteral("tracks"), tracksJson(tracks)}});
        });
    });

    m_handlers.insert(QStringLiteral("get_latest_music"), [this](const QJsonObject &arguments, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        const int limit = qBound(1, arguments.value(QStringLiteral("limit")).toInt(30), 100);
        m_api->fetchLatest(limit, [done](bool ok, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取最新音乐失败，请稍后重试"), QJsonObject());
                return;
            }
            const QList<MusicInfo> tracks = tracksFromMaps(list);
            done(true, QStringLiteral("最新音乐共 %1 首").arg(tracks.size()),
                 QJsonObject{{QStringLiteral("tracks"), tracksJson(tracks)}});
        });
    });

    m_handlers.insert(QStringLiteral("get_daily_recommendations"), [this](const QJsonObject &, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchDailyRecommendations([done](bool ok, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取每日推荐失败（可能未登录）"), QJsonObject());
                return;
            }
            const QList<MusicInfo> tracks = tracksFromMaps(list);
            done(true, QStringLiteral("每日推荐共 %1 首").arg(tracks.size()),
                 QJsonObject{{QStringLiteral("tracks"), tracksJson(tracks)}});
        });
    });

    m_handlers.insert(QStringLiteral("get_music_detail"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchMusicInfo(musicId, [musicId, done](bool ok, const QVariantMap &data) {
            if (!ok) {
                done(false, QStringLiteral("获取曲目详情失败"), QJsonObject());
                return;
            }
            done(true, QStringLiteral("曲目 %1：%2").arg(musicId).arg(data.value(QStringLiteral("title")).toString()),
                 QJsonObject{{QStringLiteral("music"), QJsonObject::fromVariantMap(data)}});
        });
    });

    m_handlers.insert(QStringLiteral("get_lyrics"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchLyrics(musicId, [musicId, done](bool ok, const QString &lyrics) {
            if (!ok) {
                done(false, QStringLiteral("获取歌词失败"), QJsonObject());
                return;
            }
            done(true, lyrics.isEmpty() ? QStringLiteral("该曲目暂无歌词")
                                        : QStringLiteral("已获取曲目 %1 的歌词").arg(musicId),
                 QJsonObject{{QStringLiteral("music_id"), musicId},
                             {QStringLiteral("lyrics"), lyrics}});
        });
    });

    m_handlers.insert(QStringLiteral("get_comments"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        const int page = qBound(1, arguments.value(QStringLiteral("page")).toInt(1), 10000);
        const int pageSize = qBound(1, arguments.value(QStringLiteral("page_size")).toInt(20), 50);
        m_api->fetchComments(musicId, page, pageSize,
                             [done](bool ok, const QString &message, const QVariantMap &data) {
                                 if (!ok) {
                                     done(false, message.isEmpty() ? QStringLiteral("获取评论失败") : message,
                                          QJsonObject());
                                     return;
                                 }
                                 const int total = data.value(QStringLiteral("total")).toInt();
                                 done(true, QStringLiteral("共获取 %1 条评论").arg(total),
                                      QJsonObject::fromVariantMap(data));
                             });
    });

    // ── 收藏 ───────────────────────────────────────────
    m_handlers.insert(QStringLiteral("get_favorites"), [this](const QJsonObject &, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchFavorites([done](bool ok, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取收藏列表失败（可能未登录）"), QJsonObject());
                return;
            }
            const QList<MusicInfo> tracks = tracksFromMaps(list);
            done(true, QStringLiteral("收藏共 %1 首").arg(tracks.size()),
                 QJsonObject{{QStringLiteral("tracks"), tracksJson(tracks)}});
        });
    });

    m_handlers.insert(QStringLiteral("is_favorite"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_isFavorited) {
            done(false, QStringLiteral("收藏状态不可用"), QJsonObject());
            return;
        }
        const bool favorite = m_isFavorited(musicId);
        done(true, favorite ? QStringLiteral("曲目 %1 已收藏").arg(musicId)
                            : QStringLiteral("曲目 %1 未收藏").arg(musicId),
             QJsonObject{{QStringLiteral("music_id"), musicId},
                         {QStringLiteral("favorite"), favorite}});
    });

    m_handlers.insert(QStringLiteral("add_favorite"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        applyFavorite(musicId, true, done);
    });

    m_handlers.insert(QStringLiteral("remove_favorite"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        applyFavorite(musicId, false, done);
    });

    m_handlers.insert(QStringLiteral("toggle_favorite"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        const bool favorite = m_isFavorited ? m_isFavorited(musicId) : false;
        applyFavorite(musicId, !favorite, done);
    });

    // ── 在线歌单 ───────────────────────────────────────
    m_handlers.insert(QStringLiteral("list_playlists"), [this](const QJsonObject &, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchUserPlaylists([done](bool ok, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取歌单列表失败（可能未登录）"), QJsonObject());
                return;
            }
            done(true, QStringLiteral("共 %1 个歌单").arg(list.size()),
                 QJsonObject{{QStringLiteral("playlists"), mapsToJson(list)}});
        });
    });

    m_handlers.insert(QStringLiteral("get_playlist_detail"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchPlaylistDetail(playlistId, [done](bool ok, const QVariantMap &detail) {
            if (!ok) {
                done(false, QStringLiteral("获取歌单详情失败"), QJsonObject());
                return;
            }
            done(true, QStringLiteral("歌单：%1").arg(detail.value(QStringLiteral("name")).toString()),
                 QJsonObject{{QStringLiteral("playlist"), QJsonObject::fromVariantMap(detail)}});
        });
    });

    m_handlers.insert(QStringLiteral("get_playlist_tracks"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchPlaylistMusic(playlistId, [done](bool ok, int total, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取歌单曲目失败"), QJsonObject());
                return;
            }
            const QList<MusicInfo> tracks = tracksFromMaps(list);
            done(true, QStringLiteral("歌单共 %1 首曲目").arg(total > 0 ? total : tracks.size()),
                 QJsonObject{{QStringLiteral("total"), total},
                             {QStringLiteral("tracks"), tracksJson(tracks)}});
        });
    });

    m_handlers.insert(QStringLiteral("play_playlist"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchPlaylistMusic(playlistId, [this, done](bool ok, int, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取歌单曲目失败"), QJsonObject());
                return;
            }
            playTrackList(tracksFromMaps(list), 0, done, QStringLiteral("歌单"));
        });
    });

    m_handlers.insert(QStringLiteral("create_playlist"), [this](const QJsonObject &arguments, Done done) {
        QString name;
        QString error;
        if (!readNonEmptyString(arguments, QStringLiteral("name"), &name, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        const QString description = arguments.value(QStringLiteral("description")).toString();
        m_api->createPlaylist(name, description,
                              [done](bool ok, const QString &message, const QVariantMap &playlist) {
                                  if (!ok) {
                                      done(false, message.isEmpty() ? QStringLiteral("创建歌单失败") : message,
                                           QJsonObject());
                                      return;
                                  }
                                  done(true, QStringLiteral("已创建歌单：%1").arg(message),
                                       QJsonObject::fromVariantMap(playlist));
                              });
    });

    m_handlers.insert(QStringLiteral("update_playlist"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        // 未提供的字段传空（isNull）则后端保持原值。
        const QString name = arguments.contains(QStringLiteral("name"))
                                 ? arguments.value(QStringLiteral("name")).toString()
                                 : QString();
        const QString description = arguments.contains(QStringLiteral("description"))
                                        ? arguments.value(QStringLiteral("description")).toString()
                                        : QString();
        m_api->updatePlaylist(playlistId, name, description,
                              [done](bool ok, const QString &message, const QVariantMap &playlist) {
                                  if (!ok) {
                                      done(false, message.isEmpty() ? QStringLiteral("更新歌单失败") : message,
                                           QJsonObject());
                                      return;
                                  }
                                  done(true, QStringLiteral("歌单已更新"),
                                       QJsonObject::fromVariantMap(playlist));
                              });
    });

    m_handlers.insert(QStringLiteral("delete_playlist"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->deletePlaylist(playlistId, [playlistId, done](bool ok, const QString &message) {
            if (!ok) {
                done(false, message.isEmpty() ? QStringLiteral("删除歌单失败") : message, QJsonObject());
                return;
            }
            done(true, QStringLiteral("已删除歌单 %1").arg(playlistId),
                 QJsonObject{{QStringLiteral("playlist_id"), playlistId}});
        });
    });

    m_handlers.insert(QStringLiteral("add_music_to_playlist"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)
            || !readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->addMusicToPlaylist(playlistId, musicId,
                                  [playlistId, musicId, done](bool ok, const QString &message) {
                                      if (!ok) {
                                          done(false,
                                               message.isEmpty() ? QStringLiteral("加入歌单失败") : message,
                                               QJsonObject());
                                          return;
                                      }
                                      done(true, QStringLiteral("已把曲目 %1 加入歌单 %2").arg(musicId).arg(playlistId),
                                           QJsonObject{{QStringLiteral("playlist_id"), playlistId},
                                                       {QStringLiteral("music_id"), musicId}});
                                  });
    });

    m_handlers.insert(QStringLiteral("remove_music_from_playlist"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)
            || !readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->removeMusicFromPlaylist(playlistId, musicId,
                                       [playlistId, musicId, done](bool ok, const QString &message) {
                                           if (!ok) {
                                               done(false,
                                                    message.isEmpty() ? QStringLiteral("移出歌单失败") : message,
                                                    QJsonObject());
                                               return;
                                           }
                                           done(true, QStringLiteral("已把曲目 %1 移出歌单 %2").arg(musicId).arg(playlistId),
                                                QJsonObject{{QStringLiteral("playlist_id"), playlistId},
                                                            {QStringLiteral("music_id"), musicId}});
                                       });
    });

    m_handlers.insert(QStringLiteral("list_favorite_playlists"), [this](const QJsonObject &, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchFavoritePlaylists([done](bool ok, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取收藏歌单失败（可能未登录）"), QJsonObject());
                return;
            }
            done(true, QStringLiteral("收藏歌单共 %1 个").arg(list.size()),
                 QJsonObject{{QStringLiteral("playlists"), mapsToJson(list)}});
        });
    });

    m_handlers.insert(QStringLiteral("favorite_playlist"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->favoritePlaylist(playlistId, [playlistId, done](bool ok, const QString &message) {
            if (!ok) {
                done(false, message.isEmpty() ? QStringLiteral("收藏歌单失败") : message, QJsonObject());
                return;
            }
            done(true, QStringLiteral("已收藏歌单 %1").arg(playlistId),
                 QJsonObject{{QStringLiteral("playlist_id"), playlistId}});
        });
    });

    m_handlers.insert(QStringLiteral("unfavorite_playlist"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->unfavoritePlaylist(playlistId, [playlistId, done](bool ok, const QString &message) {
            if (!ok) {
                done(false, message.isEmpty() ? QStringLiteral("取消收藏歌单失败") : message, QJsonObject());
                return;
            }
            done(true, QStringLiteral("已取消收藏歌单 %1").arg(playlistId),
                 QJsonObject{{QStringLiteral("playlist_id"), playlistId}});
        });
    });

    // ── 下载 ───────────────────────────────────────────
    m_handlers.insert(QStringLiteral("list_downloads"), [](const QJsonObject &, Done done) {
        const QList<MusicInfo> downloaded = PlaylistDatabase::instance().getDownloads();
        const QList<MusicInfo> pending = MusicDownloadManager::instance().pendingDownloads();
        done(true, QStringLiteral("已下载 %1 首，下载中 %2 首").arg(downloaded.size()).arg(pending.size()),
             QJsonObject{{QStringLiteral("download_dir"), MusicDownloadManager::instance().downloadDir()},
                         {QStringLiteral("downloaded"), tracksJson(downloaded)},
                         {QStringLiteral("pending"), tracksJson(pending)}});
    });

    m_handlers.insert(QStringLiteral("download_music"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        resolveTrack(musicId, [musicId, done](bool, const MusicInfo &info, const QString &err) {
            if (!err.isEmpty()) {
                done(false, err, QJsonObject());
                return;
            }
            auto &manager = MusicDownloadManager::instance();
            if (manager.isDownloaded(musicId)) {
                done(true, QStringLiteral("曲目 %1 已下载").arg(musicId),
                     QJsonObject{{QStringLiteral("music_id"), musicId},
                                 {QStringLiteral("status"), QStringLiteral("downloaded")}});
                return;
            }
            if (manager.isPending(musicId)) {
                done(true, QStringLiteral("曲目 %1 已在下载队列中").arg(musicId),
                     QJsonObject{{QStringLiteral("music_id"), musicId},
                                 {QStringLiteral("status"), QStringLiteral("pending")}});
                return;
            }
            if (manager.downloadMusic(info)) {
                done(true, QStringLiteral("已开始下载：%1 - %2").arg(info.title, info.artist),
                     QJsonObject{{QStringLiteral("music_id"), musicId},
                                 {QStringLiteral("status"), QStringLiteral("started")}});
            } else {
                done(false, QStringLiteral("加入下载队列失败"), QJsonObject());
            }
        });
    });

    m_handlers.insert(QStringLiteral("cancel_download"), [](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        MusicDownloadManager::instance().cancelDownload(musicId);
        done(true, QStringLiteral("已取消曲目 %1 的下载").arg(musicId),
             QJsonObject{{QStringLiteral("music_id"), musicId}});
    });

    // ── 账户与消息 ─────────────────────────────────────
    m_handlers.insert(QStringLiteral("get_user_info"), [this](const QJsonObject &, Done done) {
        if (!UserManager::instance().isLoggedIn()) {
            done(true, QStringLiteral("当前未登录"),
                 QJsonObject{{QStringLiteral("logged_in"), false}});
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchUserInfo([done](bool ok, const QString &message, const QVariantMap &user) {
            if (!ok) {
                done(false, message.isEmpty() ? QStringLiteral("获取用户信息失败") : message, QJsonObject());
                return;
            }
            done(true, QStringLiteral("已登录：%1").arg(user.value(QStringLiteral("nickname")).toString()),
                 QJsonObject{{QStringLiteral("logged_in"), true},
                             {QStringLiteral("user"), QJsonObject::fromVariantMap(user)}});
        });
    });

    m_handlers.insert(QStringLiteral("get_vip_status"), [](const QJsonObject &, Done done) {
        auto &user = UserManager::instance();
        done(true, user.isVip() ? QStringLiteral("当前是会员")
                                : QStringLiteral("当前不是会员"),
             QJsonObject{{QStringLiteral("logged_in"), user.isLoggedIn()},
                         {QStringLiteral("is_vip"), user.isVip()},
                         {QStringLiteral("vip_expires_at"), user.vipExpiresAt()}});
    });

    m_handlers.insert(QStringLiteral("get_notifications"), [this](const QJsonObject &arguments, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        const int limit = qBound(1, arguments.value(QStringLiteral("limit")).toInt(20), 50);
        m_api->fetchNotifications(0, 0, limit,
                                  [done](bool ok, const QString &message, const QVariantMap &data) {
                                      if (!ok) {
                                          done(false,
                                               message.isEmpty() ? QStringLiteral("获取消息失败（可能未登录）")
                                                                 : message,
                                               QJsonObject());
                                          return;
                                      }
                                      const int unread = data.value(QStringLiteral("unread")).toInt();
                                      done(true, QStringLiteral("未读消息 %1 条").arg(unread),
                                           QJsonObject::fromVariantMap(data));
                                  });
    });

    m_handlers.insert(QStringLiteral("get_recently_played"), [](const QJsonObject &arguments, Done done) {
        const int limit = qBound(1, arguments.value(QStringLiteral("limit")).toInt(100), 1000);
        const QList<MusicInfo> tracks = PlaylistDatabase::instance().getRecentPlays(limit);
        done(true, QStringLiteral("最近播放 %1 首").arg(tracks.size()),
             QJsonObject{{QStringLiteral("tracks"), tracksJson(tracks)}});
    });

    // ── 本地库 ─────────────────────────────────────────
    m_handlers.insert(QStringLiteral("list_local_playlists"), [](const QJsonObject &, Done done) {
        const QList<LocalPlaylistInfo> playlists = PlaylistDatabase::instance().getAllPlaylists();
        QJsonArray arr;
        for (const LocalPlaylistInfo &info : playlists) {
            arr.append(QJsonObject{{QStringLiteral("local_id"), info.localId},
                                   {QStringLiteral("name"), info.name},
                                   {QStringLiteral("description"), info.description},
                                   {QStringLiteral("cover_music_id"), info.coverMusicId},
                                   {QStringLiteral("track_count"),
                                    PlaylistDatabase::instance().getPlaylistMusicCount(info.localId)},
                                   {QStringLiteral("created_at"), info.createdAt},
                                   {QStringLiteral("updated_at"), info.updatedAt}});
        }
        done(true, QStringLiteral("本地歌单共 %1 个").arg(playlists.size()),
             QJsonObject{{QStringLiteral("playlists"), arr}});
    });

    m_handlers.insert(QStringLiteral("get_local_playlist_tracks"), [](const QJsonObject &arguments, Done done) {
        int localId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("local_id"), &localId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        const QList<MusicInfo> tracks = PlaylistDatabase::instance().getPlaylistMusic(localId);
        done(true, QStringLiteral("本地歌单共 %1 首曲目").arg(tracks.size()),
             QJsonObject{{QStringLiteral("local_id"), localId},
                         {QStringLiteral("tracks"), tracksJson(tracks)}});
    });

    m_handlers.insert(QStringLiteral("create_local_playlist"), [](const QJsonObject &arguments, Done done) {
        QString name;
        QString error;
        if (!readNonEmptyString(arguments, QStringLiteral("name"), &name, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        const QString description = arguments.value(QStringLiteral("description")).toString();
        const int localId = PlaylistDatabase::instance().createPlaylist(name, description);
        if (localId <= 0) {
            done(false, QStringLiteral("创建本地歌单失败"), QJsonObject());
            return;
        }
        done(true, QStringLiteral("已创建本地歌单：%1").arg(name),
             QJsonObject{{QStringLiteral("local_id"), localId},
                         {QStringLiteral("name"), name}});
    });

    m_handlers.insert(QStringLiteral("delete_local_playlist"), [](const QJsonObject &arguments, Done done) {
        int localId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("local_id"), &localId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!PlaylistDatabase::instance().deletePlaylist(localId)) {
            done(false, QStringLiteral("删除本地歌单失败"), QJsonObject());
            return;
        }
        done(true, QStringLiteral("已删除本地歌单 %1").arg(localId),
             QJsonObject{{QStringLiteral("local_id"), localId}});
    });

    m_handlers.insert(QStringLiteral("add_music_to_local_playlist"), [this](const QJsonObject &arguments, Done done) {
        int localId = 0;
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("local_id"), &localId, &error)
            || !readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        resolveTrack(musicId, [localId, musicId, done](bool, const MusicInfo &info, const QString &) {
            PlaylistDatabase::instance().addMusic(localId, info);
            done(true, QStringLiteral("已把曲目 %1 加入本地歌单 %2").arg(musicId).arg(localId),
                 QJsonObject{{QStringLiteral("local_id"), localId},
                             {QStringLiteral("music_id"), musicId}});
        });
    });

    m_handlers.insert(QStringLiteral("remove_music_from_local_playlist"), [](const QJsonObject &arguments, Done done) {
        int localId = 0;
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("local_id"), &localId, &error)
            || !readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        PlaylistDatabase::instance().removeMusic(localId, musicId);
        done(true, QStringLiteral("已把曲目 %1 移出本地歌单 %2").arg(musicId).arg(localId),
             QJsonObject{{QStringLiteral("local_id"), localId},
                         {QStringLiteral("music_id"), musicId}});
    });

    m_handlers.insert(QStringLiteral("play_local_playlist"), [this](const QJsonObject &arguments, Done done) {
        int localId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("local_id"), &localId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        const QList<MusicInfo> tracks = PlaylistDatabase::instance().getPlaylistMusic(localId);
        playTrackList(tracks, 0, done, QStringLiteral("本地歌单"));
    });

    // ── 界面导航 ───────────────────────────────────────
    m_handlers.insert(QStringLiteral("open_page"), [this](const QJsonObject &arguments, Done done) {
        static const QStringList allowed{QStringLiteral("home"), QStringLiteral("favorites"),
                                         QStringLiteral("recent"), QStringLiteral("downloads"),
                                         QStringLiteral("hot"), QStringLiteral("latest"),
                                         QStringLiteral("daily"), QStringLiteral("settings")};
        const QString page = arguments.value(QStringLiteral("page")).toString().trimmed();
        if (!allowed.contains(page)) {
            done(false, QStringLiteral("page 仅支持 home/favorites/recent/downloads/hot/latest/daily/settings"),
                 QJsonObject());
            return;
        }
        if (!m_openPage) {
            done(false, QStringLiteral("当前未接入页面切换回调"), QJsonObject());
            return;
        }
        m_openPage(page);
        done(true, QStringLiteral("已切换到 %1 页面").arg(page),
             QJsonObject{{QStringLiteral("page"), page}});
    });

    // ── 音质 / 桌面歌词 / 主题 / 麦克风 / 局域网 ─────────
    m_handlers.insert(QStringLiteral("get_audio_quality"), [this](const QJsonObject &, Done done) {
        if (!m_audioQuality) {
            done(false, QStringLiteral("音质信息不可用"), QJsonObject());
            return;
        }
        const QString quality = m_audioQuality();
        done(true, QStringLiteral("当前音质偏好：%1").arg(quality),
             QJsonObject{{QStringLiteral("quality"), quality},
                         {QStringLiteral("available"),
                          QJsonArray{QStringLiteral("standard"), QStringLiteral("hq"),
                                     QStringLiteral("sq"), QStringLiteral("hires")}}});
    });
    m_handlers.insert(QStringLiteral("set_audio_quality"), [this](const QJsonObject &arguments, Done done) {
        static const QStringList allowed{QStringLiteral("standard"), QStringLiteral("hq"),
                                         QStringLiteral("sq"), QStringLiteral("hires")};
        const QString quality = arguments.value(QStringLiteral("quality")).toString().trimmed().toLower();
        if (!allowed.contains(quality)) {
            done(false, QStringLiteral("quality 仅支持 standard / hq / sq / hires"), QJsonObject());
            return;
        }
        if (!m_setAudioQuality) {
            done(false, QStringLiteral("当前未接入音质切换回调"), QJsonObject());
            return;
        }
        if (!m_setAudioQuality(quality)) {
            done(false, QStringLiteral("音质切换失败"), QJsonObject());
            return;
        }
        done(true, QStringLiteral("播放音质已切换为 %1").arg(quality),
             QJsonObject{{QStringLiteral("quality"), quality}});
    });

    m_handlers.insert(QStringLiteral("get_desktop_lyrics"), [this](const QJsonObject &, Done done) {
        if (!m_desktopLyrics) {
            done(false, QStringLiteral("桌面歌词状态不可用"), QJsonObject());
            return;
        }
        const bool enabled = m_desktopLyrics();
        done(true, enabled ? QStringLiteral("桌面歌词已开启") : QStringLiteral("桌面歌词已关闭"),
             QJsonObject{{QStringLiteral("enabled"), enabled}});
    });
    m_handlers.insert(QStringLiteral("set_desktop_lyrics"), [this](const QJsonObject &arguments, Done done) {
        if (!arguments.value(QStringLiteral("enabled")).isBool()) {
            done(false, QStringLiteral("缺少布尔参数 enabled"), QJsonObject());
            return;
        }
        if (!m_setDesktopLyrics) {
            done(false, QStringLiteral("当前未接入桌面歌词回调"), QJsonObject());
            return;
        }
        const bool enabled = arguments.value(QStringLiteral("enabled")).toBool();
        m_setDesktopLyrics(enabled);
        done(true, enabled ? QStringLiteral("已开启桌面歌词") : QStringLiteral("已关闭桌面歌词"),
             QJsonObject{{QStringLiteral("enabled"), enabled}});
    });

    m_handlers.insert(QStringLiteral("get_theme_mode"), [](const QJsonObject &, Done done) {
        const Theme::ThemeMode mode = Theme::ThemeManager::instance().currentMode();
        const QString name = mode == Theme::Dark ? QStringLiteral("dark")
                             : mode == Theme::Light ? QStringLiteral("light")
                                                    : QStringLiteral("system");
        done(true, QStringLiteral("当前主题模式：%1").arg(name),
             QJsonObject{{QStringLiteral("mode"), name}});
    });
    m_handlers.insert(QStringLiteral("set_theme_mode"), [](const QJsonObject &arguments, Done done) {
        const QString mode = arguments.value(QStringLiteral("mode")).toString().trimmed().toLower();
        Theme::ThemeMode target;
        if (mode == QLatin1String("system"))
            target = Theme::System;
        else if (mode == QLatin1String("dark"))
            target = Theme::Dark;
        else if (mode == QLatin1String("light"))
            target = Theme::Light;
        else {
            done(false, QStringLiteral("mode 仅支持 system / dark / light"), QJsonObject());
            return;
        }
        Theme::ThemeManager::instance().setMode(target);
        done(true, QStringLiteral("主题模式已设为 %1").arg(mode),
             QJsonObject{{QStringLiteral("mode"), mode}});
    });

    m_handlers.insert(QStringLiteral("get_mic_sync"), [](const QJsonObject &, Done done) {
        auto &controller = MicSyncController::instance();
        const bool enabled = controller.isEnabled();
        done(true,
             !MicSyncController::isSupported()
                 ? QStringLiteral("当前平台不支持麦克风同步")
                 : (enabled ? QStringLiteral("麦克风同步已开启")
                            : QStringLiteral("麦克风同步已关闭")),
             QJsonObject{{QStringLiteral("supported"), MicSyncController::isSupported()},
                         {QStringLiteral("enabled"), enabled},
                         {QStringLiteral("device_name"), MicSyncController::deviceName()}});
    });
    m_handlers.insert(QStringLiteral("set_mic_sync"), [](const QJsonObject &arguments, Done done) {
        if (!arguments.value(QStringLiteral("enabled")).isBool()) {
            done(false, QStringLiteral("缺少布尔参数 enabled"), QJsonObject());
            return;
        }
        if (!MicSyncController::isSupported()) {
            done(false, QStringLiteral("当前平台不支持麦克风同步"), QJsonObject());
            return;
        }
        const bool enabled = arguments.value(QStringLiteral("enabled")).toBool();
        MicSyncController::instance().setEnabled(enabled);
        done(true,
             enabled ? QStringLiteral("已开启麦克风同步，请在语音软件里选择「%1」")
                           .arg(MicSyncController::deviceName())
                     : QStringLiteral("已关闭麦克风同步"),
             QJsonObject{{QStringLiteral("enabled"), enabled}});
    });

    m_handlers.insert(QStringLiteral("list_lan_devices"), [](const QJsonObject &, Done done) {
        auto &manager = LanDeviceManager::instance();
        const QList<LanDeviceInfo> &devices = manager.devices();
        QJsonArray arr;
        for (const LanDeviceInfo &device : devices) {
            arr.append(QJsonObject{{QStringLiteral("device_id"), device.deviceId},
                                   {QStringLiteral("device_name"), device.deviceName},
                                   {QStringLiteral("platform"), device.platform},
                                   {QStringLiteral("host"), device.host},
                                   {QStringLiteral("port"), device.port},
                                   {QStringLiteral("account_tag"), device.accountTag},
                                   {QStringLiteral("queue_count"), device.queueCount},
                                   {QStringLiteral("current_music_id"), device.currentMusicId},
                                   {QStringLiteral("selected"),
                                    device.deviceId == manager.selectedDeviceId()}});
        }
        done(true, QStringLiteral("发现 %1 台局域网设备").arg(devices.size()),
             QJsonObject{{QStringLiteral("devices"), arr},
                         {QStringLiteral("selected_device_id"), manager.selectedDeviceId()}});
    });
    m_handlers.insert(QStringLiteral("select_lan_device"), [](const QJsonObject &arguments, Done done) {
        const QString deviceId = arguments.value(QStringLiteral("device_id")).toString();
        LanDeviceManager::instance().selectDevice(deviceId);
        done(true,
             deviceId.isEmpty() ? QStringLiteral("已切回本机队列登录")
                                : QStringLiteral("已选择设备 %1").arg(deviceId),
             QJsonObject{{QStringLiteral("selected_device_id"), deviceId}});
    });
    m_handlers.insert(QStringLiteral("get_remote_queue"), [](const QJsonObject &, Done done) {
        auto &manager = LanDeviceManager::instance();
        const LanQueueSnapshotInfo &snapshot = manager.remoteQueue();
        done(true,
             QStringLiteral("远程队列 %1 首，当前第 %2 首")
                 .arg(snapshot.items.size())
                 .arg(snapshot.currentIndex + 1),
             QJsonObject{{QStringLiteral("selected_device_id"), manager.selectedDeviceId()},
                         {QStringLiteral("revision"), snapshot.revision},
                         {QStringLiteral("current_index"), snapshot.currentIndex},
                         {QStringLiteral("is_playing"), snapshot.isPlaying},
                         {QStringLiteral("play_mode"), snapshot.playMode},
                         {QStringLiteral("tracks"), tracksJson(snapshot.items)}});
    });

    // ── 站内歌单广场 / 我的上传 ─────────────────────────
    m_handlers.insert(QStringLiteral("search_playlists"), [this](const QJsonObject &arguments, Done done) {
        QString query;
        QString error;
        if (!readNonEmptyString(arguments, QStringLiteral("query"), &query, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchPlaylists(query, [done](bool ok, const QList<QVariantMap> &results) {
            if (!ok) {
                done(false, QStringLiteral("歌单搜索失败，请稍后重试"), QJsonObject());
                return;
            }
            done(true, QStringLiteral("找到 %1 个歌单").arg(results.size()),
                 QJsonObject{{QStringLiteral("playlists"), mapsToJson(results)}});
        });
    });

    m_handlers.insert(QStringLiteral("get_uploaded_music"), [this](const QJsonObject &, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchUploadedMusic([done](bool ok, int total, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取上传曲目失败（可能未登录）"), QJsonObject());
                return;
            }
            const QList<MusicInfo> tracks = tracksFromMaps(list);
            done(true, QStringLiteral("已上传 %1 首").arg(total > 0 ? total : tracks.size()),
                 QJsonObject{{QStringLiteral("total"), total},
                             {QStringLiteral("tracks"), tracksJson(tracks)}});
        });
    });

    // ── 评论 / 消息 ────────────────────────────────────
    m_handlers.insert(QStringLiteral("post_comment"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        QString content;
        if (!readNonEmptyString(arguments, QStringLiteral("content"), &content, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        const int parentId = qMax(0, arguments.value(QStringLiteral("parent_id")).toInt(0));
        m_api->postComment(musicId, content, parentId,
                           [done](bool ok, const QString &message, const QVariantMap &data) {
                               if (!ok) {
                                   done(false, message.isEmpty() ? QStringLiteral("发表评论失败") : message,
                                        QJsonObject());
                                   return;
                               }
                               done(true, QStringLiteral("评论已发表"),
                                    QJsonObject::fromVariantMap(data));
                           });
    });

    m_handlers.insert(QStringLiteral("delete_comment"), [this](const QJsonObject &arguments, Done done) {
        int commentId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("comment_id"), &commentId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->deleteComment(commentId, [commentId, done](bool ok, const QString &message, const QVariantMap &) {
            if (!ok) {
                done(false, message.isEmpty() ? QStringLiteral("删除评论失败") : message, QJsonObject());
                return;
            }
            done(true, QStringLiteral("已删除评论 %1").arg(commentId),
                 QJsonObject{{QStringLiteral("comment_id"), commentId}});
        });
    });

    m_handlers.insert(QStringLiteral("mark_notifications_read"), [this](const QJsonObject &arguments, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        QList<int> ids;
        for (const QJsonValue &value : arguments.value(QStringLiteral("ids")).toArray()) {
            const int id = value.toInt(0);
            if (id > 0)
                ids.append(id);
        }
        m_api->markNotificationsRead(ids,
                                     [ids, done](bool ok, const QString &message, const QVariantMap &data) {
                                         if (!ok) {
                                             done(false,
                                                  message.isEmpty() ? QStringLiteral("标记已读失败") : message,
                                                  QJsonObject());
                                             return;
                                         }
                                         done(true,
                                              ids.isEmpty() ? QStringLiteral("已把全部消息标记为已读")
                                                            : QStringLiteral("已标记 %1 条消息为已读").arg(ids.size()),
                                              QJsonObject::fromVariantMap(data));
                                     });
    });

    // ── 批量操作 ───────────────────────────────────────
    m_handlers.insert(QStringLiteral("batch_search_music"), [this](const QJsonObject &arguments, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        QList<ApiClient::BatchSearchItem> items;
        for (const QJsonValue &value : arguments.value(QStringLiteral("items")).toArray()) {
            const QJsonObject obj = value.toObject();
            ApiClient::BatchSearchItem item;
            item.title = obj.value(QStringLiteral("title")).toString().trimmed();
            item.artist = obj.value(QStringLiteral("artist")).toString().trimmed();
            if (!item.title.isEmpty())
                items.append(item);
        }
        if (items.isEmpty()) {
            done(false, QStringLiteral("items 为空"), QJsonObject());
            return;
        }
        m_api->batchSearchMusic(items, [done](bool ok, const ApiClient::BatchSearchResult &result) {
            if (!ok) {
                done(false, result.message.isEmpty() ? QStringLiteral("批量匹配失败") : result.message,
                     QJsonObject());
                return;
            }
            QJsonArray ids;
            for (int id : result.matchedMusicIds)
                ids.append(id);
            done(true, QStringLiteral("匹配成功 %1 首，失败 %2 首")
                           .arg(result.successCount)
                           .arg(result.failCount),
                 QJsonObject{{QStringLiteral("matched_music_ids"), ids},
                             {QStringLiteral("success_count"), result.successCount},
                             {QStringLiteral("fail_count"), result.failCount}});
        });
    });

    m_handlers.insert(QStringLiteral("batch_add_favorites"), [this](const QJsonObject &arguments, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        QList<int> ids;
        for (const QJsonValue &value : arguments.value(QStringLiteral("music_ids")).toArray()) {
            const int id = value.toInt(0);
            if (id > 0)
                ids.append(id);
        }
        if (ids.isEmpty()) {
            done(false, QStringLiteral("music_ids 为空"), QJsonObject());
            return;
        }
        m_api->batchAddFavorites(ids, [this, done](bool ok, const ApiClient::BatchAddResult &result) {
            if (ok && m_refreshFavorites)
                m_refreshFavorites();
            if (!ok) {
                done(false, result.message.isEmpty() ? QStringLiteral("批量收藏失败") : result.message,
                     QJsonObject());
                return;
            }
            done(true, QStringLiteral("已批量收藏 %1 首").arg(result.addedCount),
                 QJsonObject{{QStringLiteral("added_count"), result.addedCount}});
        });
    });

    m_handlers.insert(QStringLiteral("batch_add_to_playlist"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        QList<int> ids;
        for (const QJsonValue &value : arguments.value(QStringLiteral("music_ids")).toArray()) {
            const int id = value.toInt(0);
            if (id > 0)
                ids.append(id);
        }
        if (ids.isEmpty()) {
            done(false, QStringLiteral("music_ids 为空"), QJsonObject());
            return;
        }
        m_api->batchAddMusicToPlaylist(playlistId, ids,
                                       [playlistId, done](bool ok, const ApiClient::BatchAddResult &result) {
                                           if (!ok) {
                                               done(false,
                                                    result.message.isEmpty() ? QStringLiteral("批量加入歌单失败")
                                                                             : result.message,
                                                    QJsonObject());
                                               return;
                                           }
                                           done(true, QStringLiteral("已把 %1 首加入歌单 %2")
                                                          .arg(result.addedCount)
                                                          .arg(playlistId),
                                                QJsonObject{{QStringLiteral("playlist_id"), playlistId},
                                                            {QStringLiteral("added_count"), result.addedCount}});
                                       });
    });

    // ── 本地音乐 ───────────────────────────────────────
    m_handlers.insert(QStringLiteral("open_local_file"), [this](const QJsonObject &arguments, Done done) {
        QString path;
        QString error;
        if (!readNonEmptyString(arguments, QStringLiteral("path"), &path, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!LocalMusic::isSupportedLocalAudioFile(path)) {
            done(false, QStringLiteral("不支持的本地文件类型：%1").arg(path), QJsonObject());
            return;
        }
        if (!m_openLocalFile) {
            done(false, QStringLiteral("当前未接入本地文件播放回调"), QJsonObject());
            return;
        }
        m_openLocalFile(path);
        done(true, QStringLiteral("已打开本地文件：%1").arg(path),
             QJsonObject{{QStringLiteral("path"), path}});
    });

    m_handlers.insert(QStringLiteral("clear_recently_played"), [](const QJsonObject &, Done done) {
        PlaylistDatabase::instance().clearRecentPlays();
        done(true, QStringLiteral("已清空最近播放记录"), QJsonObject());
    });

    m_handlers.insert(QStringLiteral("resume_last_played"), [this](const QJsonObject &, Done done) {
        auto &manager = PlaylistManager::instance();
        if (!manager.hasLastPlayed()) {
            done(false, QStringLiteral("没有可恢复的上次播放记录"), QJsonObject());
            return;
        }
        if (!m_play) {
            done(false, QStringLiteral("当前未接入播放回调"), QJsonObject());
            return;
        }
        const MusicInfo info = manager.lastPlayedMusic();
        const QList<MusicInfo> &queue = manager.playlist();
        for (int i = 0; i < queue.size(); ++i) {
            if (queue.at(i).id == info.id) {
                manager.setCurrentIndex(i);
                break;
            }
        }
        m_play(info);
        done(true, QStringLiteral("已恢复上次播放：%1 - %2").arg(info.title, info.artist),
             QJsonObject{{QStringLiteral("track"), trackJson(info)}});
    });

    m_handlers.insert(QStringLiteral("update_local_playlist"), [](const QJsonObject &arguments, Done done) {
        int localId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("local_id"), &localId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        const LocalPlaylistInfo existing = PlaylistDatabase::instance().getPlaylist(localId);
        if (existing.localId <= 0) {
            done(false, QStringLiteral("本地歌单不存在：%1").arg(localId), QJsonObject());
            return;
        }
        if (!arguments.contains(QStringLiteral("name")) && !arguments.contains(QStringLiteral("description"))) {
            done(false, QStringLiteral("至少提供 name 或 description"), QJsonObject());
            return;
        }
        const QString name = arguments.contains(QStringLiteral("name"))
                                 ? arguments.value(QStringLiteral("name")).toString()
                                 : existing.name;
        const QString description = arguments.contains(QStringLiteral("description"))
                                        ? arguments.value(QStringLiteral("description")).toString()
                                        : existing.description;
        if (name.trimmed().isEmpty()) {
            done(false, QStringLiteral("歌单名称不能为空"), QJsonObject());
            return;
        }
        if (!PlaylistDatabase::instance().updatePlaylist(localId, name, description)) {
            done(false, QStringLiteral("修改本地歌单失败"), QJsonObject());
            return;
        }
        done(true, QStringLiteral("本地歌单已更新：%1").arg(name),
             QJsonObject{{QStringLiteral("local_id"), localId},
                         {QStringLiteral("name"), name},
                         {QStringLiteral("description"), description}});
    });

    // ── 下载扩展 ───────────────────────────────────────
    m_handlers.insert(QStringLiteral("download_playlist"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchPlaylistMusic(playlistId, [done](bool ok, int, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取歌单曲目失败"), QJsonObject());
                return;
            }
            const QList<MusicInfo> tracks = tracksFromMaps(list);
            const int enqueued = MusicDownloadManager::instance().enqueueAll(tracks);
            done(true, QStringLiteral("已入队 %1 首（歌单共 %2 首）").arg(enqueued).arg(tracks.size()),
                 QJsonObject{{QStringLiteral("enqueued"), enqueued},
                             {QStringLiteral("total"), tracks.size()}});
        });
    });

    m_handlers.insert(QStringLiteral("remove_download"), [](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        const bool removed = PlaylistDatabase::instance().removeDownload(musicId);
        if (!removed) {
            done(false, QStringLiteral("未找到曲目 %1 的下载记录").arg(musicId), QJsonObject());
            return;
        }
        done(true, QStringLiteral("已移除曲目 %1 的下载记录").arg(musicId),
             QJsonObject{{QStringLiteral("music_id"), musicId}});
    });

    // ── 外部歌单导入 ───────────────────────────────────
    m_handlers.insert(QStringLiteral("get_external_playlist"), [this](const QJsonObject &arguments, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        const QString source = arguments.value(QStringLiteral("source")).toString().trimmed().toLower();
        const QString playlistId = arguments.value(QStringLiteral("playlist_id")).toString().trimmed();
        if (playlistId.isEmpty()) {
            done(false, QStringLiteral("缺少参数 playlist_id"), QJsonObject());
            return;
        }
        auto finishTracks = [done, source](bool ok, const QString &message, const QString &name,
                                           int trackCount, const QList<ApiClient::NeteaseTrack> &tracks) {
            if (!ok) {
                done(false, message.isEmpty() ? QStringLiteral("获取外部歌单失败") : message, QJsonObject());
                return;
            }
            QJsonArray arr;
            for (const ApiClient::NeteaseTrack &track : tracks)
                arr.append(QJsonObject{{QStringLiteral("title"), track.name},
                                       {QStringLiteral("artist"), track.artist}});
            done(true, QStringLiteral("%1 歌单「%2」共 %3 首").arg(source, name).arg(trackCount),
                 QJsonObject{{QStringLiteral("source"), source},
                             {QStringLiteral("name"), name},
                             {QStringLiteral("track_count"), trackCount},
                             {QStringLiteral("tracks"), arr}});
        };
        if (source == QLatin1String("netease")) {
            m_api->fetchNeteasePlaylist(playlistId.toLongLong(),
                                        [finishTracks](bool ok, const QString &message,
                                                       const ApiClient::NeteasePlaylistInfo &info) {
                                            finishTracks(ok, message, info.name, info.trackCount, info.tracks);
                                        });
        } else if (source == QLatin1String("qq")) {
            m_api->fetchQqPlaylist(playlistId,
                                   [finishTracks](bool ok, const QString &message,
                                                  const ApiClient::QqPlaylistInfo &info) {
                                       finishTracks(ok, message, info.name, info.trackCount, info.tracks);
                                   });
        } else if (source == QLatin1String("kugou")) {
            m_api->fetchKugouPlaylist(playlistId,
                                      [finishTracks](bool ok, const QString &message,
                                                     const ApiClient::KugouPlaylistInfo &info) {
                                          finishTracks(ok, message, info.name, info.trackCount, info.tracks);
                                      });
        } else if (source == QLatin1String("qishui")) {
            m_api->fetchQishuiPlaylist(playlistId,
                                       [finishTracks](bool ok, const QString &message,
                                                      const ApiClient::QishuiPlaylistInfo &info) {
                                           finishTracks(ok, message, info.name, info.trackCount, info.tracks);
                                       });
        } else {
            done(false, QStringLiteral("source 仅支持 netease / qq / kugou / qishui"), QJsonObject());
        }
    });

    m_handlers.insert(QStringLiteral("start_external_import"), [this](const QJsonObject &arguments, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        const QString source = arguments.value(QStringLiteral("source")).toString().trimmed().toLower();
        static const QStringList sources{QStringLiteral("netease"), QStringLiteral("qq"),
                                         QStringLiteral("kugou"), QStringLiteral("qishui")};
        if (!sources.contains(source)) {
            done(false, QStringLiteral("source 仅支持 netease / qq / kugou / qishui"), QJsonObject());
            return;
        }
        QString externalId;
        QString error;
        if (!readNonEmptyString(arguments, QStringLiteral("external_id"), &externalId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        const int targetId = arguments.value(QStringLiteral("target_playlist_id")).toInt(0);
        const QString targetName = arguments.value(QStringLiteral("target_name")).toString().trimmed();
        if (targetId <= 0 && targetName.isEmpty()) {
            done(false, QStringLiteral("target_playlist_id 与 target_name 至少提供一个"), QJsonObject());
            return;
        }
        if (m_import.running) {
            done(false, QStringLiteral("已有导入任务进行中，可先用 get_import_status 查看进度"), QJsonObject());
            return;
        }
        m_import = ExternalImportState{};
        m_import.running = true;
        m_import.source = source;
        m_import.externalId = externalId;

        ApiClient::ExternalPullCallbacks callbacks;
        callbacks.onStart = [this](const ApiClient::ExternalPullStart &start) {
            m_import.total = start.total;
            m_import.lastMessage = QStringLiteral("开始匹配导入");
        };
        callbacks.onTrack = [this](const ApiClient::ExternalPullTrack &track) {
            m_import.index = track.index;
            m_import.total = track.total;
            m_import.lastTrack = QStringLiteral("%1 - %2").arg(track.title, track.artist);
            m_import.lastMessage = track.status;
            if (track.status == QLatin1String("imported"))
                ++m_import.imported;
            else if (track.status == QLatin1String("existed"))
                ++m_import.existed;
            else if (track.status == QLatin1String("failed"))
                ++m_import.failed;
        };
        callbacks.onDone = [this](const ApiClient::ExternalPullSummary &summary) {
            m_import.running = false;
            m_import.total = summary.total;
            m_import.imported = summary.imported;
            m_import.existed = summary.existed;
            m_import.failed = summary.failed;
        };
        callbacks.onError = [this](const QString &message) {
            m_import.running = false;
            m_import.error = message;
        };
        m_api->pullExternalPlaylist(source, externalId, qMax(0, targetId), targetName, callbacks);
        done(true, QStringLiteral("已启动 %1 歌单导入，可用 get_import_status 查询进度").arg(source),
             importStatusJson());
    });

    m_handlers.insert(QStringLiteral("get_import_status"), [this](const QJsonObject &, Done done) {
        if (m_import.source.isEmpty()) {
            done(true, QStringLiteral("暂无导入任务"), QJsonObject{{QStringLiteral("running"), false}});
            return;
        }
        done(true,
             m_import.running
                 ? QStringLiteral("导入中：%1 / %2（已入库 %3，失败 %4）")
                       .arg(m_import.index)
                       .arg(m_import.total)
                       .arg(m_import.imported)
                       .arg(m_import.failed)
                 : QStringLiteral("导入结束：入库 %1，已存在 %2，失败 %3")
                       .arg(m_import.imported)
                       .arg(m_import.existed)
                       .arg(m_import.failed),
             importStatusJson());
    });

    // ── 分享视频渲染 ───────────────────────────────────
    m_handlers.insert(QStringLiteral("create_video_render"), [this](const QJsonObject &arguments, Done done) {
        int musicId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("music_id"), &musicId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        const double startSec = qMax(0.0, arguments.value(QStringLiteral("start_sec")).toDouble(0.0));
        const bool watermarked = arguments.contains(QStringLiteral("watermarked"))
                                     ? arguments.value(QStringLiteral("watermarked")).toBool(true)
                                     : true;
        m_api->createVideoRenderJob(musicId, startSec, watermarked,
                                    [done](bool ok, const QString &message, const QVariantMap &data) {
                                        if (!ok) {
                                            done(false,
                                                 message.isEmpty() ? QStringLiteral("创建视频渲染任务失败")
                                                                   : message,
                                                 QJsonObject());
                                            return;
                                        }
                                        done(true, QStringLiteral("视频渲染任务已创建"),
                                             QJsonObject::fromVariantMap(data));
                                    });
    });

    m_handlers.insert(QStringLiteral("get_video_render_status"), [this](const QJsonObject &arguments, Done done) {
        const QString jobId = arguments.value(QStringLiteral("job_id")).toString().trimmed();
        if (jobId.isEmpty()) {
            done(false, QStringLiteral("缺少参数 job_id"), QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchVideoRenderStatus(jobId, [done](bool ok, const QVariantMap &data) {
            if (!ok) {
                done(false, QStringLiteral("查询视频渲染状态失败"), QJsonObject());
                return;
            }
            const QString status = data.value(QStringLiteral("status")).toString();
            done(true, QStringLiteral("视频渲染状态：%1").arg(status),
                 QJsonObject::fromVariantMap(data));
        });
    });

    m_handlers.insert(QStringLiteral("download_video_render"), [this](const QJsonObject &arguments, Done done) {
        const QString jobId = arguments.value(QStringLiteral("job_id")).toString().trimmed();
        if (jobId.isEmpty()) {
            done(false, QStringLiteral("缺少参数 job_id"), QJsonObject());
            return;
        }
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        QString path = arguments.value(QStringLiteral("save_path")).toString().trimmed();
        if (path.isEmpty())
            path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                   + QStringLiteral("/NekoMusic_") + jobId + QStringLiteral(".mp4");
        m_api->downloadVideoRenderFile(jobId, path, [path, done](bool ok, const QString &message) {
            if (!ok) {
                done(false, message.isEmpty() ? QStringLiteral("下载渲染视频失败") : message, QJsonObject());
                return;
            }
            done(true, QStringLiteral("视频已保存到 %1").arg(path),
                 QJsonObject{{QStringLiteral("save_path"), path}});
        });
    });

    // ── 账户 / 更新 ────────────────────────────────────
    m_handlers.insert(QStringLiteral("logout"), [](const QJsonObject &, Done done) {
        if (!UserManager::instance().isLoggedIn()) {
            done(true, QStringLiteral("当前未登录"), QJsonObject{{QStringLiteral("logged_in"), false}});
            return;
        }
        UserManager::instance().logout();
        done(true, QStringLiteral("已退出登录"), QJsonObject{{QStringLiteral("logged_in"), false}});
    });

    m_handlers.insert(QStringLiteral("get_vip_pricing"), [this](const QJsonObject &, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchVipPricing([done](bool ok, const QString &message, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, message.isEmpty() ? QStringLiteral("获取会员套餐失败") : message, QJsonObject());
                return;
            }
            done(true, QStringLiteral("共 %1 个套餐").arg(list.size()),
                 QJsonObject{{QStringLiteral("pricing"), mapsToJson(list)}});
        });
    });

    m_handlers.insert(QStringLiteral("check_update"), [this](const QJsonObject &, Done done) {
        if (!m_checkUpdate) {
            done(false, QStringLiteral("当前未接入更新检查回调"), QJsonObject());
            return;
        }
        m_checkUpdate();
        done(true, QStringLiteral("已开始检查更新，如有新版本会弹窗提示"), QJsonObject());
    });

    // ── 页面跳转 / 一键播放 ────────────────────────────
    m_handlers.insert(QStringLiteral("open_playlist_page"), [this](const QJsonObject &arguments, Done done) {
        int playlistId = 0;
        QString error;
        if (!readPositiveInt(arguments, QStringLiteral("playlist_id"), &playlistId, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_openPlaylistPage) {
            done(false, QStringLiteral("当前未接入页面跳转回调"), QJsonObject());
            return;
        }
        m_openPlaylistPage(playlistId);
        done(true, QStringLiteral("已打开歌单 %1").arg(playlistId),
             QJsonObject{{QStringLiteral("playlist_id"), playlistId}});
    });

    m_handlers.insert(QStringLiteral("open_artist_page"), [this](const QJsonObject &arguments, Done done) {
        QString query;
        QString error;
        if (!readNonEmptyString(arguments, QStringLiteral("query"), &query, &error)) {
            done(false, error, QJsonObject());
            return;
        }
        if (!m_openArtistPage) {
            done(false, QStringLiteral("当前未接入页面跳转回调"), QJsonObject());
            return;
        }
        m_openArtistPage(query);
        done(true, QStringLiteral("已打开歌手「%1」的页面").arg(query),
             QJsonObject{{QStringLiteral("query"), query}});
    });

    m_handlers.insert(QStringLiteral("play_ranking"), [this](const QJsonObject &, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchRanking([this, done](bool ok, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取排行榜失败"), QJsonObject());
                return;
            }
            playTrackList(tracksFromMaps(list), 0, done, QStringLiteral("排行榜"));
        });
    });

    m_handlers.insert(QStringLiteral("play_latest_music"), [this](const QJsonObject &arguments, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        const int limit = qBound(1, arguments.value(QStringLiteral("limit")).toInt(30), 100);
        m_api->fetchLatest(limit, [this, done](bool ok, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取最新音乐失败"), QJsonObject());
                return;
            }
            playTrackList(tracksFromMaps(list), 0, done, QStringLiteral("最新音乐"));
        });
    });

    m_handlers.insert(QStringLiteral("play_daily_recommendations"), [this](const QJsonObject &, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchDailyRecommendations([this, done](bool ok, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取每日推荐失败（可能未登录）"), QJsonObject());
                return;
            }
            playTrackList(tracksFromMaps(list), 0, done, QStringLiteral("每日推荐"));
        });
    });

    m_handlers.insert(QStringLiteral("play_favorites"), [this](const QJsonObject &, Done done) {
        if (!m_api) {
            done(false, QStringLiteral("接口不可用"), QJsonObject());
            return;
        }
        m_api->fetchFavorites([this, done](bool ok, const QList<QVariantMap> &list) {
            if (!ok) {
                done(false, QStringLiteral("获取收藏失败（可能未登录）"), QJsonObject());
                return;
            }
            playTrackList(tracksFromMaps(list), 0, done, QStringLiteral("收藏"));
        });
    });
}

QJsonObject McpBridge::importStatusJson() const
{
    return QJsonObject{
        {QStringLiteral("running"), m_import.running},
        {QStringLiteral("source"), m_import.source},
        {QStringLiteral("external_id"), m_import.externalId},
        {QStringLiteral("index"), m_import.index},
        {QStringLiteral("total"), m_import.total},
        {QStringLiteral("imported"), m_import.imported},
        {QStringLiteral("existed"), m_import.existed},
        {QStringLiteral("failed"), m_import.failed},
        {QStringLiteral("last_track"), m_import.lastTrack},
        {QStringLiteral("last_message"), m_import.lastMessage},
        {QStringLiteral("error"), m_import.error},
    };
}

void McpBridge::resolveTrack(int musicId,
                             std::function<void(bool ok, const MusicInfo &info, const QString &error)> done)
{
    const QList<MusicInfo> &queue = PlaylistManager::instance().playlist();
    for (const MusicInfo &item : queue) {
        if (item.id == musicId) {
            done(true, item, QString());
            return;
        }
    }
    if (!m_api) {
        MusicInfo info;
        info.id = musicId;
        done(true, info, QString());
        return;
    }
    m_api->fetchMusicInfo(musicId, [musicId, done](bool ok, const QVariantMap &data) {
        MusicInfo info;
        info.id = musicId;
        if (ok) {
            info.title = data.value(QStringLiteral("title")).toString();
            info.artist = data.value(QStringLiteral("artist")).toString();
            info.album = data.value(QStringLiteral("album")).toString();
            info.duration = data.value(QStringLiteral("duration")).toInt();
            info.coverUrl = data.value(QStringLiteral("coverUrl")).toString();
            if (info.coverUrl.isEmpty())
                info.coverUrl = QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(musicId);
        }
        done(true, info, QString());
    });
}

void McpBridge::runSearch(const QString &query, int limit,
                          std::function<void(bool, const QList<MusicInfo> &, const QString &)> done)
{
    if (!m_api) {
        done(false, {}, QStringLiteral("搜索接口不可用"));
        return;
    }
    m_api->searchMusic(query, 1, qBound(1, limit, 50),
                       [done, limit](bool ok, int, int, int, const QList<QVariantMap> &results) {
                           if (!ok) {
                               done(false, {}, QStringLiteral("搜索失败，请稍后重试"));
                               return;
                           }
                           // 后端忽略分页参数、一次性返回全部命中，这里按调用方要求截断。
                           QList<MusicInfo> tracks = tracksFromMaps(results);
                           if (tracks.size() > limit)
                               tracks = tracks.mid(0, limit);
                           done(true, tracks, QString());
                       });
}

void McpBridge::playTrackList(const QList<MusicInfo> &tracks, int index, const Done &done,
                              const QString &label)
{
    if (tracks.isEmpty()) {
        done(false, QStringLiteral("%1中没有可播放的曲目").arg(label), QJsonObject());
        return;
    }
    const int safeIndex = qBound(0, index, static_cast<int>(tracks.size()) - 1);
    if (m_replaceQueue) {
        m_replaceQueue(tracks, safeIndex);
    } else {
        PlaylistManager::instance().replacePlaylist(tracks, safeIndex);
        if (m_play)
            m_play(tracks.at(safeIndex));
    }
    const MusicInfo &info = tracks.at(safeIndex);
    done(true,
         QStringLiteral("开始播放%1（共 %2 首）：%3 - %4")
             .arg(label)
             .arg(tracks.size())
             .arg(info.title, info.artist),
         QJsonObject{{QStringLiteral("count"), tracks.size()},
                     {QStringLiteral("index"), safeIndex},
                     {QStringLiteral("track"), trackJson(info)}});
}

void McpBridge::applyFavorite(int musicId, bool favorite, const Done &done)
{
    if (!m_api) {
        done(false, QStringLiteral("接口不可用"), QJsonObject());
        return;
    }
    auto finish = [this, musicId, favorite, done](bool ok, const QString &message) {
        if (!ok) {
            done(false, message.isEmpty() ? QStringLiteral("收藏操作失败") : message, QJsonObject());
            return;
        }
        if (m_refreshFavorites)
            m_refreshFavorites();
        done(true,
             favorite ? QStringLiteral("已收藏曲目 %1").arg(musicId)
                      : QStringLiteral("已取消收藏曲目 %1").arg(musicId),
             QJsonObject{{QStringLiteral("music_id"), musicId},
                         {QStringLiteral("favorite"), favorite}});
    };
    if (favorite)
        m_api->addFavorite(musicId, finish);
    else
        m_api->removeFavorite(musicId, finish);
}

QJsonObject McpBridge::playbackStateJson() const
{
    const qint64 duration = m_engine->duration();
    const qint64 position = m_engine->position();
    QJsonObject state{
        {QStringLiteral("state"), stateName(m_engine->playbackState())},
        {QStringLiteral("volume"), static_cast<int>(m_engine->volume() * 100.0f + 0.5f)},
        {QStringLiteral("position_seconds"), static_cast<double>(position) / 1000.0},
        {QStringLiteral("duration_seconds"), static_cast<double>(duration) / 1000.0},
        {QStringLiteral("play_mode"), PlaylistManager::instance().playMode()},
    };
    const MusicInfo &current = m_engine->currentMusic();
    if (!current.title.isEmpty() || current.id != 0)
        state.insert(QStringLiteral("track"), trackJson(current));
    return state;
}

QJsonObject McpBridge::trackJson(const MusicInfo &info)
{
    return QJsonObject{
        {QStringLiteral("id"), info.id},
        {QStringLiteral("title"), info.title},
        {QStringLiteral("artist"), info.artist},
        {QStringLiteral("album"), info.album},
        {QStringLiteral("duration"), info.duration},
        {QStringLiteral("cover_url"), info.coverUrl},
    };
}

QJsonArray McpBridge::tracksJson(const QList<MusicInfo> &tracks)
{
    QJsonArray arr;
    for (const MusicInfo &info : tracks)
        arr.append(trackJson(info));
    return arr;
}

MusicInfo McpBridge::musicInfoFromMap(const QVariantMap &map)
{
    return infoFromMap(map);
}
