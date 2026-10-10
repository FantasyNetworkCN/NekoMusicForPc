/**
 * @file downloadpage.cpp
 * @brief 下载管理 — 正在下载 / 已完成 两个分区
 */

#include "downloadpage.h"
#include "songlistwidget.h"
#include "core/i18n.h"
#include "core/musicdownloadmanager.h"
#include "core/playlistdb.h"
#include "core/playlistmanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"
#include "ui/toast.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QMessageBox>
#include <QShowEvent>
#include <QFile>
#include <QFileInfo>

DownloadPage::DownloadPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("downloadPage"));
    setupUi();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) { applyPageStyle(); });

    connect(&PlaylistManager::instance(), &PlaylistManager::playlistChanged, this,
            &DownloadPage::updatePlayingHighlight);
    connect(&PlaylistManager::instance(), &PlaylistManager::currentIndexChanged, this,
            &DownloadPage::updatePlayingHighlight);

    auto &mgr = MusicDownloadManager::instance();
    connect(&mgr, &MusicDownloadManager::downloadsChanged, this, &DownloadPage::refresh);
    connect(&mgr, &MusicDownloadManager::downloadCompleted, this, &DownloadPage::refresh);
    connect(&mgr, &MusicDownloadManager::downloadFailed, this, &DownloadPage::refresh);
    connect(&mgr, &MusicDownloadManager::downloadCancelled, this, [this](int) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("downloadCancelled")), Toast::Info);
        refresh();
    });
    connect(&mgr, &MusicDownloadManager::downloadProgress, this, [this](int, qint64, qint64) {
        if (m_activeTab == Tab::Downloading && m_songList)
            m_songList->refreshDownloadProgress();
    });
}

void DownloadPage::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 12, 24, 24);
    root->setSpacing(0);

    m_header = new QWidget(this);
    auto *headerLay = new QVBoxLayout(m_header);
    headerLay->setContentsMargins(0, 0, 0, 0);
    headerLay->setSpacing(12);

    auto *titleRow = new QWidget(m_header);
    auto *titleLay = new QHBoxLayout(titleRow);
    titleLay->setContentsMargins(0, 0, 0, 0);
    titleLay->setSpacing(8);
    titleLay->setAlignment(Qt::AlignBottom);

    m_titleLbl = new QLabel(I18n::instance().tr(QStringLiteral("downloadManage")), titleRow);
    titleLay->addWidget(m_titleLbl);

    m_countLbl = new QLabel(titleRow);
    titleLay->addWidget(m_countLbl);
    titleLay->addStretch();
    headerLay->addWidget(titleRow);

    m_tabBar = new QWidget(m_header);
    m_tabBar->setObjectName(QStringLiteral("downloadTabBar"));
    auto *tabLay = new QHBoxLayout(m_tabBar);
    tabLay->setContentsMargins(0, 0, 0, 0);
    tabLay->setSpacing(0);

    auto *tabGroup = new QButtonGroup(this);
    tabGroup->setExclusive(true);

    m_tabDownloading = new QPushButton(m_tabBar);
    m_tabDownloading->setCheckable(true);
    m_tabDownloading->setCursor(Qt::PointingHandCursor);
    m_tabDownloading->setFixedHeight(32);
    tabGroup->addButton(m_tabDownloading);
    tabLay->addWidget(m_tabDownloading);

    m_tabCompleted = new QPushButton(m_tabBar);
    m_tabCompleted->setCheckable(true);
    m_tabCompleted->setCursor(Qt::PointingHandCursor);
    m_tabCompleted->setFixedHeight(32);
    tabGroup->addButton(m_tabCompleted);
    tabLay->addWidget(m_tabCompleted);
    tabLay->addStretch();

    m_tabDownloading->setChecked(true);
    connect(m_tabDownloading, &QPushButton::clicked, this, [this]() { setActiveTab(Tab::Downloading); });
    connect(m_tabCompleted, &QPushButton::clicked, this, [this]() { setActiveTab(Tab::Completed); });
    headerLay->addWidget(m_tabBar);

    m_statusHintLbl = new QLabel(m_header);
    m_statusHintLbl->hide();
    headerLay->addWidget(m_statusHintLbl);

    auto *menuRow = new QWidget(m_header);
    menuRow->setFixedHeight(40);
    auto *menuLay = new QHBoxLayout(menuRow);
    menuLay->setContentsMargins(0, 0, 0, 0);
    menuLay->setSpacing(12);

    m_playBtn = new QPushButton(I18n::instance().tr(QStringLiteral("play")), menuRow);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setFixedHeight(40);
    connect(m_playBtn, &QPushButton::clicked, this, [this]() {
        const QList<MusicInfo> songs = activeSongs();
        if (!songs.isEmpty())
            emit playAllRequested(songs);
    });
    menuLay->addWidget(m_playBtn);

    m_clearBtn = new QPushButton(I18n::instance().tr(QStringLiteral("clearDownloads")), menuRow);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setFixedHeight(40);
    connect(m_clearBtn, &QPushButton::clicked, this, &DownloadPage::confirmClearDownloads);
    menuLay->addWidget(m_clearBtn);

    menuLay->addStretch();
    headerLay->addWidget(menuRow);
    m_header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    root->addWidget(m_header);

    m_contentStack = new QStackedWidget(this);
    m_contentStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    m_songList = new SongListWidget(m_contentStack);
    m_songList->onSongActivate = [this](const MusicInfo &info) {
        if (m_activeTab == Tab::Completed)
            emit playRequested(info);
    };
    m_songList->onSongPlayNext = [this](const MusicInfo &info) {
        if (m_activeTab == Tab::Completed)
            emit playRequested(info);
    };
    m_songList->onUnfavorite = [this](int id) { emit favoriteRequested(id); };
    // 该页的“收藏”是切换语义：统一菜单里作为「添加/取消收藏」双向入口复用
    m_songList->onToggleFavorite = [this](const MusicInfo &info) { emit favoriteRequested(info.id); };
    m_songList->isFavorited = [this](int id) { return m_favoritedIds.contains(id); };
    m_songList->onTogglePlayPause = [this]() { emit playPauseRequested(); };
    m_songList->setShowDownloadActions(false);
    m_contentStack->addWidget(m_songList);

    m_emptyWrap = new QWidget(m_contentStack);
    auto *emptyLay = new QVBoxLayout(m_emptyWrap);
    emptyLay->setContentsMargins(0, 0, 0, 0);
    emptyLay->setAlignment(Qt::AlignCenter);
    emptyLay->setSpacing(12);
    m_emptyIcon = new QLabel(m_emptyWrap);
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    m_emptyIcon->hide();
    emptyLay->addWidget(m_emptyIcon);
    m_statusLabel = new QLabel(m_emptyWrap);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->hide();
    emptyLay->addWidget(m_statusLabel);
    m_contentStack->addWidget(m_emptyWrap);

    root->addWidget(m_contentStack, 1);

    applyPageStyle();
    m_pendingDownloads = MusicDownloadManager::instance().pendingDownloads();
    loadCompletedDownloads();
    setActiveTab(Tab::Downloading);
}

