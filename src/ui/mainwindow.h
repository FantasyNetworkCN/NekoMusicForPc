#pragma once

/**
 * @file mainwindow.h
 * @brief 主窗口 — 日系动漫风
 *
 * 无边框窗口：
 * TitleBar(56) + Sidebar(240) | HomePage + PlayerBar(80)
 */

#include <QMainWindow>
#include <QEvent>
#include <QUrl>
#include <QList>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QPixmap>
#include <QSize>
#include <QPainter>
#include <QRect>
#include "core/musicinfo.h"
#include "theme/thememanager.h"

class QCloseEvent;
class TitleBar;
class Sidebar;
class HomePage;
class SettingsPage;
class FavoritesPage;
class RecentPage;
class PlayerBar;
class PlayerEngine;
class MusicDownloader;
class MusicListPage;
class PlayerPage;
class QMenu;
class QTimer;
class PlaylistDetailPage;
class AddToPlaylistDialog;
class PlaylistPanel;
class SearchPage;
class ArtistDetailPage;
class VipPage;
class ApiClient;
class UpdateChecker;
class UpdateDialog;
class SearchPage;
class DesktopLrc;
class SystemMediaController;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /** 资源管理器「打开方式」或命令行传入的本地音频路径（mp3/flac/wav 等） */
    void openAudioFileFromPath(const QString &path);

    void paintShellBackdrop(QPainter &p, const QRect &r) const;
    /** 供播放页模糊底图复用，避免再次 render 整窗。 */
    QPixmap shellBackdropPixmapForSize(const QSize &size);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onTrayPrevious();
    void onTrayPlayPause();
    void onTrayNext();
    void onTrayShow();
    void onTrayQuit();

private:
    void setupUi();
    void loadStyleSheet();
    void applyTheme();
    void scheduleShellBackdropRebuild(int delayMs = 120);
    void rebuildShellBackdropCache();
    void updateChromeForShellBackdrop();
    void switchPage(QWidget *target);
    void showMusicListPage(bool isHot);
    void showDailyRecommendationsPage();
    void showPlaylistDetailPage(int localId);
    void playMusicById(int musicId, const QString &title, const QString &artist, const QString &coverUrl = QString());
    void playMusicFromInfo(const MusicInfo &info);
    void playLocalMusicInfo(const MusicInfo &info);
    void createTrayIcon();
    void createPlaylist();
    void showAddToPlaylistDialog(const MusicInfo &music);
    void addToPlaylistFromPlayer(int musicId);
    void togglePlaylistPanel();
    void showPlaylistDrawer();
    void hidePlaylistDrawer();
    QWidget *playlistDrawerHost() const;
    void syncPlaylistDrawerGeometry();
    void raisePlaylistDrawerStack();
    void playMusicFromPlaylist(int musicId);
    void playNext();
    void playPrevious();
    void toggleFavorite(int musicId);
    void copyCurrentTrackShare();
    /** @param showNoUpdateToast 为 true 时表示用户从设置页手动检查，已是最新版本时弹出 Toast */
    void checkForUpdates(bool showNoUpdateToast = false);
    void refreshSystemMediaIntegration();
    void syncPlayModeUi();
    void applyDesktopLyricsEnabled(bool enabled, bool showToast = false);
    void togglePlaybackForSystemUi();
    void resumePlaybackForSystemUi();
    void pausePlaybackForSystemUi();
    void setupKeyboardShortcuts();
    void reloadKeyboardShortcuts();

    /** 打开/关闭全屏播放页（SPlayer：隐藏底栏 MainPlayer，播放页铺满窗口） */
    void openPlayerPage();
    void closePlayerPage();
    QRect playerPageOverlayGeometry() const;

