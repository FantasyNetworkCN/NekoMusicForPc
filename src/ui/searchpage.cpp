/**
 * @file searchpage.cpp
 * @brief 搜索页 — 1:1 SPlayer Search/layout.vue + songs/playlists/artists
 */

#include "searchpage.h"
#include "songlistwidget.h"
#include "coverlistcard.h"
#include "core/apiclient.h"
#include "core/i18n.h"
#include "core/covercache.h"
#include "core/musicdownloadmanager.h"
#include "core/playlistmanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"
#include "ui/scrollareafix.h"
#include "ui/songcontextmenu.h"
#include "ui/songmenubuilder.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QScrollArea>
#include <QButtonGroup>
#include <QShowEvent>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

class SearchArtistTile : public QWidget
{
public:
    explicit SearchArtistTile(const QVariantMap &artist, QWidget *parent = nullptr)
        : QWidget(parent), m_artist(artist)
    {
        setCursor(Qt::PointingHandCursor);
        setFixedSize(120, 160);
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(8, 12, 8, 8);
        lay->setSpacing(8);
        lay->setAlignment(Qt::AlignHCenter);

        m_cover = new QLabel(this);
        m_cover->setFixedSize(96, 96);
        m_cover->setAlignment(Qt::AlignCenter);
        loadAvatar();
        lay->addWidget(m_cover, 0, Qt::AlignHCenter);

        const QString name = artist.value(QStringLiteral("name")).toString();
        auto *nameLbl = new QLabel(name, this);
        nameLbl->setAlignment(Qt::AlignCenter);
        nameLbl->setWordWrap(true);
        nameLbl->setMaximumHeight(36);
        nameLbl->setStyleSheet(QStringLiteral("QLabel { font-size: 13px; font-weight: 600; }"));
        lay->addWidget(nameLbl);

        const int count = artist.value(QStringLiteral("musicCount")).toInt();
        if (count > 0) {
            auto *countLbl = new QLabel(
                I18n::instance().tr(QStringLiteral("favoritePageSongCount")).arg(count), this);
            countLbl->setAlignment(Qt::AlignCenter);
            countLbl->setStyleSheet(QStringLiteral("QLabel { font-size: 11px; color: rgba(128,128,128,0.9); }"));
            lay->addWidget(countLbl);
        }
    }

    std::function<void(const QVariantMap &)> onClicked;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onClicked)
            onClicked(m_artist);
        QWidget::mousePressEvent(e);
    }

    void enterEvent(QEnterEvent *) override
    {
        setStyleSheet(QStringLiteral("SearchArtistTile { background: rgba(230,57,80,0.08); border-radius: 16px; }"));
    }

    void leaveEvent(QEvent *) override { setStyleSheet(QString()); }

private:
    void loadAvatar()
    {
        QPixmap pix(96, 96);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addEllipse(0, 0, 96, 96);
        p.fillPath(path, QColor(128, 128, 128, 50));
        p.setClipPath(path);
        p.drawPixmap(36, 36, Icons::renderNamed("Artist", 24, QColor(255, 255, 255, 160)));
        m_cover->setPixmap(pix);
    }

    QVariantMap m_artist;
    QLabel *m_cover = nullptr;
};

QString playlistCoverUrl(const QVariantMap &pl)
{
    QString first = pl.value(QStringLiteral("firstMusicCover")).toString();
    if (!first.isEmpty())
        return CoverCache::resolveCoverUrl(first);
    const int musicId = pl.value(QStringLiteral("firstMusicId")).toInt();
    if (musicId <= 0)
        return QString();
    return QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(musicId);
}

QList<MusicInfo> tracksFromArtistMap(const QVariantMap &artist)
{
    QList<MusicInfo> list;
    QVariantList musicList = artist.value(QStringLiteral("musicList")).toList();
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

} // namespace

SearchPage::SearchPage(ApiClient *apiClient, QWidget *parent)
    : QWidget(parent), m_apiClient(apiClient)
{
    setObjectName(QStringLiteral("searchPage"));
    setupUi();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) { applyPageStyle(); });
    connect(&PlaylistManager::instance(), &PlaylistManager::playlistChanged, this,
            &SearchPage::updatePlayingHighlight);
    connect(&PlaylistManager::instance(), &PlaylistManager::currentIndexChanged, this,
            &SearchPage::updatePlayingHighlight);
}

