/**
 * @file artistdetailpage.cpp
 * @brief 歌手详情子页
 */

#include "artistdetailpage.h"
#include "songlistwidget.h"
#include "core/i18n.h"
#include "core/musicdownloadmanager.h"
#include "core/playlistmanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>

ArtistDetailPage::ArtistDetailPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("artistDetailPage"));
    setupUi();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) { applyPageStyle(); });
    connect(&PlaylistManager::instance(), &PlaylistManager::playlistChanged, this,
            &ArtistDetailPage::updatePlayingHighlight);
    connect(&PlaylistManager::instance(), &PlaylistManager::currentIndexChanged, this,
            &ArtistDetailPage::updatePlayingHighlight);
}

void ArtistDetailPage::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 12, 24, 24);
    root->setSpacing(0);

    m_topBar = new QWidget(this);
    m_topBar->setFixedHeight(48);
    auto *topLay = new QHBoxLayout(m_topBar);
    topLay->setContentsMargins(0, 0, 0, 0);
    topLay->setSpacing(12);

    m_backBtn = new QPushButton(m_topBar);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setFixedHeight(36);
    connect(m_backBtn, &QPushButton::clicked, this, &ArtistDetailPage::backRequested);
    topLay->addWidget(m_backBtn);
    topLay->addStretch();
    root->addWidget(m_topBar);

    m_header = new QWidget(this);
    auto *headerLay = new QVBoxLayout(m_header);
    headerLay->setContentsMargins(0, 0, 0, 0);
    headerLay->setSpacing(12);

    auto *titleRow = new QWidget(m_header);
    auto *titleLay = new QHBoxLayout(titleRow);
    titleLay->setContentsMargins(0, 0, 0, 0);
    titleLay->setSpacing(8);
    titleLay->setAlignment(Qt::AlignBottom);

    m_titleLbl = new QLabel(titleRow);
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

    m_playBtn = new QPushButton(menuRow);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setFixedHeight(40);
    connect(m_playBtn, &QPushButton::clicked, this, [this]() {
        if (!m_tracks.isEmpty())
            emit playAllRequested(m_tracks);
    });
    menuLay->addWidget(m_playBtn);

    m_downloadAllBtn = new QPushButton(I18n::instance().tr(QStringLiteral("downloadAll")), menuRow);
    m_downloadAllBtn->setCursor(Qt::PointingHandCursor);
    m_downloadAllBtn->setFixedHeight(40);
    connect(m_downloadAllBtn, &QPushButton::clicked, this, [this]() {
        if (!m_tracks.isEmpty())
            emit downloadAllRequested(m_tracks);
    });
    menuLay->addWidget(m_downloadAllBtn);
    menuLay->addStretch();
    headerLay->addWidget(menuRow);
    root->addWidget(m_header);

    m_songList = new SongListWidget(this);
    m_songList->onSongActivate = [this](const MusicInfo &info) { emit playMusic(info); };
    m_songList->onSongPlayNext = [this](const MusicInfo &info) { emit playMusic(info); };
    m_songList->onUnfavorite = [this](int id) { emit favoriteRequested(id); };
    // 该页的“收藏”是切换语义：统一菜单里作为「添加/取消收藏」双向入口复用
    m_songList->onToggleFavorite = [this](const MusicInfo &info) { emit favoriteRequested(info.id); };
    m_songList->onDownload = [this](const MusicInfo &info) { emit downloadRequested(info); };
    m_songList->isFavorited = [this](int id) { return m_favoritedIds.contains(id); };
    m_songList->isDownloaded = [](int id) { return MusicDownloadManager::instance().isDownloaded(id); };
    m_songList->onTogglePlayPause = [this]() { emit playPauseRequested(); };
    root->addWidget(m_songList, 1);

    applyPageStyle();
}

