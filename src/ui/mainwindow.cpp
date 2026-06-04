/**
 * @file mainwindow.cpp
 * @brief 主窗口实现 — SPlayer 式布局
 *
 * 侧栏全高 + 内容区顶栏 + 底栏播放器；简约扁平表面。
 */

#include "mainwindow.h"
#include "ui/titlebar.h"
#include "ui/sidebar.h"
#include "ui/homepage.h"
#include "ui/settingspage.h"
#include "ui/favoritespage.h"
#include "ui/recentpage.h"
#include "ui/playerbar.h"
#include "ui/logindialog.h"
#include "ui/musiclistpage.h"
#include "ui/playerpage.h"
#include "ui/playlistdetailpage.h"
#include "ui/searchpage.h"
#include "ui/artistdetailpage.h"
#include "ui/vippage.h"
#include "ui/addtoplaylistdialog.h"
#include "ui/neteaseimportdialog.h"
#include "ui/qqimportdialog.h"
#include "ui/playlistpanel.h"
#include "ui/toast.h"
#include "ui/updatedialog.h"
#include "ui/defaultmusicplayerdialog.h"
#include "ui/searchpage.h"
#include "ui/desktoplrc.h"
#include <QSettings>
#include <QSet>
#include "core/playerengine.h"
#include "core/i18n.h"
#include "core/apiclient.h"
#include "core/httpprotocollabel.h"
#include "core/musicdownloader.h"
#include "core/linuxtmpfscache.h"
#include "core/usermanager.h"
#include "core/playlistdb.h"
#include "core/playlistmanager.h"
#include "core/updatechecker.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/glasspaint.h"
#include "version.h"
#include "core/systemmediacontroller.h"
#include "core/localmusicmeta.h"
#include "core/defaultmusicappchecker.h"
#include "core/appshortcuts.h"
#include "core/globalshortcutcontroller.h"
#include "core/shellbackdropsettings.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QWindow>
#include <QPainter>
#include <QList>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <functional>

namespace {

/** 抽屉遮罩：高斯模糊 + 轻微压暗，淡入淡出（对齐 SPlayer backdrop-filter） */
class PlaylistDrawerScrim final : public QWidget {
public:
    explicit PlaylistDrawerScrim(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_StyledBackground, true);
        setCursor(Qt::ArrowCursor);
        m_opacityFx = new QGraphicsOpacityEffect(this);
        m_opacityFx->setOpacity(0.0);
        setGraphicsEffect(m_opacityFx);
        hide();
    }

    std::function<void()> onClicked;

    void setFullPlayerMode(bool on)
    {
        if (m_fullPlayerMode == on)
            return;
        m_fullPlayerMode = on;
        update();
    }

    void refreshBackdrop(QWidget *host, QWidget *drawerPanel)
    {
        if (!host)
            return;
        QList<QWidget *> exclude;
        exclude.append(this);
        if (drawerPanel)
            exclude.append(drawerPanel);
        const qreal blurRadius = m_fullPlayerMode ? 80.0 : 48.0;
        m_blurPixmap = GlassPaint::grabBlurredBackdrop(host, exclude, blurRadius);
        update();
    }

    void fadeIn(int durationMs = Theme::kAnimNormal)
    {
        stopFadeAnim();
        m_opacityFx->setOpacity(0.0);
        show();
        m_fadeAnim = new QPropertyAnimation(m_opacityFx, "opacity", this);
        m_fadeAnim->setDuration(durationMs);
        m_fadeAnim->setStartValue(0.0);
        m_fadeAnim->setEndValue(1.0);
        m_fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() { m_fadeAnim = nullptr; });
        m_fadeAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void fadeOut(int durationMs = Theme::kAnimNormal)
    {
        stopFadeAnim();
        if (!isVisible()) {
            m_opacityFx->setOpacity(0.0);
            return;
        }
        m_fadeAnim = new QPropertyAnimation(m_opacityFx, "opacity", this);
        m_fadeAnim->setDuration(durationMs);
        m_fadeAnim->setStartValue(m_opacityFx->opacity());
        m_fadeAnim->setEndValue(0.0);
        m_fadeAnim->setEasingCurve(QEasingCurve::InCubic);
        connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
            m_fadeAnim = nullptr;
            hide();
            m_opacityFx->setOpacity(0.0);
        });
        m_fadeAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (!m_blurPixmap.isNull())
            p.drawPixmap(rect(), m_blurPixmap);
        else if (!m_fullPlayerMode)
            p.fillRect(rect(), QColor(36, 36, 36, 220));
        // 主界面：blur + 轻压暗；播放页：仅 blur（SPlayer .full-player background transparent）
        if (!m_fullPlayerMode)
            p.fillRect(rect(), QColor(0, 0, 0, 72));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (onClicked)
            onClicked();
        event->accept();
    }

private:
    void stopFadeAnim()
    {
        if (!m_fadeAnim)
            return;
        m_fadeAnim->stop();
        m_fadeAnim->deleteLater();
        m_fadeAnim = nullptr;
    }

    QPixmap m_blurPixmap;
    bool m_fullPlayerMode = false;
    QGraphicsOpacityEffect *m_opacityFx = nullptr;
    QPropertyAnimation *m_fadeAnim = nullptr;
};

/** 铺满 centralWidget 的整窗底图（含首页） */
class AppShellBackdrop final : public QWidget {
public:
    explicit AppShellBackdrop(MainWindow *main, QWidget *parent)
        : QWidget(parent)
        , m_main(main)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!m_main)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        m_main->paintShellBackdrop(p, rect());
    }

private:
    MainWindow *m_main = nullptr;
};

constexpr int kFramelessResizeMargin = 8;

Qt::Edges framelessResizeEdgesAt(const QWidget *win, const QPoint &globalPos)
{
    if (!win || win->isMaximized())
        return {};
    const QRect g = win->frameGeometry();
    Qt::Edges edges;
    if (globalPos.x() - g.left() < kFramelessResizeMargin)
        edges |= Qt::LeftEdge;
    if (g.right() - globalPos.x() < kFramelessResizeMargin)
        edges |= Qt::RightEdge;
    if (globalPos.y() - g.top() < kFramelessResizeMargin)
        edges |= Qt::TopEdge;
    if (g.bottom() - globalPos.y() < kFramelessResizeMargin)
        edges |= Qt::BottomEdge;
    return edges;
}

Qt::CursorShape cursorForResizeEdges(Qt::Edges edges)
{
    const bool left = edges.testFlag(Qt::LeftEdge);
    const bool right = edges.testFlag(Qt::RightEdge);
    const bool top = edges.testFlag(Qt::TopEdge);
    const bool bottom = edges.testFlag(Qt::BottomEdge);
    if ((left && top) || (right && bottom))
        return Qt::SizeFDiagCursor;
    if ((right && top) || (left && bottom))
        return Qt::SizeBDiagCursor;
    if (left || right)
        return Qt::SizeHorCursor;
    if (top || bottom)
        return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

} // namespace
#include <QPointer>
#include <QDebug>
#include "ui/lineinputdialog.h"
#include <QTimer>
#include <memory>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QCloseEvent>
#include <QFileOpenEvent>
#include <QAction>
#include <QUrl>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCheckBox>
#include <QAbstractButton>

namespace {
/** 单次远程起播：超过此时长仍未进入 Playing 则计一次失败并重试（最多 3 次）。 */
constexpr int kStreamReadyTimeoutMs = 12000;
constexpr int kStreamRetryDelayMs = 350;
/** 起播稳定后再后台缓存，避免与 QMediaPlayer 并行拉同一 URL 触发服务端断流 */
constexpr int kBgCacheDelayMs = 2500;
/** 断流后若 .part 已达此大小则改播本地部分文件 */
constexpr qint64 kMinPartResumeBytes = 512 * 1024;

bool isRecoverablePlaybackError(const QString &err)
{
    const QString e = err.toLower();
    return e.contains(QStringLiteral("demux"))
        || e.contains(QStringLiteral("failed"))
        || e.contains(QStringLiteral("input/output"))
        || e.contains(QStringLiteral("resource error"))
        || e.contains(QStringLiteral("network"));
}

QString buildShareClipboardText(const MusicInfo &m)
{
    I18n &i18n = I18n::instance();
    const QString title = m.title.isEmpty() ? i18n.tr(QStringLiteral("unknown")) : m.title;
    const QString artist = m.artist.isEmpty() ? i18n.tr(QStringLiteral("unknown")) : m.artist;
    const QString link = m.isLocalFile()
        ? QUrl::fromLocalFile(m.localPath).toString()
        : QStringLiteral("%1/detail/%2").arg(QString::fromUtf8(Theme::kApiBase)).arg(m.id);

    QString t;
    if (m.isLocalFile())
        t += i18n.tr(QStringLiteral("shareClipboardHeaderLocal"));
    else
        t += i18n.tr(QStringLiteral("shareClipboardHeaderOnline"));
    t += QStringLiteral("\n\n");
    t += i18n.tr(QStringLiteral("shareClipboardTrack")).arg(title);
    t += QStringLiteral("\n");
    t += i18n.tr(QStringLiteral("shareClipboardArtist")).arg(artist);
    t += QStringLiteral("\n");
    if (!m.album.isEmpty()) {
        t += i18n.tr(QStringLiteral("shareClipboardAlbum")).arg(m.album);
        t += QStringLiteral("\n");
    }
    t += QStringLiteral("\n");
    if (m.isLocalFile())
        t += i18n.tr(QStringLiteral("shareClipboardPromoLocal"));
    else
        t += i18n.tr(QStringLiteral("shareClipboardPromo"));
    t += QStringLiteral("\n\n");
    t += (m.isLocalFile() ? i18n.tr(QStringLiteral("shareClipboardLinkLocal"))
                          : i18n.tr(QStringLiteral("shareClipboardLinkOnline")));
    t += QStringLiteral("\n");
    t += link;
    t += QStringLiteral("\n");
    return t;
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAutoFillBackground(false);

    setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    m_engine = new PlayerEngine(this);
    m_downloader = &MusicDownloader::instance();
    setupUi();
    loadStyleSheet();
    AppShortcuts::instance().load();
    GlobalShortcutController::instance().installFallback(this);
    setupKeyboardShortcuts();
    createTrayIcon();

    m_systemMedia = new SystemMediaController(this);
    m_systemMedia->setHostWindow(this);
    m_systemMedia->setPlayerEngine(m_engine);
    connect(m_systemMedia, &SystemMediaController::raiseRequested, this, &MainWindow::onTrayShow);
    connect(m_systemMedia, &SystemMediaController::quitRequested, qApp, &QApplication::quit);
    connect(m_systemMedia, &SystemMediaController::nextRequested, this, &MainWindow::playNext);
    connect(m_systemMedia, &SystemMediaController::previousRequested, this, &MainWindow::playPrevious);
    connect(m_systemMedia, &SystemMediaController::playPauseRequested, this, &MainWindow::togglePlaybackForSystemUi);
    connect(m_systemMedia, &SystemMediaController::playRequested, this, &MainWindow::resumePlaybackForSystemUi);
    connect(m_systemMedia, &SystemMediaController::pauseRequested, this, &MainWindow::pausePlaybackForSystemUi);
    connect(m_systemMedia, &SystemMediaController::stopRequested, this, [this]() {
        if (m_engine)
            m_engine->stop();
    });
    connect(m_systemMedia, &SystemMediaController::seekRelativeUs, this, [this](qint64 us) {
        if (!m_engine)
            return;
        qint64 ms = m_engine->position() + us / 1000;
        if (ms < 0)
            ms = 0;
        m_engine->setPosition(ms);
        if (m_systemMedia)
            m_systemMedia->notifySeeked(ms * 1000);
    });
    connect(m_systemMedia, &SystemMediaController::seekAbsoluteUs, this, [this](qint64 us) {
        if (!m_engine)
            return;
        qint64 ms = us / 1000;
        if (ms < 0)
            ms = 0;
        m_engine->setPosition(ms);
        if (m_systemMedia)
            m_systemMedia->notifySeeked(ms * 1000);
    });
    connect(m_systemMedia, &SystemMediaController::loopStatusSetRequested, this, [this](const QString &status) {
        auto &mgr = PlaylistManager::instance();
        if (status == QStringLiteral("Track"))
            mgr.setPlayMode(QStringLiteral("single"));
        else if (status == QStringLiteral("Playlist"))
            mgr.setPlayMode(QStringLiteral("list"));
        else
            mgr.setPlayMode(mgr.playMode() == QStringLiteral("random") ? QStringLiteral("list")
                                                                       : mgr.playMode());
        syncPlayModeUi();
        refreshSystemMediaIntegration();
    });
    connect(m_systemMedia, &SystemMediaController::shuffleSetRequested, this, [this](bool shuffle) {
        auto &mgr = PlaylistManager::instance();
        if (shuffle)
            mgr.setPlayMode(QStringLiteral("random"));
        else if (mgr.playMode() == QStringLiteral("random"))
            mgr.setPlayMode(QStringLiteral("list"));
        syncPlayModeUi();
        refreshSystemMediaIntegration();
    });
    connect(m_systemMedia, &SystemMediaController::volumeSetByOs, this, [this](double v) {
        if (m_engine)
            m_engine->setVolume(static_cast<float>(v));
        if (m_playerBar)
            m_playerBar->setVolumePercentSynced(qBound(0, static_cast<int>(qRound(v * 100.0)), 100));
        if (m_playerPage)
            m_playerPage->setVolumePercentSynced(qBound(0, static_cast<int>(qRound(v * 100.0)), 100));
    });
    connect(m_engine, &PlayerEngine::stateChanged, this, &MainWindow::refreshSystemMediaIntegration);
    connect(m_engine, &PlayerEngine::mediaPlaybackStateChanged, this, &MainWindow::refreshSystemMediaIntegration);
    connect(m_engine, &PlayerEngine::fadeComplete, this, &MainWindow::refreshSystemMediaIntegration);
    connect(m_engine, &PlayerEngine::durationChanged, this, &MainWindow::refreshSystemMediaIntegration);
    connect(m_engine, &PlayerEngine::positionChanged, m_systemMedia, &SystemMediaController::onPositionMsChanged);
    connect(&PlaylistManager::instance(), &PlaylistManager::playlistChanged, this, &MainWindow::refreshSystemMediaIntegration);
    connect(&PlaylistManager::instance(), &PlaylistManager::playModeChanged, this, [this](const QString &) {
        refreshSystemMediaIntegration();
        syncPlayModeUi();
    });
    connect(m_playerBar, &PlayerBar::volumePercentChanged, this, [this](int p) {
        if (m_systemMedia)
            m_systemMedia->syncVolumeFromEngine(p / 100.0);
    });
    refreshSystemMediaIntegration();

    // 连接主题变化信号
    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged,
            this, &MainWindow::applyTheme);
    m_shellBackdropRebuildTimer = new QTimer(this);
    m_shellBackdropRebuildTimer->setSingleShot(true);
    connect(m_shellBackdropRebuildTimer, &QTimer::timeout, this, &MainWindow::rebuildShellBackdropCache);
    connect(&ShellBackdropSettings::instance(), &ShellBackdropSettings::changed, this, [this]() {
        m_shellBackdropCache = QPixmap();
        m_shellBackdropCacheSize = QSize();
        scheduleShellBackdropRebuild(0);
        updateChromeForShellBackdrop();
    });