void DownloadPage::setActiveTab(Tab tab)
{
    m_activeTab = tab;
    if (m_tabDownloading)
        m_tabDownloading->setChecked(tab == Tab::Downloading);
    if (m_tabCompleted)
        m_tabCompleted->setChecked(tab == Tab::Completed);
    loadActiveTab();
}

QList<MusicInfo> DownloadPage::activeSongs() const
{
    return m_activeTab == Tab::Downloading ? m_pendingDownloads : m_completedDownloads;
}

void DownloadPage::loadActiveTab()
{
    const QList<MusicInfo> songs = activeSongs();
    const bool downloadingTab = m_activeTab == Tab::Downloading;
    updateHeaderMeta();
    updateTabLabels();

    if (m_songList) {
        m_songList->setDownloadTaskMode(downloadingTab);
        if (downloadingTab) {
            auto &mgr = MusicDownloadManager::instance();
            m_songList->isActiveDownload = [&mgr](int id) { return mgr.isActiveDownload(id); };
            m_songList->downloadProgressReceived = [&mgr](int id) { return mgr.progressReceived(id); };
            m_songList->downloadProgressTotal = [&mgr](int id) { return mgr.progressTotal(id); };
            m_songList->onCancelDownload = [&mgr](int id) { mgr.cancelDownload(id); };
        } else {
            m_songList->isActiveDownload = nullptr;
            m_songList->downloadProgressReceived = nullptr;
            m_songList->downloadProgressTotal = nullptr;
            m_songList->onCancelDownload = nullptr;
        }
    }

    if (songs.isEmpty()) {
        if (m_songList)
            m_songList->setSongs({});
        const char *icon = downloadingTab ? "Download" : "DownloadDone";
        const QString text = downloadingTab
                                 ? I18n::instance().tr(QStringLiteral("emptyDownloading"))
                                 : I18n::instance().tr(QStringLiteral("emptyDownloads"));
        showPageStatus(text, icon);
        return;
    }

    hidePageStatus();
    if (m_songList) {
        m_songList->setSongs(songs);
        m_songList->refreshFavoriteDisplay();
        if (downloadingTab)
            m_songList->refreshDownloadProgress();
    }
    updatePlayingHighlight();
}