void SearchPage::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 12, 24, 24);
    root->setSpacing(0);

    auto *titleRow = new QWidget(this);
    auto *titleLay = new QHBoxLayout(titleRow);
    titleLay->setContentsMargins(0, 0, 0, 0);
    titleLay->setSpacing(8);
    titleLay->setAlignment(Qt::AlignBottom);

    m_keywordLbl = new QLabel(titleRow);
    m_suffixLbl = new QLabel(I18n::instance().tr(QStringLiteral("searchRelatedSuffix")), titleRow);
    titleLay->addWidget(m_keywordLbl);
    titleLay->addWidget(m_suffixLbl);
    titleLay->addStretch();
    root->addWidget(titleRow);

    auto *tabBar = new QWidget(this);
    tabBar->setObjectName(QStringLiteral("searchTabBar"));
    auto *tabLay = new QHBoxLayout(tabBar);
    tabLay->setContentsMargins(0, 12, 0, 12);
    tabLay->setSpacing(0);

    auto *tabGroup = new QButtonGroup(this);
    tabGroup->setExclusive(true);

    auto makeTab = [&](const QString &key) {
        auto *btn = new QPushButton(I18n::instance().tr(key), tabBar);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(32);
        tabGroup->addButton(btn);
        tabLay->addWidget(btn);
        return btn;
    };

    m_tabSongs = makeTab(QStringLiteral("searchMusic"));
    m_tabPlaylists = makeTab(QStringLiteral("searchPlaylist"));
    m_tabArtists = makeTab(QStringLiteral("artist"));
    m_tabSongs->setChecked(true);
    tabLay->addStretch();

    connect(m_tabSongs, &QPushButton::clicked, this, [this]() { setActiveTab(Songs); });
    connect(m_tabPlaylists, &QPushButton::clicked, this, [this]() { setActiveTab(Playlists); });
    connect(m_tabArtists, &QPushButton::clicked, this, [this]() { setActiveTab(Artists); });
    root->addWidget(tabBar);

    m_tabStack = new QStackedWidget(this);

    // ── 单曲（SongList） ──
    auto *songsPage = new QWidget(m_tabStack);
    auto *songsLay = new QVBoxLayout(songsPage);
    songsLay->setContentsMargins(0, 0, 0, 0);
    songsLay->setSpacing(0);

    m_songsActionRow = new QWidget(songsPage);
    m_songsActionRow->setFixedHeight(40);
    auto *songsActionLay = new QHBoxLayout(m_songsActionRow);
    songsActionLay->setContentsMargins(0, 0, 0, 12);
    songsActionLay->setSpacing(12);

    m_songsPlayAllBtn = new QPushButton(I18n::instance().tr(QStringLiteral("playAll")), m_songsActionRow);
    m_songsPlayAllBtn->setCursor(Qt::PointingHandCursor);
    m_songsPlayAllBtn->setFixedHeight(40);
    connect(m_songsPlayAllBtn, &QPushButton::clicked, this, [this]() {
        if (!m_musicResults.isEmpty()) {
            PlaylistManager::instance().clearPlaylist();
            PlaylistManager::instance().addAllToPlaylist(m_musicResults);
            emit playMusic(m_musicResults.first());
        }
    });
    songsActionLay->addWidget(m_songsPlayAllBtn);

    m_songsDownloadAllBtn = new QPushButton(I18n::instance().tr(QStringLiteral("downloadAll")),
                                            m_songsActionRow);
    m_songsDownloadAllBtn->setCursor(Qt::PointingHandCursor);
    m_songsDownloadAllBtn->setFixedHeight(40);
    connect(m_songsDownloadAllBtn, &QPushButton::clicked, this, [this]() {
        if (!m_musicResults.isEmpty())
            emit downloadAllRequested(m_musicResults);
    });
    songsActionLay->addWidget(m_songsDownloadAllBtn);
    songsActionLay->addStretch();
    m_songsActionRow->hide();
    songsLay->addWidget(m_songsActionRow);

    m_songList = new SongListWidget(songsPage);
    m_songList->onSongActivate = [this](const MusicInfo &info) { emit playMusic(info); };
    m_songList->onSongPlayNext = [this](const MusicInfo &info) { emit playMusic(info); };
    m_songList->onSongContextMenu = [this](const MusicInfo &info, const QPoint &pos) {
        showSongContextMenu(info, pos);
    };
    m_songList->onUnfavorite = [this](int id) { emit favoriteRequested(id); };
    m_songList->onDownload = [this](const MusicInfo &info) { emit downloadRequested(info); };
    m_songList->isFavorited = [this](int id) { return m_favoritedIds.contains(id); };
    m_songList->isDownloaded = [](int id) { return MusicDownloadManager::instance().isDownloaded(id); };
    m_songList->onTogglePlayPause = [this]() { emit playPauseRequested(); };
    connect(m_songList, &SongListWidget::scrolled, this, &SearchPage::onSongListScrolled);
    songsLay->addWidget(m_songList, 1);

    m_songsEmptyWrap = new QWidget(songsPage);
    auto *songsEmptyLay = new QVBoxLayout(m_songsEmptyWrap);
    songsEmptyLay->setAlignment(Qt::AlignCenter);
    songsEmptyLay->setSpacing(12);
    m_songsEmptyIcon = new QLabel(m_songsEmptyWrap);
    m_songsEmptyIcon->setAlignment(Qt::AlignCenter);
    songsEmptyLay->addWidget(m_songsEmptyIcon);
    m_songsEmptyLbl = new QLabel(m_songsEmptyWrap);
    m_songsEmptyLbl->setAlignment(Qt::AlignCenter);
    m_songsEmptyLbl->setWordWrap(true);
    songsEmptyLay->addWidget(m_songsEmptyLbl);
    m_songsEmptyWrap->hide();
    songsLay->addWidget(m_songsEmptyWrap);

    m_loadMoreBtn = new QPushButton(songsPage);
    m_loadMoreBtn->setCursor(Qt::PointingHandCursor);
    m_loadMoreBtn->setFixedHeight(40);
    m_loadMoreBtn->hide();
    connect(m_loadMoreBtn, &QPushButton::clicked, this, [this]() { fetchMusicResults(true); });
    songsLay->addWidget(m_loadMoreBtn, 0, Qt::AlignHCenter);

    m_tabStack->addWidget(songsPage);

    // ── 歌单（CoverList 网格） ──
    auto *plPage = new QWidget(m_tabStack);
    auto *plLay = new QVBoxLayout(plPage);
    plLay->setContentsMargins(0, 0, 0, 0);

    m_playlistScroll = new QScrollArea(plPage);
    m_playlistScroll->setObjectName(QStringLiteral("searchScroll"));
    m_playlistScroll->setWidgetResizable(true);
    m_playlistScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_playlistScroll->setFrameShape(QFrame::NoFrame);

    m_playlistGridHost = new CoverGridHost(m_playlistScroll);
    m_playlistGridHost->onResized = [this]() { relayoutPlaylistGrid(); };
    auto *plGridOuter = new QVBoxLayout(m_playlistGridHost);
    plGridOuter->setContentsMargins(4, 20, 4, 20);
    plGridOuter->setSpacing(0);
    m_playlistGridInner = new QWidget(m_playlistGridHost);
    m_playlistGridInner->setObjectName(QStringLiteral("searchCoverGridInner"));
    plGridOuter->addWidget(m_playlistGridInner);
    plGridOuter->addStretch();
    m_playlistScroll->setWidget(m_playlistGridHost);
    nekoPolishScrollAreaViewport(m_playlistScroll);
    plLay->addWidget(m_playlistScroll, 1);

    m_playlistEmptyWrap = new QWidget(plPage);
    auto *plEmptyLay = new QVBoxLayout(m_playlistEmptyWrap);
    plEmptyLay->setAlignment(Qt::AlignCenter);
    plEmptyLay->setSpacing(12);
    m_playlistEmptyIcon = new QLabel(m_playlistEmptyWrap);
    m_playlistEmptyIcon->setAlignment(Qt::AlignCenter);
    plEmptyLay->addWidget(m_playlistEmptyIcon);
    m_playlistEmptyLbl = new QLabel(m_playlistEmptyWrap);
    m_playlistEmptyLbl->setAlignment(Qt::AlignCenter);
    m_playlistEmptyLbl->setWordWrap(true);
    plEmptyLay->addWidget(m_playlistEmptyLbl);
    m_playlistEmptyWrap->hide();
    plLay->addWidget(m_playlistEmptyWrap);

    m_tabStack->addWidget(plPage);

    // ── 歌手（ArtistList 网格，点击进入独立子页） ──
    auto *arPage = new QWidget(m_tabStack);
    auto *arLay = new QVBoxLayout(arPage);
    arLay->setContentsMargins(0, 0, 0, 0);

    m_artistGridScroll = new QScrollArea(arPage);
    m_artistGridScroll->setObjectName(QStringLiteral("searchScroll"));
    m_artistGridScroll->setWidgetResizable(true);
    m_artistGridScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_artistGridScroll->setFrameShape(QFrame::NoFrame);

    m_artistGridHost = new QWidget(m_artistGridScroll);
    m_artistGrid = new QGridLayout(m_artistGridHost);
    m_artistGrid->setContentsMargins(4, 20, 4, 20);
    m_artistGrid->setSpacing(20);
    m_artistGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_artistGridScroll->setWidget(m_artistGridHost);
    nekoPolishScrollAreaViewport(m_artistGridScroll);

    m_artistEmptyWrap = new QWidget(arPage);
    auto *arEmptyLay = new QVBoxLayout(m_artistEmptyWrap);
    arEmptyLay->setAlignment(Qt::AlignCenter);
    arEmptyLay->setSpacing(12);
    m_artistEmptyIcon = new QLabel(m_artistEmptyWrap);
    m_artistEmptyIcon->setAlignment(Qt::AlignCenter);
    arEmptyLay->addWidget(m_artistEmptyIcon);
    m_artistEmptyLbl = new QLabel(m_artistEmptyWrap);
    m_artistEmptyLbl->setAlignment(Qt::AlignCenter);
    m_artistEmptyLbl->setWordWrap(true);
    arEmptyLay->addWidget(m_artistEmptyLbl);
    m_artistEmptyWrap->hide();
    arLay->addWidget(m_artistEmptyWrap);
    arLay->addWidget(m_artistGridScroll, 1);
    m_tabStack->addWidget(arPage);

    root->addWidget(m_tabStack, 1);

    m_hintWrap = new QWidget(this);
    auto *hintLay = new QVBoxLayout(m_hintWrap);
    hintLay->setAlignment(Qt::AlignCenter);
    m_hintLbl = new QLabel(m_hintWrap);
    m_hintLbl->setAlignment(Qt::AlignCenter);
    m_hintLbl->setWordWrap(true);
    hintLay->addWidget(m_hintLbl);
    root->addWidget(m_hintWrap);

    applyPageStyle();
    showHintState(true);
}