    setWindowTitle(QStringLiteral("NekoMusic"));
    resize(1200, 800);
    setMinimumSize(960, 640);
    scheduleShellBackdropRebuild(0);

    qApp->installEventFilter(this);

    // 延迟检查版本更新
    QTimer::singleShot(2000, this, [this]() { checkForUpdates(false); });
    QTimer::singleShot(3500, this, [this]() { maybePromptDefaultMusicPlayer(); });
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    while (QApplication::overrideCursor())
        QApplication::restoreOverrideCursor();

    // 清理下载器连接
    disconnectDownloader();

    if (m_desktopLrc) {
        m_desktopLrc->hide();
        delete m_desktopLrc;
        m_desktopLrc = nullptr;
    }

    // 清理托盘图标
    if (m_trayIcon) {
        m_trayIcon->hide();
        delete m_trayIcon;
    }
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    central->setObjectName("centralWidget");
    central->setAutoFillBackground(false);
    setCentralWidget(central);

    m_shellBackdrop = new AppShellBackdrop(this, central);
    m_shellBackdrop->setObjectName(QStringLiteral("shellBackdrop"));
    m_shellBackdrop->lower();

    auto *mainV = new QVBoxLayout(central);
    mainV->setContentsMargins(0, 0, 0, 0);
    mainV->setSpacing(0);

    // 中间区域：侧栏（全高）+ 内容列（顶栏 + 页面）
    m_midWidget = new QWidget(this);
    m_midWidget->setObjectName("midWidget");
    m_midWidget->setAttribute(Qt::WA_StyledBackground, true);
    auto *midH = new QHBoxLayout(m_midWidget);
    midH->setContentsMargins(0, 0, 0, 0);
    midH->setSpacing(0);

    m_apiClient = new ApiClient(this);
    m_sidebar = new Sidebar(m_apiClient, this);
    midH->addWidget(m_sidebar);

    m_contentColumn = new QWidget(m_midWidget);
    m_contentColumn->setObjectName("contentColumn");
    auto *contentCol = m_contentColumn;
    auto *contentV = new QVBoxLayout(contentCol);
    contentV->setContentsMargins(0, 0, 0, 0);
    contentV->setSpacing(0);

    m_titleBar = new TitleBar(contentCol);
    contentV->addWidget(m_titleBar);

    m_stack = new QStackedWidget(contentCol);
    m_stack->setObjectName("pageStack");
    m_homePage = new HomePage(this);
    m_settingsPage = new SettingsPage(this);
    m_favoritesPage = new FavoritesPage(m_apiClient, this);
    m_recentPage = new RecentPage(this);
    m_hotMusicPage = new MusicListPage(MusicListPage::Hot, this);
    m_latestMusicPage = new MusicListPage(MusicListPage::Latest, this);
    m_dailyMusicPage = new MusicListPage(MusicListPage::Daily, this);
    m_playlistDetailPage = new PlaylistDetailPage(m_apiClient, this);
    m_searchPage = new SearchPage(m_apiClient, this);
    m_artistDetailPage = new ArtistDetailPage(this);
    m_vipPage = new VipPage(m_apiClient, this);
    m_stack->addWidget(m_homePage);
    m_stack->addWidget(m_settingsPage);
    m_stack->addWidget(m_favoritesPage);
    m_stack->addWidget(m_recentPage);
    m_stack->addWidget(m_hotMusicPage);
    m_stack->addWidget(m_latestMusicPage);
    m_stack->addWidget(m_dailyMusicPage);
    m_stack->addWidget(m_playlistDetailPage);
    m_stack->addWidget(m_searchPage);
    m_stack->addWidget(m_artistDetailPage);
    m_stack->addWidget(m_vipPage);
    contentV->addWidget(m_stack, 1);
    midH->addWidget(contentCol, 1);

    mainV->addWidget(m_midWidget, 1);

    // 播放栏（横跨整个窗口底部）
    m_playerBar = new PlayerBar(m_engine, this);
    mainV->addWidget(m_playerBar);

    // 播放页面 — 全屏覆盖层（覆盖侧边栏和标题栏，不覆盖播放栏）
    m_playerPage = new PlayerPage(m_engine, m_apiClient, m_midWidget);
    m_playerPage->hide();

    // 播放队列抽屉：贴窗口右缘滑入，层级盖住底栏播放器（对齐 SPlayer n-drawer）
    m_playlistPanel = new PlaylistPanel(central);
    m_playlistScrim = new PlaylistDrawerScrim(central);
    static_cast<PlaylistDrawerScrim *>(m_playlistScrim)->onClicked = [this]() { hidePlaylistDrawer(); };
    connect(m_playlistPanel, &PlaylistPanel::hideRequested, this, &MainWindow::hidePlaylistDrawer);
    connect(m_playlistPanel, &PlaylistPanel::drawerClosed, this, [this]() {
        if (m_playerBar)
            m_playerBar->setFloatingProgressSuppressed(false);
    });

    // 加载播放队列
    PlaylistManager::instance().load();

#ifdef Q_OS_LINUX
    LinuxTmpfsCache::runStartupMaintenance();
#endif
    MusicDownloader::purgeLegacyMd5CacheFiles();

    // 恢复上次播放的音乐
    if (PlaylistManager::instance().hasLastPlayed()) {
        auto lastMusic = PlaylistManager::instance().lastPlayedMusic();
        m_playerBar->setCurrentMusicId(lastMusic.id);
        m_playerBar->setSongInfo(lastMusic.title, lastMusic.artist, lastMusic.coverUrl);
        m_playerPage->setMusicInfo(lastMusic.id, lastMusic.title, lastMusic.artist, lastMusic.album, lastMusic.coverUrl);
        m_playerPage->loadLyricsForTrack(lastMusic);
        m_engine->setCurrentMusic(lastMusic);

        // 检查收藏状态（loadFavoritesCache 会在之后异步更新，但这里先设置初始状态）
        bool isFavorited = checkIsFavorited(lastMusic.id);
        m_playerBar->setFavoriteStatus(isFavorited);
        if (m_playerPage)
            m_playerPage->setFavoriteStatus(isFavorited);

        // 与点歌一致：远程起播 + 并行写缓存；外部文件则直接本地起播；起播后暂停等待用户操作
        disconnectDownloader();
        const quint64 restoreSeq = m_enginePlaySeq;
        m_playerBar->setLoading(true);
        if (lastMusic.isLocalFile()) {
            QTimer::singleShot(0, this, [this, lastMusic, restoreSeq]() {
                if (restoreSeq != m_enginePlaySeq)
                    return;
                cancelStreamWatch();
                m_streamRetryActive = false;
                m_playerBar->setLoading(false);
                m_engine->play(QUrl::fromLocalFile(lastMusic.localPath));
                QTimer::singleShot(80, this, [this]() { m_engine->pause(); });
            });
        } else {
            const QUrl url(QString::fromUtf8("%1/api/music/file/%2")
                               .arg(QString::fromUtf8(Theme::kApiBase))
                               .arg(lastMusic.id));
            // 略延迟远程起播：窗口 show 与 PipeWire/FFmpeg 设备就绪后再拉流，降低偶发 SIGSEGV 风险
            QTimer::singleShot(120, this, [this, lastMusic, restoreSeq, url]() {
                if (restoreSeq != m_enginePlaySeq)
                    return;
                startRemotePlaybackWithBackgroundCache(lastMusic.id, restoreSeq, url, true);
            });
        }
    }