void DownloadPage::loadCompletedDownloads()
{
    m_completedDownloads.clear();

    const QList<MusicInfo> records = PlaylistDatabase::instance().getDownloads();
    for (const MusicInfo &info : records) {
        if (!info.localPath.isEmpty() && QFile::exists(info.localPath))
            m_completedDownloads.append(info);
        else
            PlaylistDatabase::instance().removeDownload(info.id);
    }
}

void DownloadPage::applyPageStyle()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString titleFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QString metaFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.72)");
    const QString statusFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.55)");
    const QString tabBg = dark ? QStringLiteral("#2a2a2a") : QStringLiteral("#f0f0f0");
    const QString tabSelBg = dark ? QStringLiteral("#3a3a3a") : QStringLiteral("#ffffff");

    if (m_titleLbl) {
        m_titleLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 30px; font-weight: 700; color: %1; }").arg(titleFg));
    }
    if (m_countLbl) {
        m_countLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 15px; font-weight: 400; color: %1; padding-bottom: 2px; }")
                                      .arg(metaFg));
    }
    if (m_statusHintLbl) {
        m_statusHintLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 13px; color: %1; }").arg(metaFg));
    }

    const QString tabStyle = QStringLiteral(
        "QPushButton {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 8px;"
        "  color: %1;"
        "  font-size: 14px;"
        "  font-weight: 500;"
        "  padding: 0 16px;"
        "}"
        "QPushButton:checked {"
        "  background: %2;"
        "  color: %3;"
        "}"
        "QPushButton:hover:!checked { color: %3; }")
                                .arg(metaFg, tabSelBg, titleFg);
    if (m_tabBar) {
        m_tabBar->setStyleSheet(QStringLiteral("QWidget#downloadTabBar { background: %1; border-radius: 10px; }")
                                    .arg(tabBg));
    }
    if (m_tabDownloading)
        m_tabDownloading->setStyleSheet(tabStyle);
    if (m_tabCompleted)
        m_tabCompleted->setStyleSheet(tabStyle);

    if (m_playBtn) {
        m_playBtn->setIcon(Icons::renderNamed("Play", 18, QColor(255, 255, 255)));
        m_playBtn->setIconSize(QSize(18, 18));
        m_playBtn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: #D84B63;"
            "  color: #ffffff;"
            "  border: none;"
            "  border-radius: 20px;"
            "  font-size: 14px;"
            "  font-weight: 500;"
            "  padding: 0 20px;"
            "}"
            "QPushButton:hover { background: #ff5070; }"
            "QPushButton:disabled { background: rgba(230,57,80,0.35); color: rgba(255,255,255,0.6); }"));
    }

    const QString secondaryBg = dark ? QStringLiteral("#2a2a2a") : QStringLiteral("#f0f0f0");
    const QString secondaryFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QColor secondaryIc = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
    if (m_clearBtn) {
        m_clearBtn->setIcon(Icons::renderNamed("Delete", 18, secondaryIc));
        m_clearBtn->setIconSize(QSize(18, 18));
        m_clearBtn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: %1;"
            "  color: %2;"
            "  border: none;"
            "  border-radius: 20px;"
            "  font-size: 14px;"
            "  font-weight: 500;"
            "  padding: 0 20px;"
            "}"
            "QPushButton:hover { background: rgba(230,57,80,0.15); }"
            "QPushButton:disabled { color: rgba(128,128,128,0.8); }")
                                     .arg(secondaryBg, secondaryFg));
    }

    if (m_statusLabel) {
        m_statusLabel->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 14px; color: %1; padding: 60px 24px; }").arg(statusFg));
    }

    if (m_songList)
        m_songList->applyTheme();
}

void DownloadPage::retranslate()
{
    if (m_titleLbl)
        m_titleLbl->setText(I18n::instance().tr(QStringLiteral("downloadManage")));
    if (m_playBtn)
        m_playBtn->setText(I18n::instance().tr(QStringLiteral("play")));
    if (m_clearBtn)
        m_clearBtn->setText(I18n::instance().tr(QStringLiteral("clearDownloads")));
    if (m_songList)
        m_songList->retranslate();
    updateTabLabels();
    updateHeaderMeta();
    loadActiveTab();
    applyPageStyle();
}

void DownloadPage::refresh()
{
    m_pendingDownloads = MusicDownloadManager::instance().pendingDownloads();
    loadCompletedDownloads();
    updateTabLabels();

    if (m_activeTab == Tab::Downloading && m_pendingDownloads.isEmpty() && !m_completedDownloads.isEmpty())
        setActiveTab(Tab::Completed);
    else
        loadActiveTab();
}