void SearchPage::applyPageStyle()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString titleFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QString metaFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.72)");
    const QString statusFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.55)");
    const QString tabBg = dark ? QStringLiteral("#2a2a2a") : QStringLiteral("#f0f0f0");
    const QString tabSelBg = dark ? QStringLiteral("#3a3a3a") : QStringLiteral("#ffffff");

    if (m_keywordLbl) {
        m_keywordLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 36px; font-weight: 700; color: %1; }").arg(titleFg));
    }
    if (m_suffixLbl) {
        m_suffixLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 22px; color: %1; padding-bottom: 4px; }").arg(metaFg));
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

    if (auto *bar = findChild<QWidget *>(QStringLiteral("searchTabBar"))) {
        bar->setStyleSheet(QStringLiteral("QWidget#searchTabBar { background: %1; border-radius: 10px; }")
                              .arg(tabBg));
    }
    if (m_tabSongs)
        m_tabSongs->setStyleSheet(tabStyle);
    if (m_tabPlaylists)
        m_tabPlaylists->setStyleSheet(tabStyle);
    if (m_tabArtists)
        m_tabArtists->setStyleSheet(tabStyle);

    const QString emptyStyle = QStringLiteral("QLabel { font-size: 14px; color: %1; padding: 60px 24px; }")
                                 .arg(statusFg);
    if (m_songsEmptyLbl)
        m_songsEmptyLbl->setStyleSheet(emptyStyle);
    if (m_playlistEmptyLbl)
        m_playlistEmptyLbl->setStyleSheet(emptyStyle);
    if (m_artistEmptyLbl)
        m_artistEmptyLbl->setStyleSheet(emptyStyle);
    if (m_hintLbl)
        m_hintLbl->setStyleSheet(emptyStyle);

    if (m_loadMoreBtn) {
        m_loadMoreBtn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: %1;"
            "  color: %2;"
            "  border: none;"
            "  border-radius: 20px;"
            "  font-size: 14px;"
            "  font-weight: 500;"
            "  padding: 0 24px;"
            "}"
            "QPushButton:hover { background: rgba(230,57,80,0.15); }"
            "QPushButton:disabled { color: rgba(128,128,128,0.8); }")
                                         .arg(tabBg, titleFg));
    }

    const QColor secondaryIc = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
    if (m_songsPlayAllBtn) {
        m_songsPlayAllBtn->setIcon(Icons::renderNamed("Play", 18, QColor(255, 255, 255)));
        m_songsPlayAllBtn->setIconSize(QSize(18, 18));
        m_songsPlayAllBtn->setStyleSheet(QStringLiteral(
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
    if (m_songsDownloadAllBtn) {
        m_songsDownloadAllBtn->setIcon(Icons::renderNamed("Download", 18, secondaryIc));
        m_songsDownloadAllBtn->setIconSize(QSize(18, 18));
        m_songsDownloadAllBtn->setStyleSheet(QStringLiteral(
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
                                             .arg(tabBg, titleFg));
    }

    const QColor emptyIcon = dark ? QColor(244, 246, 255, 120) : QColor(33, 37, 41, 100);
    const QPixmap offIcon = Icons::renderNamed("SearchOff", 64, emptyIcon);
    if (m_songsEmptyIcon)
        m_songsEmptyIcon->setPixmap(offIcon);
    if (m_playlistEmptyIcon)
        m_playlistEmptyIcon->setPixmap(offIcon);
    if (m_artistEmptyIcon)
        m_artistEmptyIcon->setPixmap(offIcon);

    if (m_songList)
        m_songList->applyTheme();
    for (auto *card : m_playlistCards)
        card->applyTheme();
}

MusicInfo SearchPage::musicFromMap(const QVariantMap &item)
{
    MusicInfo info;
    info.id = item.value(QStringLiteral("id")).toInt();
    info.title = item.value(QStringLiteral("title")).toString();
    info.artist = item.value(QStringLiteral("artist")).toString();
    info.album = item.value(QStringLiteral("album")).toString();
    info.duration = item.value(QStringLiteral("duration")).toInt();
    info.coverUrl = QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(info.id);
    info.lrc = item.value(QStringLiteral("lrc")).toBool();
    info.maxQuality = item.value(QStringLiteral("maxQuality")).toString();
    return info;
}

void SearchPage::search(const QString &query)
{
    m_query = query.trimmed();
    updateTitleRow();
    showHintState(m_query.isEmpty());
    if (m_query.isEmpty())
        return;

    m_musicPage = 1;
    m_musicResults.clear();
    m_playlistResults.clear();
    m_artistResults.clear();

    if (m_songList)
        m_songList->setSongs({});
    hideSongsEmpty();
    hidePlaylistEmpty();
    hideArtistEmpty();

    fetchMusicResults(false);
    fetchPlaylistResults();
    fetchArtistResults();
}

void SearchPage::updateTitleRow()
{
    if (m_keywordLbl)
        m_keywordLbl->setText(m_query.isEmpty() ? QStringLiteral("—") : m_query);
    if (m_suffixLbl)
        m_suffixLbl->setVisible(!m_query.isEmpty());
}

void SearchPage::showHintState(bool hint)
{
    if (m_hintWrap)
        m_hintWrap->setVisible(hint);
    if (m_tabStack)
        m_tabStack->setVisible(!hint);
    if (hint && m_hintLbl)
        m_hintLbl->setText(I18n::instance().tr(QStringLiteral("searchStartHint")));
}

void SearchPage::setActiveTab(Tab tab)
{
    m_activeTab = tab;
    if (m_tabStack)
        m_tabStack->setCurrentIndex(static_cast<int>(tab));
    if (m_tabSongs)
        m_tabSongs->setChecked(tab == Songs);
    if (m_tabPlaylists)
        m_tabPlaylists->setChecked(tab == Playlists);
    if (m_tabArtists)
        m_tabArtists->setChecked(tab == Artists);
}

void SearchPage::fetchMusicResults(bool append)
{
    if (!m_apiClient || m_query.isEmpty() || m_musicLoading)
        return;
    m_musicLoading = true;
    if (m_loadMoreBtn) {
        m_loadMoreBtn->setEnabled(false);
        m_loadMoreBtn->setText(I18n::instance().tr(QStringLiteral("loading")));
    }

    const int page = append ? m_musicPage + 1 : 1;
    m_apiClient->searchMusic(m_query, page, kMusicPageSize,
                             [this, append, page](bool success, int total, int, int,
                                                  const QList<QVariantMap> &results) {
                                 m_musicLoading = false;
                                 if (!success) {
                                     if (!append)
                                         showSongsEmpty(
                                             I18n::instance().tr(QStringLiteral("searchEmptySongs"))
                                                 .arg(m_query));
                                     if (m_loadMoreBtn)
                                         m_loadMoreBtn->hide();
                                     return;
                                 }

                                 m_musicPage = page;
                                 m_musicTotal = total > 0 ? total : static_cast<int>(results.size());
                                 if (!append)
                                     m_musicResults.clear();
                                 for (const auto &item : results)
                                     m_musicResults.append(musicFromMap(item));

                                 m_musicHasMore = m_musicResults.size() < m_musicTotal;
                                 applyMusicResults();
                             });
}

void SearchPage::applyMusicResults()
{
    if (m_musicResults.isEmpty()) {
        if (m_songList)
            m_songList->setSongs({});
        if (m_songsActionRow)
            m_songsActionRow->hide();
        showSongsEmpty(I18n::instance().tr(QStringLiteral("searchEmptySongs")).arg(m_query));
        if (m_loadMoreBtn)
            m_loadMoreBtn->hide();
        return;
    }

    hideSongsEmpty();
    if (m_songsActionRow)
        m_songsActionRow->show();
    if (m_songsPlayAllBtn)
        m_songsPlayAllBtn->setEnabled(true);
    if (m_songsDownloadAllBtn)
        m_songsDownloadAllBtn->setEnabled(true);
    if (m_songList) {
        m_songList->setSongs(m_musicResults);
        m_songList->refreshDownloadDisplay();
        updatePlayingHighlight();
    }

    if (m_loadMoreBtn) {
        const bool showMore = m_musicHasMore;
        m_loadMoreBtn->setVisible(showMore);
        m_loadMoreBtn->setEnabled(true);
        m_loadMoreBtn->setText(I18n::instance().tr(QStringLiteral("loadMore")));
    }
}

void SearchPage::refreshDownloadDisplay()
{
    if (m_songList)
        m_songList->refreshDownloadDisplay();
}

void SearchPage::fetchPlaylistResults()
{
    if (!m_apiClient || m_query.isEmpty() || m_playlistLoading)
        return;
    m_playlistLoading = true;
    m_apiClient->fetchPlaylists(m_query, [this](bool success, const QList<QVariantMap> &results) {
        m_playlistLoading = false;
        if (success)
            m_playlistResults = results;
        else
            m_playlistResults.clear();
        applyPlaylistResults();
    });
}

void SearchPage::applyPlaylistResults()
{
    for (auto *card : m_playlistCards) {
        card->deleteLater();
    }
    m_playlistCards.clear();

    if (m_playlistResults.isEmpty()) {
        m_playlistScroll->hide();
        showPlaylistEmpty(I18n::instance().tr(QStringLiteral("searchEmptyPlaylists")).arg(m_query));
        return;
    }

    hidePlaylistEmpty();
    m_playlistScroll->show();

    for (const auto &pl : m_playlistResults) {
        CoverListItemData info;
        info.id = pl.value(QStringLiteral("id")).toInt();
        info.name = pl.value(QStringLiteral("name")).toString();
        info.description = pl.value(QStringLiteral("description")).toString();
        info.musicCount = pl.value(QStringLiteral("musicCount")).toInt();
        info.coverUrl = playlistCoverUrl(pl);

        auto *card = new CoverListCard(info, m_playlistGridInner);
        connect(card, &CoverListCard::clicked, this, &SearchPage::openPlaylist);
        connect(card, &CoverListCard::playClicked, this, &SearchPage::openPlaylist);
        m_playlistCards.append(card);
    }
    relayoutPlaylistGrid();
}

void SearchPage::relayoutPlaylistGrid()
{
    if (!m_playlistGridInner || m_playlistCards.isEmpty())
        return;

    QLayout *oldLay = m_playlistGridInner->layout();
    if (oldLay) {
        QLayoutItem *item;
        while ((item = oldLay->takeAt(0)) != nullptr)
            delete item;
        delete oldLay;
    }

    auto *grid = new QGridLayout(m_playlistGridInner);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(20);
    grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    const int hostW = m_playlistScroll ? m_playlistScroll->viewport()->width() : m_playlistGridHost->width();
    const int gap = 20;
    const int minCell = 160;
    const int usable = qMax(minCell, hostW - 8);
    int cols = qMax(1, (usable + gap) / (minCell + gap));
    const int cellW = (usable - gap * (cols - 1)) / cols;

    int row = 0;
    int col = 0;
    for (auto *card : m_playlistCards) {
        card->setCellWidth(cellW);
        card->applyTheme();
        grid->addWidget(card, row, col);
        col++;
        if (col >= cols) {
            col = 0;
            row++;
        }
    }
}

void SearchPage::fetchArtistResults()
{
    if (!m_apiClient || m_query.isEmpty() || m_artistLoading)
        return;
    m_artistLoading = true;
    m_apiClient->searchArtists(m_query, [this](bool success, const QVariantMap &result) {
        m_artistLoading = false;
        m_artistResults.clear();
        if (success && !result.isEmpty())
            m_artistResults.append(result);
        applyArtistResults();
    });
}

void SearchPage::applyArtistResults()
{
    while (QLayoutItem *it = m_artistGrid->takeAt(0)) {
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }

    if (m_artistResults.isEmpty()) {
        m_artistGridScroll->hide();
        showArtistEmpty(I18n::instance().tr(QStringLiteral("searchEmptyArtists")).arg(m_query));
        return;
    }

    hideArtistEmpty();
    m_artistGridScroll->show();

    const int colW = 130;
    const int cols = qMax(1, m_artistGridHost->width() / colW);
    int row = 0;
    int col = 0;

    for (const auto &ar : m_artistResults) {
        auto *tile = new SearchArtistTile(ar, m_artistGridHost);
        tile->onClicked = [this](const QVariantMap &artist) { emit openArtist(artist); };
        m_artistGrid->addWidget(tile, row, col);
        col++;
        if (col >= cols) {
            col = 0;
            row++;
        }
    }
}

void SearchPage::showSongsEmpty(const QString &text)
{
    if (m_songList)
        m_songList->hide();
    if (m_songsEmptyLbl)
        m_songsEmptyLbl->setText(text);
    if (m_songsEmptyWrap)
        m_songsEmptyWrap->show();
}

void SearchPage::hideSongsEmpty()
{
    if (m_songsEmptyWrap)
        m_songsEmptyWrap->hide();
    if (m_songList)
        m_songList->show();
}

void SearchPage::showPlaylistEmpty(const QString &text)
{
    if (m_playlistScroll)
        m_playlistScroll->hide();
    if (m_playlistEmptyLbl)
        m_playlistEmptyLbl->setText(text);
    if (m_playlistEmptyWrap)
        m_playlistEmptyWrap->show();
}

void SearchPage::hidePlaylistEmpty()
{
    if (m_playlistEmptyWrap)
        m_playlistEmptyWrap->hide();
}

void SearchPage::showArtistEmpty(const QString &text)
{
    if (m_artistGridScroll)
        m_artistGridScroll->hide();
    if (m_artistEmptyWrap) {
        if (m_artistEmptyLbl)
            m_artistEmptyLbl->setText(text);
        m_artistEmptyWrap->show();
    }
}

void SearchPage::hideArtistEmpty()
{
    if (m_artistEmptyWrap)
        m_artistEmptyWrap->hide();
}

void SearchPage::onSongListScrolled(int scrollTop)
{
    if (m_activeTab != Songs || m_musicLoading || !m_musicHasMore)
        return;
    const int contentH = m_musicResults.size() * SongListWidget::kRowStride;
    const int viewH = m_songList ? m_songList->height() : 0;
    if (viewH > 0 && scrollTop + viewH >= contentH - 120)
        fetchMusicResults(true);
}

void SearchPage::showSongContextMenu(const MusicInfo &info, const QPoint &globalPos)
{
    if (info.id <= 0 && info.localPath.isEmpty())
        return;

    SongMenuBuilder::State state;
    state.favorited = m_favoritedIds.contains(info.id);
    state.downloaded = MusicDownloadManager::instance().isDownloaded(info.id);
    state.inPlayQueue = SongMenuBuilder::queueContains(info.id);

    SongMenuBuilder::Handlers handlers;
    handlers.playNext = [this, info]() { emit playNextRequested(info); };
    handlers.toggleFavorite = [this, info]() { emit favoriteRequested(info.id); };
    handlers.toggleQueue = [info]() { SongMenuBuilder::toggleQueue(info); };
    handlers.download = [this, info]() { emit downloadRequested(info); };

    // 统一菜单（设计稿）：下一首播放 / 收藏 / 播放队列 / 下载
    QList<SongContextMenuPopup::Entry> entries =
        SongMenuBuilder::buildStandard(info, state, handlers);

    SongContextMenuPopup::showAt(window() ? window() : this, globalPos, entries);
}

int SearchPage::currentPlayingMusicId() const
{
    const auto &mgr = PlaylistManager::instance();
    const int idx = mgr.currentIndex();
    if (idx < 0 || idx >= mgr.playlist().size())
        return -1;
    return mgr.playlist().at(idx).id;
}

void SearchPage::updatePlayingHighlight()
{
    const int id = currentPlayingMusicId();
    if (m_songList)
        m_songList->setCurrentPlayingId(id);
}

void SearchPage::setFavoritedMusicIds(const QSet<int> &ids)
{
    m_favoritedIds = ids;
    if (m_songList)
        m_songList->refreshFavoriteDisplay();
}

void SearchPage::setPlaybackPaused(bool paused)
{
    if (m_songList)
        m_songList->setPlaybackPaused(paused);
}

void SearchPage::retranslate()
{
    if (m_suffixLbl)
        m_suffixLbl->setText(I18n::instance().tr(QStringLiteral("searchRelatedSuffix")));
    if (m_tabSongs)
        m_tabSongs->setText(I18n::instance().tr(QStringLiteral("searchMusic")));
    if (m_tabPlaylists)
        m_tabPlaylists->setText(I18n::instance().tr(QStringLiteral("searchPlaylist")));
    if (m_tabArtists)
        m_tabArtists->setText(I18n::instance().tr(QStringLiteral("artist")));
    if (m_loadMoreBtn && m_loadMoreBtn->isVisible())
        m_loadMoreBtn->setText(I18n::instance().tr(QStringLiteral("loadMore")));
    if (m_songsPlayAllBtn)
        m_songsPlayAllBtn->setText(I18n::instance().tr(QStringLiteral("playAll")));
    if (m_songsDownloadAllBtn)
        m_songsDownloadAllBtn->setText(I18n::instance().tr(QStringLiteral("downloadAll")));
    if (m_songList)
        m_songList->retranslate();
    if (m_hintWrap && m_hintWrap->isVisible())
        showHintState(true);
    applyPageStyle();
}

void SearchPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updatePlayingHighlight();
}