private:
    void maybePromptDefaultMusicPlayer();

    bool checkIsFavorited(int musicId);
    void loadFavoritesCache();
    void syncListPageFavoriteIds();
    void disconnectDownloader();
    void cancelStreamWatch();
    /** 播放始终走 HTTP 远程 URL；并行触发本地缓存（无文件则下载，已有则下载器立即完成）。 */
    void startRemotePlaybackWithBackgroundCache(int musicId, quint64 playSeq, const QUrl &remoteUrl,
                                                bool pauseWhenReady = false);
    void startBackgroundCacheDownload(int musicId, quint64 playSeq, const QUrl &url);
    void attachStreamPlaybackGuards(int musicId, quint64 playSeq);
  /** @param midPlaybackError true=播放中途断流（Demuxing failed 等），需强制恢复 */
    void handleRemoteStreamFailure(int musicId, quint64 playSeq, bool midPlaybackError = false);

    bool m_switching = false;
    bool m_playerPageVisible = false;
    TitleBar *m_titleBar = nullptr;
    Sidebar *m_sidebar = nullptr;
    HomePage *m_homePage = nullptr;
    SettingsPage *m_settingsPage = nullptr;
    FavoritesPage *m_favoritesPage = nullptr;
    RecentPage *m_recentPage = nullptr;
    MusicListPage *m_hotMusicPage = nullptr;
    MusicListPage *m_latestMusicPage = nullptr;
    MusicListPage *m_dailyMusicPage = nullptr;
    PlayerPage *m_playerPage = nullptr;
    PlaylistDetailPage *m_playlistDetailPage = nullptr;
    SearchPage *m_searchPage = nullptr;
    ArtistDetailPage *m_artistDetailPage = nullptr;
    VipPage *m_vipPage = nullptr;
    PlaylistPanel *m_playlistPanel = nullptr;
    QWidget *m_playlistScrim = nullptr;
    PlayerBar *m_playerBar = nullptr;
    QWidget *m_midWidget = nullptr;
    QWidget *m_contentColumn = nullptr;
    QStackedWidget *m_stack = nullptr;
    PlayerEngine *m_engine = nullptr;
    MusicDownloader *m_downloader = nullptr;
    ApiClient *m_apiClient = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QList<int> m_favoritesCache;  // 缓存已收藏的音乐ID
    UpdateChecker *m_updateChecker = nullptr;
    UpdateDialog *m_updateDialog = nullptr;
    DesktopLrc *m_desktopLrc = nullptr;
    SystemMediaController *m_systemMedia = nullptr;

    // Download state
    bool m_isDownloading = false;
    /** 每次切歌递增；延后回调里若与当前不一致则丢弃，避免叠多个 singleShot 播错文件。 */
    quint64 m_enginePlaySeq = 0;

    // Downloader signal connections
    QMetaObject::Connection m_finishedConn;
    QMetaObject::Connection m_errorConn;
    QMetaObject::Connection m_bufferConn;
    QMetaObject::Connection m_progressConn;
    QMetaObject::Connection m_bgCacheFinishedConn;
    QMetaObject::Connection m_bgCacheErrorConn;
    QMetaObject::Connection m_streamPlayConn;
    QMetaObject::Connection m_streamErrorConn;
    AddToPlaylistDialog *m_addToPlaylistOverlay = nullptr;
    QTimer *m_streamAttemptTimer = nullptr;
    bool m_streamRetryActive = false;
    QUrl m_streamRemoteUrl;
    bool m_streamPauseWhenReady = false;
    int m_remoteStreamFailureCount = 0;
    /** 同一轮远程起播内只处理一次失败（避免 timeout 与 mediaError 双计）。 */
    bool m_streamFailHandledThisRound = false;
    /** 播放中途断流恢复进行中，避免 Demux 错误连发时叠多个 stop/重试。 */
    bool m_midPlaybackRecoveryInFlight = false;

    QWidget *m_shellBackdrop = nullptr;
    QTimer *m_shellBackdropRebuildTimer = nullptr;
    QPixmap m_shellBackdropCache;
    QSize m_shellBackdropCacheSize;
};