void DownloadPage::setPlaybackPaused(bool paused)
{
    if (m_songList)
        m_songList->setPlaybackPaused(paused);
}

void DownloadPage::setFavoritedMusicIds(const QSet<int> &ids)
{
    m_favoritedIds = ids;
    if (m_songList)
        m_songList->refreshFavoriteDisplay();
}

void DownloadPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
    updatePlayingHighlight();
}

void DownloadPage::updateTabLabels()
{
    if (m_tabDownloading) {
        m_tabDownloading->setText(
            I18n::instance().tr(QStringLiteral("downloadInProgress"))
            + QStringLiteral(" (") + QString::number(m_pendingDownloads.size()) + QLatin1Char(')'));
    }
    if (m_tabCompleted) {
        m_tabCompleted->setText(
            I18n::instance().tr(QStringLiteral("downloadCompletedTab"))
            + QStringLiteral(" (") + QString::number(m_completedDownloads.size()) + QLatin1Char(')'));
    }
}

void DownloadPage::updateHeaderMeta()
{
    const QList<MusicInfo> songs = activeSongs();
    if (m_countLbl) {
        m_countLbl->setText(I18n::instance().tr(QStringLiteral("favoritePageSongCount"))
                                .arg(songs.size()));
    }

    const bool isCompletedTab = m_activeTab == Tab::Completed;
    if (m_statusHintLbl) {
        m_statusHintLbl->setText(
            I18n::instance().tr(QStringLiteral("downloadFolderHint"))
                .arg(MusicDownloadManager::instance().downloadDir()));
        m_statusHintLbl->setVisible(isCompletedTab && !m_completedDownloads.isEmpty());
    }

    if (m_playBtn) {
        m_playBtn->setVisible(isCompletedTab);
        m_playBtn->setEnabled(isCompletedTab && !m_completedDownloads.isEmpty());
    }
    if (m_clearBtn) {
        m_clearBtn->setVisible(isCompletedTab);
        m_clearBtn->setEnabled(isCompletedTab && !m_completedDownloads.isEmpty());
    }
}

int DownloadPage::currentPlayingMusicId() const
{
    const auto &mgr = PlaylistManager::instance();
    const int idx = mgr.currentIndex();
    if (idx < 0 || idx >= mgr.playlist().size())
        return -1;
    return mgr.playlist()[idx].id;
}

void DownloadPage::updatePlayingHighlight()
{
    if (m_songList)
        m_songList->setCurrentPlayingId(currentPlayingMusicId());
}

void DownloadPage::refreshDownloadDisplay()
{
}

void DownloadPage::showPageStatus(const QString &text, const char *iconName)
{
    if (iconName && m_emptyIcon) {
        const bool dark = Theme::ThemeManager::instance().isDarkMode();
        const QColor ic = dark ? QColor(244, 246, 255, 120) : QColor(33, 37, 41, 120);
        m_emptyIcon->setPixmap(Icons::renderNamed(iconName, 48, ic));
        m_emptyIcon->show();
    } else if (m_emptyIcon) {
        m_emptyIcon->hide();
    }
    if (m_statusLabel) {
        m_statusLabel->setText(text);
        m_statusLabel->show();
    }
    if (m_contentStack && m_emptyWrap)
        m_contentStack->setCurrentWidget(m_emptyWrap);
}

void DownloadPage::hidePageStatus()
{
    if (m_emptyIcon)
        m_emptyIcon->hide();
    if (m_statusLabel)
        m_statusLabel->hide();
    if (m_contentStack && m_songList)
        m_contentStack->setCurrentWidget(m_songList);
}

void DownloadPage::confirmClearDownloads()
{
    if (m_completedDownloads.isEmpty())
        return;

    const auto ret = QMessageBox::question(
        this, I18n::instance().tr(QStringLiteral("clearDownloads")),
        I18n::instance().tr(QStringLiteral("clearDownloadsConfirm")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    for (const MusicInfo &info : m_completedDownloads) {
        if (info.localPath.isEmpty())
            continue;
        const QFileInfo fi(info.localPath);
        QFile::remove(info.localPath);
        QFile::remove(fi.absolutePath() + QLatin1Char('/')
                      + fi.completeBaseName() + QStringLiteral(".lrc"));
    }

    PlaylistDatabase::instance().clearDownloads();
    Toast::show(this, I18n::instance().tr(QStringLiteral("clearDownloadsSuccess")), Toast::Success);
    refresh();
}

void DownloadPage::paintEvent(QPaintEvent *)
{
}
