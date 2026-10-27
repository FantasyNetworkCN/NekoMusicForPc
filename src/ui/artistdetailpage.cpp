/**
 * @file artistdetailpage.cpp
 * @brief 歌手详情子页
 */

#include "artistdetailpage.h"
#include "songlistwidget.h"
#include "batchactionbar.h"
#include "core/i18n.h"
#include "core/musicdownloadmanager.h"
#include "core/playlistmanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"
#include "ui/toast.h"
#include "ui/headeractionmenu.h"

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
    m_menuRow = menuRow;
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

    m_moreBtn = new QPushButton(menuRow);
    m_moreBtn->setFixedSize(40, 40);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setFlat(true);
    m_moreBtn->setToolTip(I18n::instance().tr(QStringLiteral("moreActions")));
    connect(m_moreBtn, &QPushButton::clicked, this, &ArtistDetailPage::showMoreMenu);
    menuLay->addWidget(m_moreBtn);

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

    setupBatchBar();

    applyPageStyle();
}

void ArtistDetailPage::showMoreMenu()
{
    const bool hasSongs = !m_tracks.isEmpty();
    HeaderActionMenu menu(this);
    QAction *downloadAll = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("downloadAll")), "Download", hasSongs);
    QAction *addAll = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("addAllToQueue")), "MusicList", hasSongs);
    menu.addSeparator();
    QAction *batchManage = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("batchManage")), "Batch", hasSongs);

    QAction *picked = menu.exec(m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
    if (picked == downloadAll)
        emit downloadAllRequested(m_tracks);
    else if (picked == addAll)
        addAllToQueue();
    else if (picked == batchManage)
        enterBatchMode();
}

/** 批量编辑模式：顶部按钮排 ↔ 批量操作条 的公共接线 */
void ArtistDetailPage::setupBatchBar()
{
    if (!m_menuRow || !m_songList)
        return;

    m_batchBar = new BatchActionBar(m_menuRow->parentWidget());
    m_batchBar->setCapabilities(true, true, true, true, false);
    m_batchBar->setActionMenuMode(true);
    m_batchBar->hide();
    if (auto *lay = qobject_cast<QBoxLayout *>(m_menuRow->parentWidget()->layout()))
        lay->insertWidget(lay->indexOf(m_menuRow), m_batchBar);

    connect(m_batchBar, &BatchActionBar::exitRequested, this, &ArtistDetailPage::exitBatchMode);
    connect(m_batchBar, &BatchActionBar::selectAllToggled, this, [this]() {
        if (m_songList)
            m_songList->setBatchSelectAll(!m_songList->batchAllSelected());
    });
    connect(m_batchBar, &BatchActionBar::addToPlaylistRequested, this, [this]() {
        const QList<MusicInfo> picked = m_songList ? m_songList->batchSelectedSongs() : QList<MusicInfo>();
        if (picked.isEmpty())
            return;
        emit batchAddToPlaylistRequested(picked);
        exitBatchMode(); // 动作已派发：立刻退出并清空选择，避免重复点重复请求
    });
    connect(m_batchBar, &BatchActionBar::downloadRequested, this, [this]() {
        const QList<MusicInfo> picked = m_songList ? m_songList->batchSelectedSongs() : QList<MusicInfo>();
        if (picked.isEmpty())
            return;
        emit batchDownloadRequested(picked);
        exitBatchMode();
    });
    connect(m_batchBar, &BatchActionBar::unfavoriteRequested, this, [this]() {
        QList<int> ids;
        if (m_songList) {
            const QList<MusicInfo> picked = m_songList->batchSelectedSongs();
            for (const MusicInfo &info : picked)
                ids.append(info.id);
        }
        if (ids.isEmpty())
            return;
        emit batchFavoriteToggleRequested(ids);
        exitBatchMode();
    });
    connect(m_batchBar, &BatchActionBar::addToQueueRequested, this, [this]() {
        if (!m_songList)
            return;
        const QList<MusicInfo> picked = m_songList->batchSelectedSongs();
        if (picked.isEmpty())
            return;
        PlaylistManager::instance().addAllToPlaylist(picked);
        Toast::show(this, I18n::instance().tr(QStringLiteral("batchQueueAdded")).arg(picked.size()),
                    Toast::Success);
        exitBatchMode();
    });
    connect(m_songList, &SongListWidget::batchSelectionChanged, this,
            &ArtistDetailPage::onBatchSelectionChanged);
}

void ArtistDetailPage::enterBatchMode()
{
    if (!m_songList || !m_batchBar || m_tracks.isEmpty())
        return;

    m_songList->clearBatchSelection();
    m_songList->setBatchMode(true);
    m_batchBar->reset();
    m_menuRow->hide();
    m_batchBar->show();
}

void ArtistDetailPage::exitBatchMode()
{
    if (!m_songList || !m_batchBar)
        return;

    m_songList->setBatchMode(false);
    m_batchBar->reset();
    m_batchBar->hide();
    if (m_menuRow)
        m_menuRow->show();
}

void ArtistDetailPage::onBatchSelectionChanged(int count)
{
    if (!m_batchBar || !m_songList)
        return;
    m_batchBar->setSelectedCount(count);
    m_batchBar->setAllSelected(m_songList->batchAllSelected());
}

/** 一键把当前歌手的全部曲目加入本地播放队列 */
void ArtistDetailPage::addAllToQueue()
{
    if (m_tracks.isEmpty()) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("queueAddAllEmpty")), Toast::Info);
        return;
    }

    auto &mgr = PlaylistManager::instance();
    const int before = mgr.count();
    mgr.addAllToPlaylist(m_tracks);
    const int added = qMax(0, mgr.count() - before);

    if (added <= 0)
        Toast::show(this, I18n::instance().tr(QStringLiteral("queueAddAllNone")), Toast::Info);
    else
        Toast::show(this, I18n::instance().tr(QStringLiteral("addAllToQueueDone")).arg(added),
                    Toast::Success);
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
    const QColor secondaryIc = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
    if (m_batchBar)
        m_batchBar->applyTheme();
    if (m_moreBtn) {
        m_moreBtn->setIcon(Icons::renderNamed("More", 20, secondaryIc));
        m_moreBtn->setIconSize(QSize(20, 20));
        m_moreBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: none; border-radius: 20px; }"
            "QPushButton:hover { background: rgba(230,57,80,0.15); }").arg(secondaryBg));
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

    const bool hasSongs = !m_tracks.isEmpty();
    const bool batchActive = m_songList && m_songList->batchMode();
    if (!hasSongs && batchActive)
        exitBatchMode();
    // 批量模式下按钮排已让位给批量操作条，别被这里又显示出来
    else if (m_menuRow && batchActive)
        m_menuRow->hide();
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
    if (m_moreBtn)
        m_moreBtn->setToolTip(I18n::instance().tr(QStringLiteral("moreActions")));
    if (m_batchBar)
        m_batchBar->retranslate();
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