    // 连接导航
    connect(m_sidebar, &Sidebar::navigationRequested, this, [this](const QString &key) {
        if (key == "home") switchPage(m_homePage);
        else if (key == "favorites") {
            m_favoritesPage->refresh();
            switchPage(m_favoritesPage);
        }
        else if (key == "recent") {
            syncListPageFavoriteIds();
            m_recentPage->refresh();
            switchPage(m_recentPage);
        }
        else if (key == "search") switchPage(m_searchPage);
    });
    connect(m_favoritesPage, &FavoritesPage::playRequested, this, &MainWindow::playMusicById);
    connect(m_favoritesPage, &FavoritesPage::unfavoriteRequested, this, &MainWindow::toggleFavorite);
    connect(m_favoritesPage, &FavoritesPage::playPauseRequested, this, &MainWindow::togglePlaybackForSystemUi);
    connect(m_favoritesPage, &FavoritesPage::playAllRequested, this, [this](const QList<MusicInfo> &results) {
        PlaylistManager::instance().clearPlaylist();
        PlaylistManager::instance().addAllToPlaylist(results);
        if (!results.isEmpty()) {
            const auto &first = results.first();
            playMusicById(first.id, first.title, first.artist, first.coverUrl);
        }
    });
    connect(&UserManager::instance(), &UserManager::loginStateChanged, this, [this]() {
        if (m_favoritesPage) m_favoritesPage->refresh();
        loadFavoritesCache();
        m_sidebar->loadPlaylists();
    });
    // 启动时可能错过了信号，手动检查一次
    if (UserManager::instance().isLoggedIn()) {
        loadFavoritesCache();
        m_sidebar->loadPlaylists();
    }
    connect(m_recentPage, &RecentPage::playRequested, this, &MainWindow::playMusicFromInfo);
    connect(m_recentPage, &RecentPage::playAllRequested, this, [this](const QList<MusicInfo> &results) {
        PlaylistManager::instance().clearPlaylist();
        PlaylistManager::instance().addAllToPlaylist(results);
        if (!results.isEmpty())
            playMusicFromInfo(results.first());
    });
    connect(m_recentPage, &RecentPage::favoriteRequested, this, &MainWindow::toggleFavorite);
    connect(m_recentPage, &RecentPage::playPauseRequested, this, &MainWindow::togglePlaybackForSystemUi);
    connect(m_sidebar, &Sidebar::playlistClicked, this, &MainWindow::showPlaylistDetailPage);
    connect(m_sidebar, &Sidebar::playlistCreateRequested, this, &MainWindow::createPlaylist);
    connect(m_sidebar, &Sidebar::neteaseImportRequested, this, [this]() {
        if (!UserManager::instance().isLoggedIn()) {
            Toast::show(this, I18n::instance().tr("loginRequired"), Toast::Error);
            return;
        }
        auto *dlg = new NeteaseImportDialog(m_apiClient, this);
        connect(dlg, &NeteaseImportDialog::importCompleted, this,
                [this](int addedCount, int totalCount, int failCount, bool importedToFavorites) {
            m_sidebar->refreshPlaylists();
            if (importedToFavorites && m_favoritesPage)
                m_favoritesPage->refresh();
            Toast::show(this,
                        I18n::instance().tr(QStringLiteral("importSuccess"))
                            .arg(addedCount)
                            .arg(totalCount)
                            .arg(failCount),
                        Toast::Success);
        });
        dlg->exec();
        dlg->deleteLater();
    });
    connect(m_sidebar, &Sidebar::qqImportRequested, this, [this]() {
        if (!UserManager::instance().isLoggedIn()) {
            Toast::show(this, I18n::instance().tr("loginRequired"), Toast::Error);
            return;
        }
        auto *dlg = new QqImportDialog(m_apiClient, this);
        connect(dlg, &QqImportDialog::importCompleted, this,
                [this](int addedCount, int totalCount, int failCount, bool importedToFavorites) {
            m_sidebar->refreshPlaylists();
            if (importedToFavorites && m_favoritesPage)
                m_favoritesPage->refresh();
            Toast::show(this,
                        I18n::instance().tr(QStringLiteral("importSuccess"))
                            .arg(addedCount)
                            .arg(totalCount)
                            .arg(failCount),
                        Toast::Success);
        });
        dlg->exec();
        dlg->deleteLater();
    });
    connect(m_titleBar, &TitleBar::settingsClicked, this, [this]() {
        switchPage(m_settingsPage);
    });
    connect(m_titleBar, &TitleBar::vipClicked, this, [this]() {
        if (!UserManager::instance().isLoggedIn())
            return;
        m_vipPage->refresh();
        switchPage(m_vipPage);
    });
    connect(m_settingsPage, &SettingsPage::languageChanged, m_homePage, &HomePage::retranslate);
    connect(m_settingsPage, &SettingsPage::languageChanged, m_sidebar, &Sidebar::retranslate);
    connect(m_settingsPage, &SettingsPage::languageChanged, m_titleBar, &TitleBar::retranslate);
    connect(m_settingsPage, &SettingsPage::languageChanged, m_playerBar, &PlayerBar::retranslate);
    connect(m_settingsPage, &SettingsPage::languageChanged, m_playerPage, &PlayerPage::retranslate);
    connect(m_settingsPage, &SettingsPage::languageChanged, m_favoritesPage, &FavoritesPage::retranslate);
    connect(m_settingsPage, &SettingsPage::languageChanged, m_recentPage, &RecentPage::retranslate);

    // 音乐加载器连接 — 由各播放方法按需单独连接

    // QMediaPlayer 缓冲状态监控
    connect(m_engine, &PlayerEngine::stateChanged, this, [this](PlayerEngine::PlaybackState state) {
        if (state == PlayerEngine::Playing) {
            m_playerBar->setLoading(false);
        }
        if (m_favoritesPage) {
            m_favoritesPage->setPlaybackPaused(state != PlayerEngine::Playing);
        }
        if (m_playlistDetailPage) {
            m_playlistDetailPage->setPlaybackPaused(state != PlayerEngine::Playing);
        }
        if (m_recentPage) {
            m_recentPage->setPlaybackPaused(state != PlayerEngine::Playing);
        }
        if (m_searchPage) {
            m_searchPage->setPlaybackPaused(state != PlayerEngine::Playing);
        }
        if (m_artistDetailPage) {
            m_artistDetailPage->setPlaybackPaused(state != PlayerEngine::Playing);
        }
        if (m_hotMusicPage) {
            m_hotMusicPage->setPlaybackPaused(state != PlayerEngine::Playing);
        }
        if (m_latestMusicPage) {
            m_latestMusicPage->setPlaybackPaused(state != PlayerEngine::Playing);
        }
        if (m_dailyMusicPage) {
            m_dailyMusicPage->setPlaybackPaused(state != PlayerEngine::Playing);
        }
    });

    // 记录最近播放，并刷新列表页正在播放高亮
    connect(m_engine, &PlayerEngine::musicStarted, this, [this](const MusicInfo &music) {
        PlaylistDatabase::instance().recordRecentPlay(music);
        if (m_favoritesPage)
            m_favoritesPage->updatePlayingHighlight();
        if (m_playlistDetailPage)
            m_playlistDetailPage->updatePlayingHighlight();
        if (m_recentPage)
            m_recentPage->updatePlayingHighlight();
        if (m_searchPage)
            m_searchPage->updatePlayingHighlight();
        if (m_artistDetailPage)
            m_artistDetailPage->updatePlayingHighlight();
        if (m_hotMusicPage)
            m_hotMusicPage->updatePlayingHighlight();
        if (m_latestMusicPage)
            m_latestMusicPage->updatePlayingHighlight();
        if (m_dailyMusicPage)
            m_dailyMusicPage->updatePlayingHighlight();
    });

    // 起播阶段由 attachStreamPlaybackGuards 处理；起播成功后断流/Demux 失败由此恢复
    connect(m_engine, &PlayerEngine::mediaError, this, [this](const QString &err) {
        qDebug() << "[播放错误]" << err;
        if (m_streamRetryActive)
            return;
        if (!isRecoverablePlaybackError(err)) {
            m_playerBar->setLoading(false);
            return;
        }
        const int musicId = m_playerBar->currentMusicId();
        if (musicId <= 0 || m_engine->currentMusic().isLocalFile()) {
            m_playerBar->setLoading(false);
            return;
        }
        qDebug() << "[Music] 播放中途错误，尝试恢复 id=" << musicId << err;
        handleRemoteStreamFailure(musicId, m_enginePlaySeq, true);
    });

    // 播放完成自动切歌
    connect(m_engine, &PlayerEngine::playbackFinished, this, [this]() {
        auto& manager = PlaylistManager::instance();
        if (manager.count() == 0) return;

        const QString mode = manager.playMode();
        qDebug() << "[播放完成] 当前模式:" << mode;

        if (mode == "single") {
            // 单曲循环：重新播放当前歌曲
            qDebug() << "[单曲循环] 重新播放:" << manager.playlist()[manager.currentIndex()].title;
        }
        // 所有模式都调用 playNext()：
        // - single: nextIndex() 返回相同索引，重新播放当前歌曲
        // - list:   nextIndex() 返回下一首，列表循环
        // - random: nextIndex() 返回随机不同索引，随机播放
        playNext();
    });

    // 头像点击 - 显示登录/登出菜单
    connect(m_titleBar, &TitleBar::avatarClicked, this, [this]() {
        if (UserManager::instance().isLoggedIn()) {
            // 已登录，弹出用户菜单
            QMenu *menu = new QMenu(this);
            menu->setAttribute(Qt::WA_DeleteOnClose);

            QString username = UserManager::instance().userInfo().value("username").toString();
            if (username.isEmpty()) username = "User";
            menu->addAction(username)->setEnabled(false);
            menu->addSeparator();

            auto *favAction = menu->addAction(tr("My Favorites"));
            connect(favAction, &QAction::triggered, this, [this]() {
                switchPage(m_favoritesPage);
            });

            auto *vipAction = menu->addAction(I18n::instance().tr(QStringLiteral("vipNav")));
            connect(vipAction, &QAction::triggered, this, [this]() {
                m_vipPage->refresh();
                switchPage(m_vipPage);
            });

            menu->addSeparator();
            auto *logoutAction = menu->addAction(tr("Logout"));
            connect(logoutAction, &QAction::triggered, this, [this, menu]() {
                UserManager::instance().logout();
                menu->close();
            });

            menu->popup(m_titleBar->avatarPos());
        } else {
            // 未登录，显示登录对话框
            LoginDialog dlg(this);
            dlg.exec();
        }
    });

    // 音乐列表页面导航
    connect(m_homePage, &HomePage::navigateToMusicList, this, &MainWindow::showMusicListPage);
    connect(m_homePage, &HomePage::navigateToDailyRecommendations, this, &MainWindow::showDailyRecommendationsPage);

    // 音乐列表页面返回
    connect(m_hotMusicPage, &MusicListPage::backRequested, this, [this]() {
        switchPage(m_homePage);
    });
    connect(m_latestMusicPage, &MusicListPage::backRequested, this, [this]() {
        switchPage(m_homePage);
    });
    connect(m_dailyMusicPage, &MusicListPage::backRequested, this, [this]() {
        switchPage(m_homePage);
    });

    // 搜索请求
    connect(m_titleBar, &TitleBar::searchRequested, this, [this](const QString &query) {
        syncListPageFavoriteIds();
        m_searchPage->search(query);
        switchPage(m_searchPage);
    });

    connect(m_searchPage, &SearchPage::playMusic, this, [this](const MusicInfo &info) {
        playMusicFromInfo(info);
    });
    connect(m_searchPage, &SearchPage::favoriteRequested, this, &MainWindow::toggleFavorite);
    connect(m_searchPage, &SearchPage::playPauseRequested, this,
            &MainWindow::togglePlaybackForSystemUi);
    connect(m_searchPage, &SearchPage::openPlaylist, this, [this](int playlistId) {
        m_playlistDetailPage->loadPlaylist(playlistId);
        switchPage(m_playlistDetailPage);
    });
    connect(m_searchPage, &SearchPage::openArtist, this, [this](const QVariantMap &artist) {
        syncListPageFavoriteIds();
        m_artistDetailPage->loadArtist(artist);
        switchPage(m_artistDetailPage);
    });
    connect(m_artistDetailPage, &ArtistDetailPage::backRequested, this, [this]() {
        switchPage(m_searchPage);
    });
    connect(m_artistDetailPage, &ArtistDetailPage::playMusic, this, [this](const MusicInfo &info) {
        playMusicFromInfo(info);
    });
    connect(m_artistDetailPage, &ArtistDetailPage::playAllRequested, this,
            [this](const QList<MusicInfo> &songs) {
                if (songs.isEmpty())
                    return;
                PlaylistManager::instance().clearPlaylist();
                PlaylistManager::instance().addAllToPlaylist(songs);
                playMusicFromInfo(songs.first());
            });
    connect(m_artistDetailPage, &ArtistDetailPage::favoriteRequested, this,
            &MainWindow::toggleFavorite);
    connect(m_artistDetailPage, &ArtistDetailPage::playPauseRequested, this,
            &MainWindow::togglePlaybackForSystemUi);
    connect(m_hotMusicPage, &MusicListPage::playMusic, this, &MainWindow::playMusicFromInfo);
    connect(m_latestMusicPage, &MusicListPage::playMusic, this, &MainWindow::playMusicFromInfo);
    connect(m_dailyMusicPage, &MusicListPage::playMusic, this, &MainWindow::playMusicFromInfo);

