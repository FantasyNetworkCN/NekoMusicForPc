#pragma once

/**
 * @file apiclient.h
 * @brief 后端 API 客户端
 */

#include <QObject>
#include <QNetworkAccessManager>
#include <functional>

class ApiClient : public QObject
{
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);

    // ─── 音乐相关 ────────────────────────────────────
    using PlaylistsCb = std::function<void(bool, const QList<QVariantMap>&)>;
    void fetchPlaylists(const QString &query, PlaylistsCb cb);

    using MusicListCb = std::function<void(bool, const QList<QVariantMap>&)>;
    void fetchRanking(MusicListCb cb);
    void fetchLatest(int limit, MusicListCb cb);
    void fetchDailyRecommendations(MusicListCb cb);
    void fetchFavorites(MusicListCb cb);
    
    // 音乐搜索
    using MusicSearchCb = std::function<void(bool, int, int, int, const QList<QVariantMap>&)>;
    void searchMusic(const QString &query, int page = 1, int pageSize = 20, MusicSearchCb cb = nullptr);
    
    // 获取音乐信息
    using MusicInfoCb = std::function<void(bool, const QVariantMap&)>;
    void fetchMusicInfo(int musicId, MusicInfoCb cb);
    
    // 获取歌词
    using LyricsCb = std::function<void(bool, const QString&)>;
    void fetchLyrics(int musicId, LyricsCb cb);
    
    // 搜索歌手
    using ArtistSearchCb = std::function<void(bool, const QVariantMap&)>;
    void searchArtists(const QString &query, ArtistSearchCb cb);
    
    // 获取用户上传审核通过的音乐列表
    using UploadedMusicCb = std::function<void(bool, int, const QList<QVariantMap>&)>;
    void fetchUploadedMusic(UploadedMusicCb cb);
    
    // 修改用户密码
    void changePassword(const QString &oldPassword, const QString &newPassword, std::function<void(bool, const QString&)> cb);

    // ─── 歌单相关 ────────────────────────────────────
    using PlaylistDetailCb = std::function<void(bool, const QVariantMap&)>;
    void fetchPlaylistDetail(int playlistId, PlaylistDetailCb cb);
    
    using UserPlaylistsCb = std::function<void(bool, const QList<QVariantMap>&)>;
    void fetchUserPlaylists(UserPlaylistsCb cb);
    
    using CreatePlaylistCb = std::function<void(bool, const QString&, const QVariantMap&)>;
    void createPlaylist(const QString &name, const QString &description, CreatePlaylistCb cb);
    
    using UpdatePlaylistCb = std::function<void(bool, const QString&, const QVariantMap&)>;
    void updatePlaylist(int playlistId, const QString &name, const QString &description, UpdatePlaylistCb cb);
    
    using DeletePlaylistCb = std::function<void(bool, const QString&)>;
    void deletePlaylist(int playlistId, DeletePlaylistCb cb);
    
    using PlaylistMusicCb = std::function<void(bool, int, const QList<QVariantMap>&)>;
    void fetchPlaylistMusic(int playlistId, PlaylistMusicCb cb);
    
    using AddMusicToPlaylistCb = std::function<void(bool, const QString&)>;
    void addMusicToPlaylist(int playlistId, int musicId, AddMusicToPlaylistCb cb);
    
    using RemoveMusicFromPlaylistCb = std::function<void(bool, const QString&)>;
    void removeMusicFromPlaylist(int playlistId, int musicId, RemoveMusicFromPlaylistCb cb);
    
    // 收藏歌单相关
    void fetchFavoritePlaylists(UserPlaylistsCb cb);
    void favoritePlaylist(int playlistId, AddMusicToPlaylistCb cb);
    void unfavoritePlaylist(int playlistId, RemoveMusicFromPlaylistCb cb);
    void fetchFavoritePlaylistMusic(int playlistId, PlaylistMusicCb cb);

    // ─── 用户认证 ────────────────────────────────────
    using AuthCb = std::function<void(bool success, const QString &message,
                                       const QString &token, const QVariantMap &user)>;
    void login(const QString &username, const QString &password, AuthCb cb);
    void registerUser(const QString &username, const QString &password,
                      const QString &email, const QString &verificationCode, AuthCb cb);
    /** 注册发邮箱验证码前：须先完成滑块并取得 captchaPassToken */
    void sendVerificationCode(const QString &email, const QString &username,
                              const QString &captchaPassToken,
                              std::function<void(bool, const QString &)> cb);

    using SliderCaptchaChallengeCb =
        std::function<void(bool ok, const QString &message, const QVariantMap &data)>;
    void fetchSliderCaptchaChallenge(SliderCaptchaChallengeCb cb);

    using SliderCaptchaVerifyCb =
        std::function<void(bool ok, const QString &message, const QString &captchaPassToken)>;
    void verifySliderCaptcha(const QString &captchaToken, int captchaOffsetX, SliderCaptchaVerifyCb cb);

    // ─── 忘记密码 ────────────────────────────────────
    void sendResetCode(const QString &email, std::function<void(bool, const QString&)> cb);
    void resetPassword(const QString &email, const QString &verificationCode,
                       const QString &newPassword, std::function<void(bool, const QString&)> cb);

    // ─── 会员中心 ────────────────────────────────────
    using VipPricingCb = std::function<void(bool, const QString &, const QList<QVariantMap> &)>;
    void fetchVipPricing(VipPricingCb cb);

    using VipPayCreateCb = std::function<void(bool, const QString &, const QVariantMap &)>;
    void createVipPayOrder(int pricingId, const QString &payType, VipPayCreateCb cb);

    // ─── 分享视频渲染 ────────────────────────────────────
    using VipStatusCb = std::function<void(bool ok, bool isVip)>;
    void syncSessionVipStatus(VipStatusCb cb);

    using VideoRenderCreateCb = std::function<void(bool, const QString &, const QVariantMap &)>;
    void createVideoRenderJob(int musicId, double startSec, bool watermarked, VideoRenderCreateCb cb);

    using VideoRenderStatusCb = std::function<void(bool, const QVariantMap &)>;
    void fetchVideoRenderStatus(const QString &jobId, VideoRenderStatusCb cb);

    using VideoRenderDownloadCb = std::function<void(bool, const QString &)>;
    void downloadVideoRenderFile(const QString &jobId, const QString &saveFilePath, VideoRenderDownloadCb cb);

    // ─── 网易云歌单导入 ────────────────────────────────────
    struct NeteaseTrack {
        QString name;
        QString artist;
    };
    struct NeteasePlaylistInfo {
        qint64 id = 0;
        QString name;
        int trackCount = 0;
        QList<NeteaseTrack> tracks;
    };
    using NeteasePlaylistCb = std::function<void(bool ok, const QString &message, const NeteasePlaylistInfo &playlist)>;
    void fetchNeteasePlaylist(qint64 playlistId, NeteasePlaylistCb cb);

    // ─── QQ 音乐歌单导入 ────────────────────────────────────
    struct QqPlaylistInfo {
        QString disstid;
        QString name;
        int trackCount = 0;
        QList<NeteaseTrack> tracks;
    };
    using QqPlaylistCb = std::function<void(bool ok, const QString &message, const QqPlaylistInfo &playlist)>;
    void fetchQqPlaylist(const QString &disstid, QqPlaylistCb cb);

    struct BatchSearchItem {
        QString title;
        QString artist;
    };
    struct BatchSearchResult {
        bool success = false;
        QString message;
        QList<int> matchedMusicIds;
        int successCount = 0;
        int failCount = 0;
    };
    using BatchSearchCb = std::function<void(bool ok, const BatchSearchResult &result)>;
    void batchSearchMusic(const QList<BatchSearchItem> &items, BatchSearchCb cb);

    struct BatchAddResult {
        bool success = false;
        QString message;
        int addedCount = 0;
    };
    using BatchAddMusicCb = std::function<void(bool ok, const BatchAddResult &result)>;
    void batchAddMusicToPlaylist(int playlistId, const QList<int> &musicIds, BatchAddMusicCb cb);
    void batchAddFavorites(const QList<int> &musicIds, BatchAddMusicCb cb);

private:
    QNetworkAccessManager m_nam;
    QString getAuthToken() const;
};
