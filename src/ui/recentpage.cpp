/**
 * @file recentpage.cpp
 * @brief 最近播放 — 1:1 SPlayer History.vue + SongList + SongCard
 */

#include "recentpage.h"
#include "songlistwidget.h"
#include "core/i18n.h"
#include "core/playlistdb.h"
#include "core/playlistmanager.h"
#include "core/musicdownloadmanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"
#include "ui/toast.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QShowEvent>

RecentPage::RecentPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("recentPage"));
    setupUi();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) { applyPageStyle(); });

    connect(&PlaylistManager::instance(), &PlaylistManager::playlistChanged, this,
            &RecentPage::updatePlayingHighlight);
    connect(&PlaylistManager::instance(), &PlaylistManager::currentIndexChanged, this,
            &RecentPage::updatePlayingHighlight);
}

void RecentPage::setupUi()
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

    m_titleLbl = new QLabel(I18n::instance().tr(QStringLiteral("recentPlay")), titleRow);
    titleLay->addWidget(m_titleLbl);

    m_countLbl = new QLabel(titleRow);
    titleLay->addWidget(m_countLbl);
    titleLay->addStretch();
    headerLay->addWidget(titleRow);

    auto *menuRow = new QWidget(m_header);
    menuRow->setFixedHeight(40);
    auto *menuLay = new QHBoxLayout(menuRow);
    menuLay->setContentsMargins(0, 0, 0, 0);
    menuLay->setSpacing(12);

    m_playBtn = new QPushButton(I18n::instance().tr(QStringLiteral("play")), menuRow);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setFixedHeight(40);
    connect(m_playBtn, &QPushButton::clicked, this, [this]() {
        if (!m_allRecent.isEmpty())
            emit playAllRequested(m_allRecent);
    });
    menuLay->addWidget(m_playBtn);

    m_downloadAllBtn = new QPushButton(I18n::instance().tr(QStringLiteral("downloadAll")), menuRow);
    m_downloadAllBtn->setCursor(Qt::PointingHandCursor);
    m_downloadAllBtn->setFixedHeight(40);
    connect(m_downloadAllBtn, &QPushButton::clicked, this, [this]() {
        if (!m_allRecent.isEmpty())
            emit downloadAllRequested(m_allRecent);
    });
    menuLay->addWidget(m_downloadAllBtn);

    m_clearBtn = new QPushButton(I18n::instance().tr(QStringLiteral("clearRecentList")), menuRow);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setFixedHeight(40);
    connect(m_clearBtn, &QPushButton::clicked, this, &RecentPage::confirmClearRecent);
    menuLay->addWidget(m_clearBtn);

    menuLay->addStretch();
    headerLay->addWidget(menuRow);
    root->addWidget(m_header);

    m_songList = new SongListWidget(this);
    m_songList->onSongActivate = [this](const MusicInfo &info) { emit playRequested(info); };
    m_songList->onSongPlayNext = [this](const MusicInfo &info) { emit playRequested(info); };
    m_songList->onUnfavorite = [this](int id) { emit favoriteRequested(id); };
    // 该页的“收藏”是切换语义：统一菜单里作为「添加/取消收藏」双向入口复用
    m_songList->onToggleFavorite = [this](const MusicInfo &info) { emit favoriteRequested(info.id); };
    m_songList->isFavorited = [this](int id) { return m_favoritedIds.contains(id); };
    m_songList->onTogglePlayPause = [this]() { emit playPauseRequested(); };
    m_songList->onDownload = [this](const MusicInfo &info) { emit downloadRequested(info); };
    m_songList->isDownloaded = [](int id) { return MusicDownloadManager::instance().isDownloaded(id); };
    root->addWidget(m_songList, 1);

    m_emptyWrap = new QWidget(this);
    auto *emptyLay = new QVBoxLayout(m_emptyWrap);
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
    m_emptyWrap->hide();
    root->addWidget(m_emptyWrap);

    applyPageStyle();
    loadRecentPlays();
}