    const auto onMusicListPlayAll = [this](const QList<MusicInfo> &results) {
        if (results.isEmpty())
            return;
        PlaylistManager::instance().clearPlaylist();
        PlaylistManager::instance().addAllToPlaylist(results);
        playMusicFromInfo(results.first());
    };
    connect(m_hotMusicPage, &MusicListPage::playAllRequested, this, onMusicListPlayAll);
    connect(m_latestMusicPage, &MusicListPage::playAllRequested, this, onMusicListPlayAll);
    connect(m_dailyMusicPage, &MusicListPage::playAllRequested, this, onMusicListPlayAll);

    connect(m_hotMusicPage, &MusicListPage::addToQueue, this, [this](const MusicInfo &info) {
        PlaylistManager::instance().addToPlaylist(info);
    });
    connect(m_latestMusicPage, &MusicListPage::addToQueue, this, [this](const MusicInfo &info) {
        PlaylistManager::instance().addToPlaylist(info);
    });
    connect(m_dailyMusicPage, &MusicListPage::addToQueue, this, [this](const MusicInfo &info) {
        PlaylistManager::instance().addToPlaylist(info);
    });

    connect(m_hotMusicPage, &MusicListPage::addToPlaylist, this, [this](const MusicInfo &info) {
        showAddToPlaylistDialog(info);
    });
    connect(m_latestMusicPage, &MusicListPage::addToPlaylist, this, [this](const MusicInfo &info) {
        showAddToPlaylistDialog(info);
    });
    connect(m_dailyMusicPage, &MusicListPage::addToPlaylist, this, [this](const MusicInfo &info) {
        showAddToPlaylistDialog(info);
    });

    connect(m_hotMusicPage, &MusicListPage::favoriteRequested, this, &MainWindow::toggleFavorite);
    connect(m_latestMusicPage, &MusicListPage::favoriteRequested, this, &MainWindow::toggleFavorite);
    connect(m_dailyMusicPage, &MusicListPage::favoriteRequested, this, &MainWindow::toggleFavorite);
    connect(m_hotMusicPage, &MusicListPage::playPauseRequested, this,
            &MainWindow::togglePlaybackForSystemUi);
    connect(m_latestMusicPage, &MusicListPage::playPauseRequested, this,
            &MainWindow::togglePlaybackForSystemUi);
    connect(m_dailyMusicPage, &MusicListPage::playPauseRequested, this,
            &MainWindow::togglePlaybackForSystemUi);

    connect(m_playerBar, &PlayerBar::coverClicked, this, &MainWindow::openPlayerPage);

    // 播放队列按钮
    connect(m_playerBar, &PlayerBar::playlistClicked, this, &MainWindow::togglePlaylistPanel);

    // 上一首/下一首
    connect(m_playerBar, &PlayerBar::previousClicked, this, &MainWindow::playPrevious);
    connect(m_playerBar, &PlayerBar::nextClicked, this, &MainWindow::playNext);

    // 收藏按钮
    connect(m_playerBar, &PlayerBar::favoriteClicked, this, &MainWindow::toggleFavorite);
    connect(m_playerBar, &PlayerBar::addToPlaylistClicked, this, &MainWindow::addToPlaylistFromPlayer);
    connect(m_playerBar, &PlayerBar::shareClicked, this, &MainWindow::copyCurrentTrackShare);

    connect(m_playerBar, &PlayerBar::playModeClicked, this, [this]() {
        PlaylistManager::instance().togglePlayMode();
    });

    connect(m_playerPage, &PlayerPage::backRequested, this, &MainWindow::closePlayerPage);
    connect(m_playerPage, &PlayerPage::favoriteClicked, this, &MainWindow::toggleFavorite);
    connect(m_playerPage, &PlayerPage::addToPlaylistClicked, this, &MainWindow::addToPlaylistFromPlayer);
    connect(m_playerPage, &PlayerPage::previousClicked, this, &MainWindow::playPrevious);
    connect(m_playerPage, &PlayerPage::nextClicked, this, &MainWindow::playNext);
    connect(m_playerPage, &PlayerPage::playlistClicked, this, &MainWindow::togglePlaylistPanel);
    connect(m_playerPage, &PlayerPage::desktopLyricsToggled, this, [this](bool enabled) {
        applyDesktopLyricsEnabled(enabled, true);
    });
    connect(m_playerBar, &PlayerBar::volumePercentChanged, m_playerPage, &PlayerPage::setVolumePercentSynced);
    connect(m_playerPage, &PlayerPage::volumePercentChanged, m_playerBar, &PlayerBar::setVolumePercentSynced);
    connect(m_playerPage, &PlayerPage::volumePercentChanged, this, [this](int p) {
        if (m_systemMedia)
            m_systemMedia->syncVolumeFromEngine(p / 100.0);
    });

    // 播放位置变化时更新歌词高亮
    connect(m_engine, &PlayerEngine::positionChanged, m_playerPage, &PlayerPage::updateLyricHighlight);
    connect(m_playerPage, &PlayerPage::barLyricLineChanged, m_playerBar, &PlayerBar::setBarLyricLine);
    // 注意：自动切歌仅连接上方 playbackFinished 的 lambda，勿重复 connect playNext

    // 语言切换
    connect(m_settingsPage, &SettingsPage::languageChanged, m_hotMusicPage, &MusicListPage::retranslate);
    connect(m_settingsPage, &SettingsPage::languageChanged, m_latestMusicPage, &MusicListPage::retranslate);
    connect(m_settingsPage, &SettingsPage::languageChanged, m_dailyMusicPage, &MusicListPage::retranslate);
    connect(m_settingsPage, &SettingsPage::languageChanged, m_playlistDetailPage, &PlaylistDetailPage::retranslate);
    connect(m_settingsPage, &SettingsPage::checkForUpdatesRequested, this, [this]() {
        checkForUpdates(true);
    });

    // 播放列表页面返回
    connect(m_playlistDetailPage, &PlaylistDetailPage::backRequested, this, [this]() {
        switchPage(m_homePage);
    });

    // 播放列表页面播放
    connect(m_playlistDetailPage, &PlaylistDetailPage::playMusic, this, [this](const MusicInfo &info) {
        playMusicFromInfo(info);
    });
    connect(m_playlistDetailPage, &PlaylistDetailPage::playAllRequested, this,
            [this](const QList<MusicInfo> &songs) {
                if (songs.isEmpty())
                    return;
                const MusicInfo &first = songs.first();
                playMusicById(first.id, first.title, first.artist, first.coverUrl);
            });
    connect(m_playlistDetailPage, &PlaylistDetailPage::playPauseRequested, this,
            &MainWindow::togglePlaybackForSystemUi);
    connect(m_playlistDetailPage, &PlaylistDetailPage::favoriteRequested, this,
            &MainWindow::toggleFavorite);

    // 语言切换
    connect(m_settingsPage, &SettingsPage::languageChanged, m_searchPage, &SearchPage::retranslate);
    connect(m_settingsPage, &SettingsPage::languageChanged, m_artistDetailPage,
            &ArtistDetailPage::retranslate);
    connect(m_settingsPage, &SettingsPage::languageChanged, m_vipPage, &VipPage::retranslate);

    // 播放列表页面刷新侧边栏
    connect(m_playlistDetailPage, &PlaylistDetailPage::refreshSidebarPlaylists, this, [this]() {
        m_sidebar->refreshPlaylists();
    });

    connect(m_playlistPanel, &PlaylistPanel::playRequested, this, [this](int musicId) {
        playMusicFromPlaylist(musicId);
    });

    connect(m_vipPage, &VipPage::loginRequested, this, [this]() {
        LoginDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted)
            m_vipPage->refresh();
    });

    // 桌面歌词：同进程内的独立顶层窗口（parent=nullptr 避免随主窗口最小化）
    m_desktopLrc = new DesktopLrc(nullptr);

    connect(m_engine, &PlayerEngine::positionChanged, m_desktopLrc, &DesktopLrc::updatePosition);

    connect(m_engine, &PlayerEngine::musicStarted, m_desktopLrc, [this](const MusicInfo &music) {
        if (m_desktopLrc) {
            m_desktopLrc->setCurrentSong(music.title, music.artist);
            if (m_playerPage) {
                m_playerPage->emitDesktopLyricsPayload();
            }
        }
    });

    connect(m_playerPage, &PlayerPage::lyricsPayloadReady, m_desktopLrc, &DesktopLrc::loadLrcText);

    connect(m_playerBar, &PlayerBar::desktopLyricsToggled, this, [this](bool enabled) {
        applyDesktopLyricsEnabled(enabled, true);
    });

    syncPlayModeUi();
    applyDesktopLyricsEnabled(QSettings().value(QStringLiteral("desktopLyrics"), false).toBool(), false);
}
void MainWindow::loadStyleSheet()
{
    applyTheme();
}

void MainWindow::applyTheme()
{
    const QString style = Theme::ThemeManager::instance().currentStyleSheet();
    /* 挂在 QApplication 上：QComboBox 下拉、部分弹出层不是 MainWindow 子控件，
     * 仅 setStyleSheet(this) 时 Windows/Fusion 下会丢失样式。 */
    setStyleSheet(QString());
    if (style.isEmpty())
        qApp->setStyleSheet(QString());
    else
        qApp->setStyleSheet(style);
    if (m_shellBackdrop)
        m_shellBackdrop->update();
    updateChromeForShellBackdrop();
}

namespace {

constexpr int kMaxBackdropCacheSide = 1600;

QSize cappedBackdropCacheSize(const QSize &widgetSize)
{
    if (widgetSize.isEmpty())
        return widgetSize;
    QSize t = widgetSize;
    const int side = qMax(t.width(), t.height());
    if (side <= kMaxBackdropCacheSide)
        return t;
    t.scale(kMaxBackdropCacheSide, kMaxBackdropCacheSide, Qt::KeepAspectRatio);
    return t;
}

} // namespace

void MainWindow::scheduleShellBackdropRebuild(int delayMs)
{
    if (!m_shellBackdropRebuildTimer)
        return;
    m_shellBackdropRebuildTimer->start(qMax(0, delayMs));
}