void ArtistDetailPage::applyPageStyle()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString titleFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QString metaFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.72)");

    if (m_backBtn) {
        m_backBtn->setText(QStringLiteral("← ") + I18n::instance().tr(QStringLiteral("searchBackToSearch")));
        m_backBtn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: transparent;"
            "  border: none;"
            "  color: %1;"
            "  font-size: 14px;"
            "  font-weight: 500;"
            "  padding: 0 12px 0 4px;"
            "}"
            "QPushButton:hover { color: #D84B63; }")
                                     .arg(titleFg));
    }
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
        m_playBtn->setText(I18n::instance().tr(QStringLiteral("play")));
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
        m_downloadAllBtn->setText(I18n::instance().tr(QStringLiteral("downloadAll")));
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
    if (m_songList)
        m_songList->applyTheme();
}

QList<MusicInfo> ArtistDetailPage::tracksFromArtistMap(const QVariantMap &artist)
{
    QList<MusicInfo> list;
    const QVariantList musicList = artist.value(QStringLiteral("musicList")).toList();
    for (const auto &v : musicList) {
        const QVariantMap m = v.toMap();
        if (m.isEmpty())
            continue;
        MusicInfo info;
        info.id = m.value(QStringLiteral("id")).toInt();
        info.title = m.value(QStringLiteral("title")).toString();
        info.artist = m.value(QStringLiteral("artist")).toString();
        info.album = m.value(QStringLiteral("album")).toString();
        info.duration = m.value(QStringLiteral("duration")).toInt();
        info.maxQuality = m.value(QStringLiteral("maxQuality")).toString();
        info.coverUrl = QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(info.id);
        list.append(info);
    }
    return list;
}

void ArtistDetailPage::loadArtist(const QVariantMap &artist)
{
    m_artistName = artist.value(QStringLiteral("name")).toString();
    m_tracks = tracksFromArtistMap(artist);
    if (m_titleLbl)
        m_titleLbl->setText(m_artistName.isEmpty() ? I18n::instance().tr(QStringLiteral("artist"))
                                                   : m_artistName);
    updateHeaderMeta();
    if (m_songList) {
        m_songList->setSongs(m_tracks);
        m_songList->refreshDownloadDisplay();
    }
    updatePlayingHighlight();
}

void ArtistDetailPage::refreshDownloadDisplay()
{
    if (m_songList)
        m_songList->refreshDownloadDisplay();
}

void ArtistDetailPage::updateHeaderMeta()
{
    if (m_countLbl) {
        m_countLbl->setText(I18n::instance().tr(QStringLiteral("favoritePageSongCount"))
                                .arg(m_tracks.size()));
    }
    if (m_playBtn)
        m_playBtn->setEnabled(!m_tracks.isEmpty());
    if (m_downloadAllBtn)
        m_downloadAllBtn->setEnabled(!m_tracks.isEmpty());
}

int ArtistDetailPage::currentPlayingMusicId() const
{
    const auto &mgr = PlaylistManager::instance();
    const int idx = mgr.currentIndex();
    if (idx < 0 || idx >= mgr.playlist().size())
        return -1;
    return mgr.playlist().at(idx).id;
}

void ArtistDetailPage::updatePlayingHighlight()
{
    if (m_songList)
        m_songList->setCurrentPlayingId(currentPlayingMusicId());
}

void ArtistDetailPage::setFavoritedMusicIds(const QSet<int> &ids)
{
    m_favoritedIds = ids;
    if (m_songList)
        m_songList->refreshFavoriteDisplay();
}

void ArtistDetailPage::setPlaybackPaused(bool paused)
{
    if (m_songList)
        m_songList->setPlaybackPaused(paused);
}

void ArtistDetailPage::retranslate()
{
    if (m_backBtn)
        m_backBtn->setText(QStringLiteral("← ") + I18n::instance().tr(QStringLiteral("searchBackToSearch")));
    if (m_playBtn)
        m_playBtn->setText(I18n::instance().tr(QStringLiteral("play")));
    if (m_songList)
        m_songList->retranslate();
    if (m_titleLbl && !m_artistName.isEmpty())
        m_titleLbl->setText(m_artistName);
    updateHeaderMeta();
    applyPageStyle();
}

void ArtistDetailPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updatePlayingHighlight();
}