void RecentPage::applyPageStyle()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString titleFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QString metaFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.72)");
    const QString statusFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.55)");

    if (m_titleLbl) {
        m_titleLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 30px; font-weight: 700; color: %1; }").arg(titleFg));
    }
    if (m_countLbl) {
        m_countLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 15px; font-weight: 400; color: %1; padding-bottom: 2px; }")
                                      .arg(metaFg));
    }
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
    if (m_downloadAllBtn) {
        m_downloadAllBtn->setIcon(Icons::renderNamed("Download", 18, secondaryIc));
        m_downloadAllBtn->setIconSize(QSize(18, 18));
        m_downloadAllBtn->setStyleSheet(QStringLiteral(
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

void RecentPage::retranslate()
{
    if (m_titleLbl)
        m_titleLbl->setText(I18n::instance().tr(QStringLiteral("recentPlay")));
    if (m_playBtn)
        m_playBtn->setText(I18n::instance().tr(QStringLiteral("play")));
    if (m_downloadAllBtn)
        m_downloadAllBtn->setText(I18n::instance().tr(QStringLiteral("downloadAll")));
    if (m_clearBtn)
        m_clearBtn->setText(I18n::instance().tr(QStringLiteral("clearRecentList")));
    if (m_songList)
        m_songList->retranslate();
    updateHeaderMeta();
    applyPageStyle();
}

void RecentPage::refresh()
{
    loadRecentPlays();
}

void RecentPage::setPlaybackPaused(bool paused)
{
    if (m_songList)
        m_songList->setPlaybackPaused(paused);
}

void RecentPage::setFavoritedMusicIds(const QSet<int> &ids)
{
    m_favoritedIds = ids;
    if (m_songList)
        m_songList->refreshFavoriteDisplay();
}

void RecentPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updatePlayingHighlight();
}

void RecentPage::updateHeaderMeta()
{
    if (m_countLbl) {
        m_countLbl->setText(I18n::instance().tr(QStringLiteral("favoritePageSongCount"))
                                .arg(m_allRecent.size()));
    }
    const bool hasSongs = !m_allRecent.isEmpty();
    if (m_playBtn)
        m_playBtn->setEnabled(hasSongs);
    if (m_downloadAllBtn)
        m_downloadAllBtn->setEnabled(hasSongs);
    if (m_clearBtn)
        m_clearBtn->setEnabled(hasSongs);
}

void RecentPage::refreshDownloadDisplay()
{
    if (m_songList)
        m_songList->refreshDownloadDisplay();
}

int RecentPage::currentPlayingMusicId() const
{
    const auto &mgr = PlaylistManager::instance();
    const int idx = mgr.currentIndex();
    if (idx < 0 || idx >= mgr.playlist().size())
        return -1;
    return mgr.playlist()[idx].id;
}

void RecentPage::updatePlayingHighlight()
{
    if (m_songList)
        m_songList->setCurrentPlayingId(currentPlayingMusicId());
}

void RecentPage::showPageStatus(const QString &text, const char *iconName)
{
    if (m_emptyIcon) {
        if (iconName) {
            const bool dark = Theme::ThemeManager::instance().isDarkMode();
            const QColor ic = dark ? QColor(244, 246, 255, 90) : QColor(33, 37, 41, 90);
            m_emptyIcon->setPixmap(Icons::renderNamed(iconName, 64, ic));
            m_emptyIcon->show();
        } else {
            m_emptyIcon->hide();
        }
    }
    if (m_statusLabel) {
        m_statusLabel->setText(text);
        m_statusLabel->show();
    }
    if (m_emptyWrap)
        m_emptyWrap->show();
    if (m_songList)
        m_songList->hide();
}

void RecentPage::hidePageStatus()
{
    if (m_emptyIcon)
        m_emptyIcon->hide();
    if (m_statusLabel)
        m_statusLabel->hide();
    if (m_emptyWrap)
        m_emptyWrap->hide();
}

void RecentPage::confirmClearRecent()
{
    if (m_allRecent.isEmpty())
        return;

    const auto ret = QMessageBox::question(
        this, I18n::instance().tr(QStringLiteral("clearRecentList")),
        I18n::instance().tr(QStringLiteral("clearRecentConfirm")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    PlaylistDatabase::instance().clearRecentPlays();
    Toast::show(this, I18n::instance().tr(QStringLiteral("clearRecentSuccess")), Toast::Success);
    loadRecentPlays();
}

void RecentPage::loadRecentPlays()
{
    m_allRecent = PlaylistDatabase::instance().getRecentPlays();
    updateHeaderMeta();

    if (m_allRecent.isEmpty()) {
        if (m_songList)
            m_songList->setSongs({});
        showPageStatus(I18n::instance().tr(QStringLiteral("emptyRecent")), "SearchOff");
        return;
    }

    hidePageStatus();
    if (m_songList) {
        m_songList->setSongs(m_allRecent);
        m_songList->refreshFavoriteDisplay();
        m_songList->refreshDownloadDisplay();
        m_songList->show();
    }
    updatePlayingHighlight();
}

void RecentPage::paintEvent(QPaintEvent *)
{
}