void MainWindow::rebuildShellBackdropCache()
{
    if (!m_shellBackdrop || !ShellBackdropSettings::instance().usesImageBackdrop())
        return;

    const QSize target = cappedBackdropCacheSize(m_shellBackdrop->size());
    if (target.isEmpty())
        return;
    if (m_shellBackdropCacheSize == target && !m_shellBackdropCache.isNull())
        return;

    const QPixmap source = ShellBackdropSettings::instance().cachedSourcePixmap();
    if (source.isNull())
        return;

    QPixmap cover = source.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
    if (cover.width() > target.width() || cover.height() > target.height()) {
        const int x = (cover.width() - target.width()) / 2;
        const int y = (cover.height() - target.height()) / 2;
        cover = cover.copy(x, y, target.width(), target.height());
    } else if (cover.size() != target) {
        cover = cover.scaled(target, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    m_shellBackdropCache = cover;
    m_shellBackdropCacheSize = target;
    m_shellBackdrop->update();
}

QPixmap MainWindow::shellBackdropPixmapForSize(const QSize &size)
{
    if (size.isEmpty())
        return {};

    auto &backdrop = ShellBackdropSettings::instance();
    if (backdrop.kind() == ShellBackdropSettings::Kind::SolidColor) {
        QPixmap pm(size);
        pm.fill(backdrop.solidColor());
        return pm;
    }

    if (!m_shellBackdropCache.isNull()) {
        if (m_shellBackdropCache.size() == size)
            return m_shellBackdropCache;
        return m_shellBackdropCache.scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    const QPixmap src = backdrop.cachedSourcePixmap();
    if (src.isNull())
        return {};
    QPixmap cover = src.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
    if (cover.width() > size.width() || cover.height() > size.height()) {
        const int x = (cover.width() - size.width()) / 2;
        const int y = (cover.height() - size.height()) / 2;
        cover = cover.copy(x, y, size.width(), size.height());
    } else if (cover.size() != size) {
        cover = cover.scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
    return cover;
}

void MainWindow::paintShellBackdrop(QPainter &p, const QRect &r) const
{
    auto &backdrop = ShellBackdropSettings::instance();
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    if (backdrop.kind() == ShellBackdropSettings::Kind::SolidColor) {
        GlassPaint::paintMainWindowSolidBackdrop(p, r, backdrop.solidColor());
        return;
    }
    if (m_shellBackdropCache.isNull()) {
        GlassPaint::paintMainWindowDeepBackdrop(p, r, dark);
        return;
    }
    GlassPaint::paintMainWindowPagesImageBackdrop(p, r, m_shellBackdropCache, dark);
}

void MainWindow::updateChromeForShellBackdrop()
{
    if (m_sidebar)
        m_sidebar->update();
    if (m_titleBar)
        m_titleBar->update();
    if (m_playerBar)
        m_playerBar->applyShellBackdropChrome();
}

void MainWindow::switchPage(QWidget *target)
{
    if (m_switching) return;
    QWidget *current = m_stack->currentWidget();
    if (current == target) return;

    // 离开热门/最新列表时释放内存，下次进入 refresh() 会重新请求
    if (current == m_hotMusicPage)
        m_hotMusicPage->releaseCachedData();
    else if (current == m_latestMusicPage)
        m_latestMusicPage->releaseCachedData();
    else if (current == m_dailyMusicPage)
        m_dailyMusicPage->releaseCachedData();

    m_switching = true;

    // Make target visible alongside current for cross-fade
    target->show();

    // Opacity effects
    auto *currentEff = new QGraphicsOpacityEffect(current);
    currentEff->setOpacity(1.0);
    current->setGraphicsEffect(currentEff);

    auto *targetEff = new QGraphicsOpacityEffect(target);
    targetEff->setOpacity(0.0);
    target->setGraphicsEffect(targetEff);

    // Parallel cross-fade
    auto *fadeOut = new QPropertyAnimation(currentEff, "opacity");
    fadeOut->setDuration(Theme::kAnimNormal);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::OutCubic);

    auto *fadeIn = new QPropertyAnimation(targetEff, "opacity");
    fadeIn->setDuration(Theme::kAnimNormal);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutCubic);

    connect(fadeIn, &QPropertyAnimation::finished, this, [this, target, current]() {
        m_stack->setCurrentWidget(target);
        current->setGraphicsEffect(nullptr);
        target->setGraphicsEffect(nullptr);
        m_switching = false;
        if (m_playerBar)
            m_playerBar->refreshGlassBackdrop();
    });

    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::showMusicListPage(bool isHot)
{
    syncListPageFavoriteIds();
    if (isHot) {
        m_hotMusicPage->refresh();
        switchPage(m_hotMusicPage);
    } else {
        m_latestMusicPage->refresh();
        switchPage(m_latestMusicPage);
    }
}

void MainWindow::showDailyRecommendationsPage()
{
    syncListPageFavoriteIds();
    m_dailyMusicPage->refresh();
    switchPage(m_dailyMusicPage);
}

void MainWindow::showPlaylistDetailPage(int localId)
{
    syncListPageFavoriteIds();
    m_playlistDetailPage->loadPlaylist(localId);
    switchPage(m_playlistDetailPage);
}

void MainWindow::playMusicFromInfo(const MusicInfo &info)
{
    if (info.isLocalFile()) {
        playLocalMusicInfo(info);
        return;
    }
    playMusicById(info.id, info.title, info.artist, info.coverUrl);
}

void MainWindow::openAudioFileFromPath(const QString &path)
{
    const QString local = LocalMusic::normalizeOpenPathArgument(path);
    if (local.isEmpty() || !LocalMusic::isSupportedLocalAudioFile(local))
        return;
    QFileInfo fi(local);
    if (!fi.exists() || !fi.isFile())
        return;

    const QString playbackPath = LocalMusic::resolveToPlayableLocalPath(local);
    if (playbackPath.isEmpty())
        return;

    show();
    raise();
    activateWindow();

    MusicInfo info = LocalMusic::probeAndBuildInfo(playbackPath);
    if (!info.isLocalFile())
        return;
    playLocalMusicInfo(info);
}

void MainWindow::playLocalMusicInfo(const MusicInfo &info)
{
    if (!info.isLocalFile())
        return;

    m_isDownloading = false;
    disconnectDownloader();
    m_downloader->cancel();
    m_engine->stop();

    ++m_enginePlaySeq;
    const quint64 playSeq = m_enginePlaySeq;

    PlaylistManager::instance().addToPlaylist(info);

    const auto &pl = PlaylistManager::instance().playlist();
    for (int i = 0; i < pl.size(); ++i) {
        if (pl[i].id == info.id && pl[i].localPath == info.localPath) {
            PlaylistManager::instance().setCurrentIndex(i);
            break;
        }
    }

    if (m_playlistPanel && m_playlistPanel->isDrawerOpen())
        m_playlistPanel->refresh();

    m_playerBar->setCurrentMusicId(info.id);
    m_playerBar->setSongInfo(info.title, info.artist, info.coverUrl);
    m_playerBar->setFavoriteStatus(false);
    if (m_playerPage)
        m_playerPage->setFavoriteStatus(false);
    m_playerPage->setMusicInfo(info.id, info.title, info.artist, info.album, info.coverUrl);
    m_playerPage->loadLyricsForTrack(info);
    m_engine->setCurrentMusic(info);

    m_playerBar->setLoading(true);
    QTimer::singleShot(0, this, [this, info, playSeq]() {
        if (playSeq != m_enginePlaySeq)
            return;
        cancelStreamWatch();
        m_streamRetryActive = false;
        m_playerBar->setLoading(false);
        m_engine->play(QUrl::fromLocalFile(info.localPath));
    });
    refreshSystemMediaIntegration();
}

void MainWindow::createPlaylist()
{
    if (!m_apiClient || !UserManager::instance().isLoggedIn()) {
        return;
    }

    LineInputDialog dlg(this,
                        I18n::instance().tr(QStringLiteral("createPlaylist")),
                        I18n::instance().tr(QStringLiteral("playlistName")),
                        I18n::instance().tr(QStringLiteral("newPlaylist")),
                        QString(),
                        I18n::instance().tr(QStringLiteral("create")),
                        false);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString name = dlg.value();
    if (name.isEmpty())
        return;

    m_apiClient->createPlaylist(name, QString(), [this](bool success, const QString &message, const QVariantMap &) {
        if (success) {
            m_sidebar->loadPlaylists();
        }
    });
}

void MainWindow::showAddToPlaylistDialog(const MusicInfo &music)
{
    QWidget *host = centralWidget();
    if (!host)
        return;

    if (m_addToPlaylistOverlay) {
        m_addToPlaylistOverlay->close();
        m_addToPlaylistOverlay = nullptr;
    }

    auto *overlay = new AddToPlaylistDialog(music, m_apiClient, host);
    m_addToPlaylistOverlay = overlay;
    connect(overlay, &AddToPlaylistDialog::playlistsChanged, this, [this]() {
        if (m_sidebar)
            m_sidebar->refreshPlaylists();
    });
    connect(overlay, &AddToPlaylistDialog::closed, this, [this]() {
        m_addToPlaylistOverlay = nullptr;
    });
    overlay->openOn(host);
    overlay->raise();
}

void MainWindow::addToPlaylistFromPlayer(int musicId)
{
    if (musicId <= 0)
        return;

    const MusicInfo &eng = m_engine->currentMusic();
    if (eng.id == musicId) {
        showAddToPlaylistDialog(eng);
        return;
    }

    const auto &queue = PlaylistManager::instance().playlist();
    for (const MusicInfo &m : queue) {
        if (m.id == musicId) {
            showAddToPlaylistDialog(m);
            return;
        }
    }

    MusicInfo stub;
    stub.id = musicId;
    showAddToPlaylistDialog(stub);
}

QWidget *MainWindow::playlistDrawerHost() const
{
    if (m_playerPageVisible && m_playerPage)
        return m_playerPage;
    return centralWidget();
}

void MainWindow::syncPlaylistDrawerGeometry()
{
    QWidget *host = playlistDrawerHost();
    if (!host || !m_playlistPanel)
        return;

    if (m_playlistPanel->parentWidget() != host)
        m_playlistPanel->setParent(host);
    if (m_playlistScrim && m_playlistScrim->parentWidget() != host)
        m_playlistScrim->setParent(host);

    if (m_playlistScrim)
        m_playlistScrim->setGeometry(host->rect());

    m_playlistPanel->syncToHost();
    if (m_playlistPanel->isDrawerOpen()) {
        m_playlistPanel->show();
        raisePlaylistDrawerStack();
    }
}

void MainWindow::raisePlaylistDrawerStack()
{
    if (!m_playlistPanel || !m_playlistPanel->isDrawerOpen())
        return;
    if (m_playlistScrim && m_playlistScrim->isVisible()) {
        m_playlistScrim->raise();
    }
    m_playlistPanel->raise();
}

void MainWindow::showPlaylistDrawer()
{
    if (!m_playlistPanel)
        return;
    const bool fullPlayer = m_playerPageVisible && m_playerPage && playlistDrawerHost() == m_playerPage;
    if (m_playerBar)
        m_playerBar->setFloatingProgressSuppressed(true);
    m_playlistPanel->setFullPlayerMode(fullPlayer);
    syncPlaylistDrawerGeometry();
    if (m_playlistScrim) {
        auto *scrim = static_cast<PlaylistDrawerScrim *>(m_playlistScrim);
        scrim->setFullPlayerMode(fullPlayer);
        scrim->refreshBackdrop(playlistDrawerHost(), m_playlistPanel);
        scrim->fadeIn();
    }
    m_playlistPanel->openDrawer();
    raisePlaylistDrawerStack();
}

void MainWindow::hidePlaylistDrawer()
{
    if (!m_playlistPanel || !m_playlistPanel->isDrawerOpen())
        return;
    if (m_playlistScrim)
        static_cast<PlaylistDrawerScrim *>(m_playlistScrim)->fadeOut();
    m_playlistPanel->closeDrawer();
}

void MainWindow::togglePlaylistPanel()
{
    if (!m_playlistPanel)
        return;
    if (m_playlistPanel->isDrawerOpen())
        hidePlaylistDrawer();
    else
        showPlaylistDrawer();
}

void MainWindow::playMusicFromPlaylist(int musicId)
{
    auto& manager = PlaylistManager::instance();
    const auto& playlist = manager.playlist();
    for (int i = 0; i < playlist.size(); ++i) {
        if (playlist[i].id == musicId) {
            manager.setCurrentIndex(i);
            playMusicFromInfo(playlist[i]);
            break;
        }
    }
}

void MainWindow::cancelStreamWatch()
{
    if (m_streamAttemptTimer) {
        m_streamAttemptTimer->stop();
        m_streamAttemptTimer->deleteLater();
        m_streamAttemptTimer = nullptr;
    }
    if (m_streamPlayConn) {
        disconnect(m_streamPlayConn);
        m_streamPlayConn = QMetaObject::Connection();
    }
    if (m_streamErrorConn) {
        disconnect(m_streamErrorConn);
        m_streamErrorConn = QMetaObject::Connection();
    }
}

void MainWindow::startRemotePlaybackWithBackgroundCache(int musicId, quint64 playSeq, const QUrl &remoteUrl,
                                                        bool pauseWhenReady)
{
    const QString cachedPath = MusicDownloader::cachedAudioFilePath(musicId);
    if (QFile::exists(cachedPath)) {
#ifdef Q_OS_LINUX
        LinuxTmpfsCache::touchAudioCacheFile(cachedPath);
#endif
        // 已有整文件：本地播，避免单曲循环/切回已缓存曲时反复开 HTTP 流导致卡顿
        cancelStreamWatch();
        m_streamRetryActive = false;
        m_playerBar->setLoading(false);
        m_engine->play(QUrl::fromLocalFile(cachedPath));
        if (pauseWhenReady)
            QTimer::singleShot(50, this, [this]() { m_engine->pause(); });
        return;
    }

    cancelStreamWatch();
    m_streamRetryActive = true;
    m_streamRemoteUrl = remoteUrl;
    m_streamPauseWhenReady = pauseWhenReady;
    m_remoteStreamFailureCount = 0;

    attachStreamPlaybackGuards(musicId, playSeq);
    m_engine->play(remoteUrl);
}

void MainWindow::startBackgroundCacheDownload(int musicId, quint64 playSeq, const QUrl &url)
{
    if (m_bgCacheFinishedConn) {
        disconnect(m_bgCacheFinishedConn);
        m_bgCacheFinishedConn = QMetaObject::Connection();
    }
    if (m_bgCacheErrorConn) {
        disconnect(m_bgCacheErrorConn);
        m_bgCacheErrorConn = QMetaObject::Connection();
    }

    m_bgCacheFinishedConn = connect(m_downloader, &MusicDownloader::downloadFinished, this,
        [this, playSeq, musicId](const QString &path) {
            if (playSeq != m_enginePlaySeq)
                return;
            qDebug() << "[后台缓存完成] id=" << musicId << path;
            if (m_playerPage && m_playerBar && m_playerBar->currentMusicId() == musicId)
                m_playerPage->refreshAudioQuality();
        });
    m_bgCacheErrorConn = connect(m_downloader, &MusicDownloader::downloadError, this,
        [this, playSeq, musicId](const QString &err) {
            if (playSeq != m_enginePlaySeq)
                return;
            qDebug() << "[后台缓存失败] id=" << musicId << err;
        });

    m_downloader->download(url, musicId);
}

void MainWindow::attachStreamPlaybackGuards(int musicId, quint64 playSeq)
{
    cancelStreamWatch();
    m_streamFailHandledThisRound = false;

    m_streamAttemptTimer = new QTimer(this);
    m_streamAttemptTimer->setSingleShot(true);
    m_streamAttemptTimer->setInterval(kStreamReadyTimeoutMs);
    connect(m_streamAttemptTimer, &QTimer::timeout, this, [this, musicId, playSeq]() {
        QTimer *t = m_streamAttemptTimer;
        m_streamAttemptTimer = nullptr;
        if (t)
            t->deleteLater();
        if (playSeq != m_enginePlaySeq)
            return;
        if (m_engine->playbackState() == PlayerEngine::Playing)
            return;
        qDebug() << "[Music] 远程起播超时 id=" << musicId;
        handleRemoteStreamFailure(musicId, playSeq);
    });

    m_streamPlayConn = connect(m_engine, &PlayerEngine::stateChanged, this,
        [this, playSeq, musicId](PlayerEngine::PlaybackState st) {
            if (playSeq != m_enginePlaySeq || st != PlayerEngine::Playing)
                return;
            m_streamRetryActive = false;
            cancelStreamWatch();
            if (m_streamPauseWhenReady)
                QTimer::singleShot(50, this, [this]() { m_engine->pause(); });
            QTimer::singleShot(kBgCacheDelayMs, this, [this, playSeq, musicId]() {
                if (playSeq != m_enginePlaySeq || m_streamRemoteUrl.isEmpty())
                    return;
                startBackgroundCacheDownload(musicId, playSeq, m_streamRemoteUrl);
            });
        });

    m_streamErrorConn = connect(m_engine, &PlayerEngine::mediaError, this,
        [this, musicId, playSeq](const QString &err) {
            if (playSeq != m_enginePlaySeq)
                return;
            qDebug() << "[Music] 远程播放错误 id=" << musicId << err;
            handleRemoteStreamFailure(musicId, playSeq, true);
        });

    m_streamAttemptTimer->start();
}

void MainWindow::handleRemoteStreamFailure(int musicId, quint64 playSeq, bool midPlaybackError)
{
    if (playSeq != m_enginePlaySeq)
        return;
    if (midPlaybackError && m_midPlaybackRecoveryInFlight)
        return;
    if (!midPlaybackError && m_engine->playbackState() == PlayerEngine::Playing)
        return;
    if (!midPlaybackError && m_streamFailHandledThisRound)
        return;
    if (!midPlaybackError)
        m_streamFailHandledThisRound = true;
    if (midPlaybackError) {
        m_midPlaybackRecoveryInFlight = true;
        QTimer::singleShot(900, this, [this]() { m_midPlaybackRecoveryInFlight = false; });
    }

    const qint64 resumePos = midPlaybackError ? m_engine->position() : 0;
    m_engine->stop();
    m_downloader->cancel();
    if (m_bgCacheFinishedConn) {
        disconnect(m_bgCacheFinishedConn);
        m_bgCacheFinishedConn = QMetaObject::Connection();
    }
    if (m_bgCacheErrorConn) {
        disconnect(m_bgCacheErrorConn);
        m_bgCacheErrorConn = QMetaObject::Connection();
    }

    if (midPlaybackError) {
        const QString cachedPath = MusicDownloader::cachedAudioFilePath(musicId);
        if (QFile::exists(cachedPath)) {
            qDebug() << "[Music] 断流，改播完整缓存 id=" << musicId << "pos=" << resumePos;
            m_remoteStreamFailureCount = 0;
            m_streamRetryActive = false;
            cancelStreamWatch();
            m_playerBar->setLoading(false);
            m_engine->playLocalResuming(cachedPath, resumePos);
            return;
        }
        const QString partPath = MusicDownloader::cachedAudioFilePath(musicId) + QStringLiteral(".part");
        if (QFileInfo(partPath).size() >= kMinPartResumeBytes) {
            qDebug() << "[Music] 断流，尝试从部分缓存续播 id=" << musicId << "pos=" << resumePos;
            m_remoteStreamFailureCount = 0;
            m_streamRetryActive = true;
            cancelStreamWatch();
            attachStreamPlaybackGuards(musicId, playSeq);
            m_engine->playLocalResuming(partPath, resumePos);
            return;
        }
    }

    m_remoteStreamFailureCount++;
    if (m_remoteStreamFailureCount >= 3) {
        m_streamRetryActive = false;
        cancelStreamWatch();
        m_playerBar->setLoading(false);
        qDebug() << "[Music] 远程播放失败已达 3 次，切下一首 id=" << musicId;
        playNext();
        return;
    }

    cancelStreamWatch();
    QTimer::singleShot(kStreamRetryDelayMs, this, [this, musicId, playSeq]() {
        if (playSeq != m_enginePlaySeq)
            return;
        m_streamRetryActive = true;
        attachStreamPlaybackGuards(musicId, playSeq);
        m_engine->play(m_streamRemoteUrl);
    });
}

void MainWindow::disconnectDownloader()
{
    qDebug() << "[MainWindow] disconnectDownloader called";
    cancelStreamWatch();
    if (m_finishedConn) {
        disconnect(m_finishedConn);
        m_finishedConn = QMetaObject::Connection();
    }
    if (m_errorConn) {
        disconnect(m_errorConn);
        m_errorConn = QMetaObject::Connection();
    }
    if (m_bufferConn) {
        disconnect(m_bufferConn);
        m_bufferConn = QMetaObject::Connection();
    }
    if (m_progressConn) {
        disconnect(m_progressConn);
        m_progressConn = QMetaObject::Connection();
    }
    if (m_bgCacheFinishedConn) {
        disconnect(m_bgCacheFinishedConn);
        m_bgCacheFinishedConn = QMetaObject::Connection();
    }
    if (m_bgCacheErrorConn) {
        disconnect(m_bgCacheErrorConn);
        m_bgCacheErrorConn = QMetaObject::Connection();
    }
}

void MainWindow::playNext()
{
    qDebug() << "[MainWindow] playNext called";

    m_isDownloading = false;
    disconnectDownloader();
    m_downloader->cancel();
    m_engine->stop();

    ++m_enginePlaySeq;
    const quint64 playSeq = m_enginePlaySeq;

    auto& manager = PlaylistManager::instance();
    if (manager.count() == 0) return;

    int nextIdx = manager.nextIndex();
    const MusicInfo info = manager.playlist()[nextIdx];
    manager.setCurrentIndex(nextIdx);

    // 与上一曲、点歌一致：立即同步播放栏与播放页（勿等 singleShot，避免播放页滞后）
    m_playerBar->setCurrentMusicId(info.id);
    m_playerBar->setSongInfo(info.title, info.artist, info.coverUrl);
    const bool favorited = checkIsFavorited(info.id);
    m_playerBar->setFavoriteStatus(favorited);
    if (m_playerPage)
        m_playerPage->setFavoriteStatus(favorited);
    m_playerPage->setMusicInfo(info.id, info.title, info.artist, info.album, info.coverUrl);
    m_playerPage->loadLyricsForTrack(info);
    m_engine->setCurrentMusic(info);

    // Refresh system media with the new song info immediately
    // This ensures MPRIS clients get the correct metadata even during stop() state
    refreshSystemMediaIntegration();

    QTimer::singleShot(80, this, [this, info, playSeq]() {
        if (playSeq != m_enginePlaySeq)
            return;

        if (info.isLocalFile()) {
            cancelStreamWatch();
            m_streamRetryActive = false;
            m_playerBar->setLoading(false);
            m_engine->play(QUrl::fromLocalFile(info.localPath));
            return;
        }

        m_playerBar->setLoading(true);
        const QUrl url(QStringLiteral("%1/api/music/file/%2").arg(Theme::kApiBase).arg(info.id));
        startRemotePlaybackWithBackgroundCache(info.id, playSeq, url, false);
    });
}

void MainWindow::playPrevious()
{
    qDebug() << "[MainWindow] playPrevious called";
    auto& manager = PlaylistManager::instance();
    if (manager.count() == 0) {
        qDebug() << "[MainWindow] Playlist is empty";
        Toast::show(this, tr("播放列表为空"));
        return;
    }

    m_isDownloading = false;
    disconnectDownloader();
    m_downloader->cancel();
    m_engine->stop();

    ++m_enginePlaySeq;
    const quint64 playSeq = m_enginePlaySeq;

    int prevIdx = manager.previousIndex();
    const MusicInfo info = manager.playlist()[prevIdx];

    qDebug() << "[切歌] 上一曲:" << info.title << "-" << info.artist << "(ID:" << info.id << ")";

    manager.setCurrentIndex(prevIdx);

    m_playerBar->setCurrentMusicId(info.id);
    m_playerBar->setSongInfo(info.title, info.artist, info.coverUrl);
    const bool favorited = checkIsFavorited(info.id);
    m_playerBar->setFavoriteStatus(favorited);
    if (m_playerPage)
        m_playerPage->setFavoriteStatus(favorited);
    m_playerPage->setMusicInfo(info.id, info.title, info.artist, info.album, info.coverUrl);
    m_playerPage->loadLyricsForTrack(info);
    m_engine->setCurrentMusic(info);

    // Refresh system media with the new song info immediately
    refreshSystemMediaIntegration();

    QTimer::singleShot(0, this, [this, info, playSeq]() {
        if (playSeq != m_enginePlaySeq)
            return;

        if (info.isLocalFile()) {
            cancelStreamWatch();
            m_streamRetryActive = false;
            m_playerBar->setLoading(false);
            m_engine->play(QUrl::fromLocalFile(info.localPath));
            return;
        }

        const QUrl url(QString::fromUtf8("%1/api/music/file/%2").arg(Theme::kApiBase).arg(info.id));
        qDebug() << "[音乐加载] 远程起播并并行缓存:" << url.toString();

        m_playerBar->setLoading(true);
        startRemotePlaybackWithBackgroundCache(info.id, playSeq, url, false);
    });
}

void MainWindow::playMusicById(int musicId, const QString &title, const QString &artist, const QString &coverUrl)
{
    if (musicId <= 0) return;

    m_isDownloading = false;
    disconnectDownloader();
    m_downloader->cancel();
    m_engine->stop();

    ++m_enginePlaySeq;
    const quint64 playSeq = m_enginePlaySeq;

    qDebug() << "[音乐加载] 播放音乐:" << title << "-" << artist << "(ID:" << musicId << ")";

    // 自动添加到播放队列（去重）
    MusicInfo mInfo;
    mInfo.id = musicId;
    mInfo.title = title;
    mInfo.artist = artist;
    mInfo.album = QString();
    mInfo.duration = 0;
    mInfo.coverUrl = coverUrl;
    PlaylistManager::instance().addToPlaylist(mInfo);

    // 更新当前索引到刚添加的音乐
    const auto& playlist = PlaylistManager::instance().playlist();
    for (int i = 0; i < playlist.size(); ++i) {
        if (playlist[i].id == musicId) {
            PlaylistManager::instance().setCurrentIndex(i);
            break;
        }
    }

    if (m_playlistPanel && m_playlistPanel->isDrawerOpen())
        m_playlistPanel->refresh();

    // Update player bar
    m_playerBar->setCurrentMusicId(musicId);
    m_playerBar->setSongInfo(title, artist, coverUrl);

    // 检查收藏状态
    bool isFavorited = checkIsFavorited(musicId);
    m_playerBar->setFavoriteStatus(isFavorited);
    if (m_playerPage)
        m_playerPage->setFavoriteStatus(isFavorited);

    // Update player page
    m_playerPage->setMusicInfo(musicId, title, artist, QString(), coverUrl);
    m_playerPage->loadLyricsForTrack(mInfo);

    // Set current music info for recent play tracking
    m_engine->setCurrentMusic(mInfo);

    const QUrl url(QString::fromUtf8("%1/api/music/file/%2").arg(Theme::kApiBase).arg(musicId));
    qDebug() << "[音乐加载] 远程起播并并行缓存:" << url.toString();

    m_playerBar->setLoading(true);
    startRemotePlaybackWithBackgroundCache(musicId, playSeq, url, false);
}

QRect MainWindow::playerPageOverlayGeometry() const
{
    if (QWidget *central = centralWidget())
        return central->rect();
    return rect();
}

void MainWindow::openPlayerPage()
{
    if (!m_playerPage || m_playerPageVisible)
        return;

    if (m_playlistPanel && m_playlistPanel->isDrawerOpen())
        hidePlaylistDrawer();

    m_playerPageVisible = true;
    if (m_playerBar) {
        m_playerBar->setChromeVisible(false);
        m_playerBar->setCoverVisible(false);
        m_playerBar->hide();
    }

    QWidget *host = centralWidget();
    if (!host)
        host = this;
    const QRect area = playerPageOverlayGeometry();
    // 先挂到 host 但保持隐藏，低分辨率 render 主界面后再 show，避免全屏 grab 卡顿
    m_playerPage->setParent(host);
    m_playerPage->setGeometry(area);
    m_playerPage->move(0, area.height());
    m_playerPage->setOpenTransitionActive(true);
    const QPixmap snap = shellBackdropPixmapForSize(area.size());
    if (!snap.isNull())
        m_playerPage->setUnderlaySnapshot(snap, area.size());
    else
        m_playerPage->refreshUnderlayBackdrop(host, area.size());
    m_playerPage->show();
    m_playerPage->raise();
    syncPlayModeUi();
    applyDesktopLyricsEnabled(QSettings().value(QStringLiteral("desktopLyrics"), false).toBool(), false);

    auto *anim = new QPropertyAnimation(m_playerPage, "pos", this);
    anim->setDuration(Theme::kAnimNormal);
    anim->setStartValue(QPoint(0, area.height()));
    anim->setEndValue(QPoint(0, 0));
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, this, [this]() {
        if (m_playerPage)
            m_playerPage->setOpenTransitionActive(false);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::closePlayerPage()
{
    if (!m_playerPage || !m_playerPageVisible)
        return;

    const QRect area = playerPageOverlayGeometry();
    m_playerPage->setOpenTransitionActive(true);
    auto *anim = new QPropertyAnimation(m_playerPage, "pos", this);
    anim->setDuration(Theme::kAnimNormal);
    anim->setStartValue(m_playerPage->pos());
    anim->setEndValue(QPoint(0, area.height()));
    anim->setEasingCurve(QEasingCurve::InCubic);
    connect(anim, &QPropertyAnimation::finished, this, [this]() {
        if (m_playerPage)
            m_playerPage->setOpenTransitionActive(false);
        m_playerPageVisible = false;
        if (m_playerPage) {
            m_playerPage->hide();
            if (m_midWidget)
                m_playerPage->setParent(m_midWidget);
            m_playerPage->move(0, 0);
            m_playerPage->setGeometry(m_midWidget ? m_midWidget->rect() : QRect());
        }
        if (m_playerBar) {
            m_playerBar->show();
            m_playerBar->setCoverVisible(true);
            m_playerBar->setChromeVisible(true);
            m_playerBar->relayoutChrome();
        }
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_shellBackdrop && centralWidget()) {
        m_shellBackdrop->setGeometry(centralWidget()->rect());
        scheduleShellBackdropRebuild(150);
    }
    if (m_playerPage) {
        if (m_playerPageVisible)
            m_playerPage->setGeometry(playerPageOverlayGeometry());
        else if (m_midWidget)
            m_playerPage->setGeometry(m_midWidget->rect());
    }
    if (m_playlistPanel && m_playlistPanel->isDrawerOpen()) {
        const bool fullPlayer = m_playerPageVisible && m_playerPage && playlistDrawerHost() == m_playerPage;
        m_playlistPanel->setFullPlayerMode(fullPlayer);
        syncPlaylistDrawerGeometry();
        if (m_playlistScrim) {
            auto *scrim = static_cast<PlaylistDrawerScrim *>(m_playlistScrim);
            scrim->setFullPlayerMode(fullPlayer);
            scrim->refreshBackdrop(playlistDrawerHost(), m_playlistPanel);
        }
    }
    if (m_playerBar && m_playerBar->isVisible())
        m_playerBar->relayoutChrome();
    if (m_playlistPanel && m_playlistPanel->isDrawerOpen())
        raisePlaylistDrawerStack();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 最小化到托盘而不是直接退出
    hide();
    event->ignore();
}

bool MainWindow::event(QEvent *event)
{
    if (event->type() == QEvent::FileOpen) {
        auto *e = static_cast<QFileOpenEvent *>(event);
        const QString p = e->file();
        if (!p.isEmpty()) {
            QTimer::singleShot(0, this, [this, p]() { openAudioFileFromPath(p); });
        }
        return true;
    }
    return QMainWindow::event(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (!isMaximized() && windowHandle()) {
        QWidget *w = qobject_cast<QWidget *>(watched);
        if (w && w->window() == this) {
            switch (event->type()) {
            case QEvent::MouseButtonPress: {
                auto *e = static_cast<QMouseEvent *>(event);
                if (e->button() == Qt::LeftButton) {
                    const Qt::Edges edges =
                        framelessResizeEdgesAt(this, e->globalPosition().toPoint());
                    if (edges) {
                        windowHandle()->startSystemResize(edges);
                        return true;
                    }
                }
                break;
            }
            case QEvent::MouseMove: {
                auto *e = static_cast<QMouseEvent *>(event);
                const Qt::Edges edges =
                    framelessResizeEdgesAt(this, e->globalPosition().toPoint());
                if (edges) {
                    const Qt::CursorShape shape = cursorForResizeEdges(edges);
                    const QCursor *cur = QApplication::overrideCursor();
                    if (!cur || cur->shape() != shape)
                        QApplication::setOverrideCursor(shape);
                } else if (QApplication::overrideCursor()) {
                    QApplication::restoreOverrideCursor();
                }
                break;
            }
            case QEvent::Leave: {
                if (QApplication::overrideCursor())
                    QApplication::restoreOverrideCursor();
                break;
            }
            default:
                break;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::createTrayIcon()
{
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(this);
        m_trayMenu = new QMenu(this);
        
        // 设置托盘图标
        QIcon trayIcon(QStringLiteral(":/icons/app.png"));
        if (trayIcon.isNull())
            trayIcon = QApplication::windowIcon();
        m_trayIcon->setIcon(trayIcon);
        m_trayIcon->setToolTip("Neko云音乐");
        
        // 创建托盘菜单
        QAction *previousAction = new QAction("上一首", this);
        QAction *playPauseAction = new QAction("播放/暂停", this);
        QAction *nextAction = new QAction("下一首", this);
        QAction *showAction = new QAction("显示主窗口", this);
        QAction *quitAction = new QAction("退出", this);
        
        connect(previousAction, &QAction::triggered, this, &MainWindow::onTrayPrevious);
        connect(playPauseAction, &QAction::triggered, this, &MainWindow::onTrayPlayPause);
        connect(nextAction, &QAction::triggered, this, &MainWindow::onTrayNext);
        connect(showAction, &QAction::triggered, this, &MainWindow::onTrayShow);
        connect(quitAction, &QAction::triggered, this, &MainWindow::onTrayQuit);
        
        m_trayMenu->addAction(previousAction);
        m_trayMenu->addAction(playPauseAction);
        m_trayMenu->addAction(nextAction);
        m_trayMenu->addSeparator();
        m_trayMenu->addAction(showAction);
        m_trayMenu->addSeparator();
        m_trayMenu->addAction(quitAction);
        
        m_trayIcon->setContextMenu(m_trayMenu);
        
        // 连接托盘图标激活信号
        connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
        
        // 显示托盘图标
        m_trayIcon->show();
    }
}

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        onTrayShow();
    }
}

void MainWindow::onTrayPrevious()
{
    playPrevious();
}

void MainWindow::onTrayPlayPause()
{
    togglePlaybackForSystemUi();
}

void MainWindow::onTrayNext()
{
    playNext();
}

void MainWindow::onTrayShow()
{
    show();
    raise();
    activateWindow();
}

void MainWindow::onTrayQuit()
{
    // 真正退出应用
    QApplication::quit();
}

void MainWindow::toggleFavorite(int musicId)
{
    qDebug() << "[收藏] 切换收藏, musicId =" << musicId;
    qDebug() << "[收藏] m_apiClient =" << (m_apiClient != nullptr) << ", 登录状态 =" << UserManager::instance().isLoggedIn();

    if (musicId <= 0 || !m_apiClient) return;

    bool isFavorited = checkIsFavorited(musicId);
    qDebug() << "[收藏] 当前收藏状态 =" << isFavorited;

    if (isFavorited) {
        qDebug() << "[收藏] 正在取消收藏, musicId =" << musicId;
        QUrl url(QString::fromUtf8("%1/api/user/favorites/%2").arg(Theme::kApiBase).arg(musicId));
        QNetworkRequest req(url);
        req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());

        auto *nam = new QNetworkAccessManager(this);
        auto *reply = nam->deleteResource(req);
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, musicId, nam]() {
            reply->deleteLater();
            nam->deleteLater();
            QByteArray body = reply->readAll();
            qDebug() << "[收藏] 取消收藏响应, 协议:" << httpProtocolLabel(reply)
                     << ", error =" << reply->error() << ", body =" << body;
            if (reply->error() == QNetworkReply::NoError) {
                m_favoritesCache.removeAll(musicId);
                m_playerBar->setFavoriteStatus(false);
                if (m_playerPage)
                    m_playerPage->setFavoriteStatus(false);
                if (m_favoritesPage)
                    m_favoritesPage->refresh();
                syncListPageFavoriteIds();
                Toast::show(this, I18n::instance().tr("cancelFavoriteSuccess"), Toast::Success);
                qDebug() << "[收藏] 已从缓存移除并更新UI";
            } else {
                QString reason = reply->errorString();
                // 尝试从 JSON body 中提取错误信息
                QJsonDocument doc = QJsonDocument::fromJson(body);
                if (!doc.isNull() && doc.isObject()) {
                    QString msg = doc.object().value("message").toString();
                    if (!msg.isEmpty()) reason = msg;
                }
                Toast::show(this, I18n::instance().tr("cancelFavoriteFailed") + ": " + reason, Toast::Error);
            }
        });
    } else {
        qDebug() << "[收藏] 正在添加收藏, musicId =" << musicId;
        QUrl url(QString::fromUtf8("%1/api/user/favorites").arg(Theme::kApiBase));
        QNetworkRequest req(url);
        req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject obj;
        obj.insert("musicId", musicId);
        QJsonDocument doc(obj);

        auto *nam = new QNetworkAccessManager(this);
        auto *reply = nam->post(req, doc.toJson());
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, musicId, nam]() {
            reply->deleteLater();
            nam->deleteLater();
            QByteArray body = reply->readAll();
            qDebug() << "[收藏] 添加收藏响应, 协议:" << httpProtocolLabel(reply)
                     << ", error =" << reply->error() << ", body =" << body;
            if (reply->error() == QNetworkReply::NoError) {
                if (!m_favoritesCache.contains(musicId)) {
                    m_favoritesCache.append(musicId);
                }
                m_playerBar->setFavoriteStatus(true);
                if (m_playerPage)
                    m_playerPage->setFavoriteStatus(true);
                syncListPageFavoriteIds();
                Toast::show(this, I18n::instance().tr("favoriteSuccess"), Toast::Success);
                qDebug() << "[收藏] 已加入缓存并更新UI";
            } else {
                QString reason = reply->errorString();
                QJsonDocument doc = QJsonDocument::fromJson(body);
                if (!doc.isNull() && doc.isObject()) {
                    QString msg = doc.object().value("message").toString();
                    if (!msg.isEmpty()) reason = msg;
                }
                Toast::show(this, I18n::instance().tr("favoriteFailed") + ": " + reason, Toast::Error);
            }
        });
    }
}

void MainWindow::copyCurrentTrackShare()
{
    if (!m_engine)
        return;
    QClipboard *clip = QGuiApplication::clipboard();
    if (!clip)
        return;

    const MusicInfo &m = m_engine->currentMusic();
    if (m.isLocalFile()) {
        if (m.localPath.isEmpty()) {
            Toast::show(this, I18n::instance().tr(QStringLiteral("shareNothingPlaying")), Toast::Error);
            return;
        }
        clip->setText(buildShareClipboardText(m));
        Toast::show(this, I18n::instance().tr(QStringLiteral("shareLocalCopied")), Toast::Success);
        return;
    }
    if (m.id <= 0) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("shareNothingPlaying")), Toast::Error);
        return;
    }
    clip->setText(buildShareClipboardText(m));
    Toast::show(this, I18n::instance().tr(QStringLiteral("shareOnlineCopied")), Toast::Success);
}

bool MainWindow::checkIsFavorited(int musicId)
{
    return m_favoritesCache.contains(musicId);
}

void MainWindow::loadFavoritesCache()
{
    qDebug() << "[收藏] 加载收藏缓存, 登录状态 =" << UserManager::instance().isLoggedIn();
    m_favoritesCache.clear();

    if (!UserManager::instance().isLoggedIn()) {
        qDebug() << "[收藏] 未登录，跳过加载";
        return;
    }
    if (!m_apiClient) {
        qWarning() << "[收藏] ApiClient 未初始化，跳过加载";
        return;
    }

    QPointer<MainWindow> self(this);
    m_apiClient->fetchFavorites([self](bool success, const QList<QVariantMap> &favorites) {
        if (!self)
            return;
        qDebug() << "[收藏] 获取收藏列表: success =" << success << ", 数量 =" << favorites.size();
        if (success) {
            for (const auto &fav : favorites) {
                int id = fav.value(QStringLiteral("id")).toInt();
                if (id > 0)
                    self->m_favoritesCache.append(id);
            }
            qDebug() << "[收藏] 缓存加载完成, 共" << self->m_favoritesCache.size() << "条";

            if (self->m_playerBar && self->m_playerBar->currentMusicId() > 0) {
                const int currentId = self->m_playerBar->currentMusicId();
                const bool isFavorited = self->m_favoritesCache.contains(currentId);
                self->m_playerBar->setFavoriteStatus(isFavorited);
                if (self->m_playerPage)
                    self->m_playerPage->setFavoriteStatus(isFavorited);
                qDebug() << "[收藏] 缓存加载后更新收藏状态, id =" << currentId << ", favorited =" << isFavorited;
            }
            self->syncListPageFavoriteIds();
        }
    });
}

void MainWindow::syncListPageFavoriteIds()
{
    const QSet<int> ids(m_favoritesCache.begin(), m_favoritesCache.end());
    if (m_playlistDetailPage)
        m_playlistDetailPage->setFavoritedMusicIds(ids);
    if (m_recentPage)
        m_recentPage->setFavoritedMusicIds(ids);
    if (m_searchPage)
        m_searchPage->setFavoritedMusicIds(ids);
    if (m_artistDetailPage)
        m_artistDetailPage->setFavoritedMusicIds(ids);
    if (m_hotMusicPage)
        m_hotMusicPage->setFavoritedMusicIds(ids);
    if (m_latestMusicPage)
        m_latestMusicPage->setFavoritedMusicIds(ids);
    if (m_dailyMusicPage)
        m_dailyMusicPage->setFavoritedMusicIds(ids);
}

void MainWindow::maybePromptDefaultMusicPlayer()
{
    QSettings settings;
    if (settings.value(QStringLiteral("defaultMusicPlayer/skipPrompt"), false).toBool())
        return;
    if (DefaultMusicAppChecker::isDefaultMusicPlayer())
        return;

    DefaultMusicPlayerDialog dlg(this);
    const int r = dlg.exec();
    if (dlg.dontAskAgain())
        settings.setValue(QStringLiteral("defaultMusicPlayer/skipPrompt"), true);
    if (r != QDialog::Accepted)
        return;

    DefaultMusicAppChecker::trySetAsDefaultMusicPlayer();
#if defined(Q_OS_WIN)
    DefaultMusicPlayerDialog::showWindowsDefaultAppsFollowUp(this);
#endif
}

void MainWindow::checkForUpdates(bool showNoUpdateToast)
{
    // 防止重复检查
    if (m_updateChecker && m_updateDialog && m_updateDialog->isVisible()) {
        return;
    }

    m_updateChecker = new UpdateChecker(QString::fromUtf8(APP_VERSION), this);

    connect(m_updateChecker, &UpdateChecker::updateAvailable, this, [this](const UpdateInfo &info) {
        qDebug() << "[更新] 发现新版本:" << info.version << "下载链接:" << info.downloadUrl;
        m_updateDialog = new UpdateDialog(m_updateChecker->currentVersion(), info.version,
                                         info.downloadUrl, info.installKind, this);

        if (info.installKind == UpdateInstallKind::DownloadInstaller) {
            connect(m_updateDialog, &UpdateDialog::downloadRequested, this, [this](const QString &url) {
                m_updateChecker->downloadUpdate(url);
            });
            connect(m_updateChecker, &UpdateChecker::downloadProgress, m_updateDialog,
                    &UpdateDialog::showDownloadProgress);
            connect(m_updateChecker, &UpdateChecker::downloadFinished, m_updateDialog,
                    &UpdateDialog::showDownloadFinished);
            connect(m_updateChecker, &UpdateChecker::downloadFailed, m_updateDialog,
                    &UpdateDialog::showDownloadFailed);
        }

        m_updateDialog->exec();
    });

    connect(m_updateChecker, &UpdateChecker::noUpdate, this, [this, showNoUpdateToast]() {
        qDebug() << "[更新] 已是最新版本";
        if (showNoUpdateToast)
            Toast::show(this, I18n::instance().tr("isLatest"), Toast::Info);
    });

    connect(m_updateChecker, &UpdateChecker::checkFailed, this, [](const QString &error) {
        qDebug() << "[更新] 检查失败:" << error;
    });

    m_updateChecker->checkForUpdates();
}

void MainWindow::syncPlayModeUi()
{
    const QString mode = PlaylistManager::instance().playMode();
    if (m_playerBar)
        m_playerBar->updatePlayModeBtn(mode);
    if (m_playerPage)
        m_playerPage->updatePlayModeBtn(mode);
}

void MainWindow::applyDesktopLyricsEnabled(bool enabled, bool showToast)
{
    QSettings().setValue(QStringLiteral("desktopLyrics"), enabled);
    if (m_playerBar)
        m_playerBar->setDesktopLyricsChecked(enabled);
    if (m_playerPage)
        m_playerPage->setDesktopLyricsChecked(enabled);
    if (m_desktopLrc) {
        if (enabled)
            m_desktopLrc->showWindow();
        else
            m_desktopLrc->hideWindow();
    }
    if (showToast) {
        Toast::show(this, enabled ? I18n::instance().tr("desktopLyricsEnabled")
                                 : I18n::instance().tr("desktopLyricsDisabled"));
    }
}

void MainWindow::refreshSystemMediaIntegration()
{
    if (!m_systemMedia || !m_engine)
        return;
    auto &mgr = PlaylistManager::instance();
    const bool hasQueue = mgr.count() > 0;
    const bool canSeek = m_engine->duration() > 0;
    qDebug() << "[SystemMedia] refreshSystemMediaIntegration: hasQueue =" << hasQueue
             << "canSeek =" << canSeek
             << "transportState =" << m_engine->transportStateForOs();
    m_systemMedia->updateCapabilities(hasQueue, hasQueue, canSeek);
    m_systemMedia->updateLoopShuffle(mgr.playMode());
    m_systemMedia->syncVolumeFromEngine(m_engine->volume());
    m_systemMedia->updateFromEngineState(m_engine->transportStateForOs());
    if (hasQueue && mgr.currentIndex() >= 0 && mgr.currentIndex() < mgr.playlist().size()) {
        const MusicInfo &info = mgr.playlist()[mgr.currentIndex()];
        m_systemMedia->updateMetadata(info, m_engine->duration());
    } else {
        MusicInfo empty;
        m_systemMedia->updateMetadata(empty, 0);
    }
}

void MainWindow::togglePlaybackForSystemUi()
{
    if (!m_engine)
        return;
    if (m_engine->isActuallyPlaying() && !m_engine->isFadingOut())
        m_engine->fadeOut();
    else
        m_engine->fadeIn();
}

void MainWindow::setupKeyboardShortcuts()
{
    auto &global = GlobalShortcutController::instance();
    global.setHostWindow(windowHandle());
    connect(&global, &GlobalShortcutController::playPauseTriggered, this,
            &MainWindow::togglePlaybackForSystemUi);
    connect(&global, &GlobalShortcutController::nextTrackTriggered, this, &MainWindow::playNext);
    connect(&global, &GlobalShortcutController::previousTrackTriggered, this,
            &MainWindow::playPrevious);
    connect(&AppShortcuts::instance(), &AppShortcuts::shortcutsChanged, &global,
            &GlobalShortcutController::scheduleRebindAfterSettingsChange);
    connect(&global, &GlobalShortcutController::bindingFailed, this,
            [this](const QString &reason) {
                Toast::show(this, reason, Toast::Info, 4500);
            });

    QTimer::singleShot(0, this, [this]() {
        GlobalShortcutController::instance().setHostWindow(windowHandle());
        GlobalShortcutController::instance().start();
    });
}

void MainWindow::reloadKeyboardShortcuts()
{
    GlobalShortcutController::instance().rebind();
}

void MainWindow::resumePlaybackForSystemUi()
{
    if (!m_engine)
        return;
    if (!m_engine->isActuallyPlaying())
        m_engine->fadeIn();
}

void MainWindow::pausePlaybackForSystemUi()
{
    if (!m_engine)
        return;
    if (m_engine->isActuallyPlaying())
        m_engine->fadeOut();
}
