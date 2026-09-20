/**
 * @file playlistdetailpage.cpp
 * @brief 歌单详情 — 1:1 SPlayer playlist.vue + ListDetail + SongList + SongCard
 */

#include "playlistdetailpage.h"
#include "songlistwidget.h"
#include "roundcoverlabel.h"
#include "core/apiclient.h"
#include "core/i18n.h"
#include "core/covercache.h"
#include "core/musicdownloadmanager.h"
#include "core/playlistmanager.h"
#include "core/usermanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"
#include "ui/lineinputdialog.h"
#include "ui/toast.h"
#include "ui/songcontextmenu.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QTimer>
#include <QShowEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QMenu>
#include <QAction>
#include <QDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace {

constexpr int kHeaderFullH = 240;
constexpr int kHeaderCompactH = 120;
constexpr int kCoverFull = 216;
constexpr int kCoverCompact = 96;

class CoverMaskWidget : public QWidget {
public:
    explicit CoverMaskWidget(int radius, QWidget *parent = nullptr)
        : QWidget(parent), m_radius(radius)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(r, m_radius, m_radius);
        p.setClipPath(clip);
        QLinearGradient g(r.topLeft(), QPointF(r.left(), r.bottom()));
        g.setColorAt(0, QColor(0, 0, 0, 76));
        g.setColorAt(1, QColor(0, 0, 0, 0));
        p.fillRect(r, g);
    }

    int m_radius = 8;
};

void layoutPlaylistCover(QWidget *wrap, RoundCoverLabel *shadow, RoundCoverLabel *cover, QWidget *mask,
                         QWidget *playCountRow, int size, bool compact)
{
    const int offX = compact ? 4 : 8;
    const int offY = compact ? 4 : 6;
    const int shadowSz = qMax(48, size - offX);
    wrap->setFixedSize(size, size);
    shadow->setGeometry(offX, offY, shadowSz, shadowSz);
    cover->setGeometry(0, 0, size, size);
    mask->setGeometry(0, 0, size, size / 3);
    if (playCountRow)
        playCountRow->setGeometry(size - 96, 8, 88, 22);
}

QString formatSongCount(int n)
{
    if (n >= 100000000)
        return QString::number(n / 100000000.0, 'f', 1) + QStringLiteral("亿");
    if (n >= 10000)
        return QString::number(n / 10000.0, 'f', 1) + QStringLiteral("万");
    return QString::number(n);
}

} // namespace

PlaylistDetailPage::PlaylistDetailPage(ApiClient *apiClient, QWidget *parent)
    : QWidget(parent), m_apiClient(apiClient)
{
    setObjectName(QStringLiteral("playlistDetailPage"));
    setupUi();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) { applyPageStyle(); });

    connect(&PlaylistManager::instance(), &PlaylistManager::playlistChanged, this,
            &PlaylistDetailPage::updatePlayingHighlight);
    connect(&PlaylistManager::instance(), &PlaylistManager::currentIndexChanged, this,
            &PlaylistDetailPage::updatePlayingHighlight);
}

void PlaylistDetailPage::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 0, 24, 24);
    root->setSpacing(0);

    m_detailHeader = new QWidget(this);
    m_detailHeader->setObjectName(QStringLiteral("playlistListDetail"));
    m_detailHeader->setMinimumHeight(kHeaderFullH);
    m_detailHeader->setMaximumHeight(kHeaderFullH);

    auto *detailLay = new QHBoxLayout(m_detailHeader);
    detailLay->setContentsMargins(0, 12, 60, 24);
    detailLay->setSpacing(0);

    m_coverWrap = new QWidget(m_detailHeader);
    m_coverWrap->setAttribute(Qt::WA_TranslucentBackground);

    m_coverShadow = new RoundCoverLabel(8, m_coverWrap);
    m_coverShadow->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_coverShadow->lower();

    m_coverImg = new RoundCoverLabel(8, m_coverWrap);
    m_coverMask = new CoverMaskWidget(8, m_coverWrap);

    m_playCountRow = new QWidget(m_coverWrap);
    m_playCountRow->setGeometry(kCoverFull - 96, 8, 88, 22);
    m_playCountRow->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *pcLay = new QHBoxLayout(m_playCountRow);
    pcLay->setContentsMargins(0, 0, 0, 0);
    pcLay->setSpacing(4);
    auto *pcIcon = new QLabel(m_playCountRow);
    pcIcon->setPixmap(Icons::renderNamed("Music", 16, Qt::white));
    pcIcon->setFixedSize(16, 16);
    pcLay->addWidget(pcIcon);
    m_playCountLbl = new QLabel(m_playCountRow);
    pcLay->addWidget(m_playCountLbl);
    layoutPlaylistCover(m_coverWrap, m_coverShadow, m_coverImg, m_coverMask, m_playCountRow, kCoverFull,
                        false);
    m_coverImg->raise();
    m_coverMask->raise();
    m_playCountRow->raise();
    m_coverShadow->setStyleSheet(QStringLiteral("background: transparent;"));

    detailLay->addWidget(m_coverWrap);
    detailLay->addSpacing(20);

    auto *dataCol = new QWidget(m_detailHeader);
    auto *dataLay = new QVBoxLayout(dataCol);
    dataLay->setContentsMargins(0, 0, 0, 0);
    dataLay->setSpacing(0);

    m_titleLbl = new QLabel(dataCol);
    m_titleLbl->setWordWrap(true);
    dataLay->addWidget(m_titleLbl);
    dataLay->addSpacing(12);

    m_metaRow = new QWidget(dataCol);
    auto *metaLay = new QHBoxLayout(m_metaRow);
    metaLay->setContentsMargins(0, 0, 0, 0);
    metaLay->setSpacing(4);
    auto *personIco = new QLabel(m_metaRow);
    personIco->setPixmap(Icons::renderNamed("Person", 20, QColor(150, 150, 150)));
    personIco->setFixedSize(20, 20);
    metaLay->addWidget(personIco);
    m_creatorLbl = new QLabel(m_metaRow);
    metaLay->addWidget(m_creatorLbl, 1);
    dataLay->addWidget(m_metaRow);
    dataLay->addStretch();

    auto *menuRow = new QWidget(dataCol);
    menuRow->setFixedHeight(40);
    auto *menuLay = new QHBoxLayout(menuRow);
    menuLay->setContentsMargins(0, 0, 0, 0);
    menuLay->setSpacing(12);

    m_playBtn = new QPushButton(I18n::instance().tr(QStringLiteral("play")), menuRow);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setFixedHeight(40);
    connect(m_playBtn, &QPushButton::clicked, this, [this]() {
        if (!m_displaySongs.isEmpty())
            emit playAllRequested(m_displaySongs);
    });
    menuLay->addWidget(m_playBtn);

    m_downloadAllBtn = new QPushButton(I18n::instance().tr(QStringLiteral("downloadAll")), menuRow);
    m_downloadAllBtn->setCursor(Qt::PointingHandCursor);
    m_downloadAllBtn->setFixedHeight(40);
    connect(m_downloadAllBtn, &QPushButton::clicked, this, [this]() {
        if (!m_displaySongs.isEmpty())
            emit downloadAllRequested(m_displaySongs);
    });
    menuLay->addWidget(m_downloadAllBtn);

    m_editPlaylistBtn = new QPushButton(I18n::instance().tr(QStringLiteral("editPlaylist")), menuRow);
    m_editPlaylistBtn->setCursor(Qt::PointingHandCursor);
    m_editPlaylistBtn->setFixedHeight(40);
    m_editPlaylistBtn->hide();
    connect(m_editPlaylistBtn, &QPushButton::clicked, this, &PlaylistDetailPage::editPlaylistDescription);
    menuLay->addWidget(m_editPlaylistBtn);

    m_collectPlaylistBtn = new QPushButton(I18n::instance().tr(QStringLiteral("collectPlaylist")), menuRow);
    m_collectPlaylistBtn->setCursor(Qt::PointingHandCursor);
    m_collectPlaylistBtn->setFixedHeight(40);
    m_collectPlaylistBtn->hide();
    connect(m_collectPlaylistBtn, &QPushButton::clicked, this, &PlaylistDetailPage::toggleCollectPlaylist);
    menuLay->addWidget(m_collectPlaylistBtn);

    m_moreBtn = new QPushButton(menuRow);
    m_moreBtn->setFixedSize(40, 40);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setFlat(true);
    connect(m_moreBtn, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        const bool dark = Theme::ThemeManager::instance().isDarkMode();
        menu.setStyleSheet(dark
                               ? QStringLiteral(
                                     "QMenu { background: #2a2a2a; border: 1px solid rgba(255,255,255,0.1);"
                                     " border-radius: 8px; padding: 6px 0; }"
                                     "QMenu::item { color: #eee; padding: 10px 20px; }"
                                     "QMenu::item:selected { background: rgba(230,57,80,0.2); }")
                               : QStringLiteral(
                                     "QMenu { background: #fff; border: 1px solid rgba(33,37,41,0.12);"
                                     " border-radius: 8px; padding: 6px 0; }"
                                     "QMenu::item { color: #212529; padding: 10px 20px; }"
                                     "QMenu::item:selected { background: rgba(230,57,80,0.12); }"));
        QAction *refreshAct = menu.addAction(I18n::instance().tr(QStringLiteral("refresh")));
        QAction *picked = menu.exec(m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
        if (picked == refreshAct)
            reloadPlaylist();
    });
    menuLay->addWidget(m_moreBtn);

    menuLay->addStretch();

    m_searchWrap = new QWidget(menuRow);
    m_searchWrap->setFixedHeight(40);
    m_searchWrap->setMinimumWidth(130);
    m_searchWrap->setMaximumWidth(200);
    auto *searchLay = new QHBoxLayout(m_searchWrap);
    searchLay->setContentsMargins(12, 0, 8, 0);
    searchLay->setSpacing(6);
    auto *searchIco = new QLabel(m_searchWrap);
    searchIco->setFixedSize(18, 18);
    searchLay->addWidget(searchIco);
    m_searchEdit = new QLineEdit(m_searchWrap);
    m_searchEdit->setPlaceholderText(I18n::instance().tr(QStringLiteral("fuzzySearch")));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFrame(false);
    // 输入停顿 180ms 再过滤：大歌单下每个按键都重建整张列表会卡手
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(180);
    connect(m_searchDebounce, &QTimer::timeout, this, &PlaylistDetailPage::applyFilter);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        m_searchDebounce->start();
    });
    searchLay->addWidget(m_searchEdit, 1);
    menuLay->addWidget(m_searchWrap);

    dataLay->addWidget(menuRow);
    detailLay->addWidget(dataCol, 1);

    root->addWidget(m_detailHeader);

    m_songList = new SongListWidget(this);
    m_songList->onSongActivate = [this](const MusicInfo &info) { emit playMusic(info); };
    m_songList->onSongPlayNext = [this](const MusicInfo &info) { emit playMusic(info); };
    m_songList->onSongContextMenu = [this](const MusicInfo &info, const QPoint &pos) {
        showSongContextMenu(info, pos);
    };
    m_songList->onUnfavorite = [this](int musicId) { emit favoriteRequested(musicId); };
    m_songList->onDownload = [this](const MusicInfo &info) { emit downloadRequested(info); };
    m_songList->isFavorited = [this](int id) { return m_favoritedIds.contains(id); };
    m_songList->isDownloaded = [](int id) { return MusicDownloadManager::instance().isDownloaded(id); };
    m_songList->onTogglePlayPause = [this]() { emit playPauseRequested(); };
    connect(m_songList, &SongListWidget::scrolled, this, [this](int scrollTop) {
        m_savedScrollTop = scrollTop;
        onListScrolled(scrollTop);
    });
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

    m_headerAnim = new QVariantAnimation(this);
    m_headerAnim->setDuration(300);
    m_headerAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_headerAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        const int h = v.toInt();
        if (m_detailHeader) {
            m_detailHeader->setMinimumHeight(h);
            m_detailHeader->setMaximumHeight(h);
        }
    });

    applyPageStyle();
}

void PlaylistDetailPage::applyPageStyle()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString titleFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QString metaFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.72)");
    const QString statusFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.55)");

    if (m_titleLbl) {
        const int fs = m_headerCompact ? 22 : 30;
        m_titleLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: %1px; font-weight: 700; color: %2; }").arg(fs).arg(titleFg));
    }
    if (m_creatorLbl) {
        m_creatorLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 14px; color: %1; }").arg(metaFg));
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
    const QString secondaryStyle = QStringLiteral(
        "QPushButton {"
        "  background: %1;"
        "  color: %2;"
        "  border: none;"
        "  border-radius: 20px;"
        "  font-size: 14px;"
        "  font-weight: 500;"
        "  padding: 0 20px;"
        "}"
        "QPushButton:hover { background: rgba(230,57,80,0.15); }")
                                     .arg(secondaryBg, secondaryFg);

    if (m_downloadAllBtn) {
        m_downloadAllBtn->setIcon(Icons::renderNamed("Download", 18, secondaryIc));
        m_downloadAllBtn->setIconSize(QSize(18, 18));
        m_downloadAllBtn->setStyleSheet(secondaryStyle);
    }
    if (m_editPlaylistBtn) {
        m_editPlaylistBtn->setIcon(Icons::renderNamed("EditNote", 18, secondaryIc));
        m_editPlaylistBtn->setIconSize(QSize(18, 18));
        m_editPlaylistBtn->setStyleSheet(secondaryStyle);
    }
    if (m_collectPlaylistBtn)
        updateCollectPlaylistButton();
    if (m_moreBtn) {
        m_moreBtn->setIcon(Icons::renderNamed("List", 20, secondaryIc));
        m_moreBtn->setIconSize(QSize(20, 20));
        m_moreBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: none; border-radius: 20px; }"
            "QPushButton:hover { background: rgba(230,57,80,0.15); }")
                                     .arg(secondaryBg));
    }
    if (m_searchWrap) {
        const QString bg = dark ? QStringLiteral("#2a2a2a") : QStringLiteral("#f0f0f0");
        m_searchWrap->setStyleSheet(QStringLiteral(
            "QWidget { background: %1; border: none; border-radius: 20px; }").arg(bg));
        if (auto *ico = m_searchWrap->findChild<QLabel *>()) {
            const QColor ic = dark ? QColor(244, 246, 255, 120) : QColor(33, 37, 41, 120);
            ico->setPixmap(Icons::renderNamed("Search", 18, ic));
        }
    }
    if (m_searchEdit) {
        m_searchEdit->setStyleSheet(QStringLiteral(
            "QLineEdit { background: transparent; border: none; font-size: 14px; color: %1; }")
                                        .arg(titleFg));
    }
    if (m_playCountLbl) {
        m_playCountLbl->setStyleSheet(QStringLiteral(
            "QLabel { color: #ffffff; font-size: 13px; font-weight: 700; background: transparent; }"));
    }
    if (m_statusLabel) {
        m_statusLabel->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 14px; color: %1; padding: 60px 24px; }").arg(statusFg));
    }

    if (m_songList)
        m_songList->applyTheme();
}

void PlaylistDetailPage::retranslate()
{
    if (m_playBtn)
        m_playBtn->setText(I18n::instance().tr(QStringLiteral("play")));
    if (m_downloadAllBtn)
        m_downloadAllBtn->setText(I18n::instance().tr(QStringLiteral("downloadAll")));
    if (m_editPlaylistBtn)
        m_editPlaylistBtn->setText(I18n::instance().tr(QStringLiteral("editPlaylist")));
    updateCollectPlaylistButton();
    if (m_searchEdit)
        m_searchEdit->setPlaceholderText(I18n::instance().tr(QStringLiteral("fuzzySearch")));
    if (m_titleLbl)
        m_titleLbl->setText(m_playlistName);
    if (m_songList)
        m_songList->retranslate();
    updateHeaderMeta();
    applyPageStyle();
}

void PlaylistDetailPage::setPlaybackPaused(bool paused)
{
    if (m_songList)
        m_songList->setPlaybackPaused(paused);
}

void PlaylistDetailPage::setFavoritedMusicIds(const QSet<int> &ids)
{
    m_favoritedIds = ids;
    if (m_songList)
        m_songList->refreshFavoriteDisplay();
}

void PlaylistDetailPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updatePlayingHighlight();
}

void PlaylistDetailPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void PlaylistDetailPage::setHeaderCompact(bool compact)
{
    if (m_headerCompact == compact)
        return;
    m_headerCompact = compact;

    const int targetH = compact ? kHeaderCompactH : kHeaderFullH;
    const int coverSz = compact ? kCoverCompact : kCoverFull;

    m_headerAnim->stop();
    m_headerAnim->setStartValue(m_detailHeader->height());
    m_headerAnim->setEndValue(targetH);
    m_headerAnim->start();

    layoutPlaylistCover(m_coverWrap, m_coverShadow, m_coverImg, m_coverMask, m_playCountRow, coverSz,
                        compact);
    if (m_playCountRow)
        m_playCountRow->setVisible(!compact);
    m_metaRow->setVisible(!compact);

    applyPageStyle();
}

void PlaylistDetailPage::onListScrolled(int scrollTop)
{
    if (!m_songList || m_songList->height() <= 0)
        return;
    if (scrollTop <= 10) {
        setHeaderCompact(false);
        return;
    }
    const int contentH = m_displaySongs.size() * SongListWidget::kRowStride;
    const int viewH = m_songList->height();
    if (contentH - viewH < 150)
        return;
    setHeaderCompact(true);
}

void PlaylistDetailPage::updateHeaderMeta()
{
    if (m_titleLbl)
        m_titleLbl->setText(m_playlistName.isEmpty() ? QStringLiteral("—") : m_playlistName);
    if (m_creatorLbl) {
        const QString creator = m_creatorUsername.isEmpty() ? QStringLiteral("—") : m_creatorUsername;
        m_creatorLbl->setText(creator);
    }
    if (m_playCountLbl)
        m_playCountLbl->setText(formatSongCount(m_allSongs.size()));
    if (m_playBtn)
        m_playBtn->setEnabled(!m_displaySongs.isEmpty());
    if (m_downloadAllBtn)
        m_downloadAllBtn->setEnabled(!m_displaySongs.isEmpty());
}

void PlaylistDetailPage::refreshDownloadDisplay()
{
    if (m_songList)
        m_songList->refreshDownloadDisplay();
}

void PlaylistDetailPage::updateCoverImage()
{
    const int coverId = m_firstMusicId > 0 ? m_firstMusicId
                                             : (m_allSongs.isEmpty() ? 0 : m_allSongs.first().id);
    if (coverId <= 0) {
        QPixmap pm(kCoverFull, kCoverFull);
        pm.fill(QColor(230, 57, 80, 50));
        m_coverImg->setPixmap(pm);
        m_coverShadow->setPixmap(pm);
        return;
    }

    m_coverMusicId = QString::number(coverId);
    const QString url = QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(coverId);

    auto applyCover = [this](const QPixmap &pix) {
        if (pix.isNull())
            return;
        QPixmap rounded = pix.scaled(kCoverFull, kCoverFull, Qt::KeepAspectRatioByExpanding,
                                     Qt::SmoothTransformation);
        m_coverImg->setPixmap(rounded);
        m_coverImg->update();

        const int sz = m_coverWrap ? m_coverWrap->width() : kCoverFull;
        QPixmap blurPm = rounded.scaled(sz * 92 / 100, sz * 96 / 100, Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation);
        QPixmap shadowPm(blurPm.size());
        shadowPm.fill(Qt::transparent);
        QPainter sp(&shadowPm);
        sp.setOpacity(0.6);
        sp.drawPixmap(0, 0, blurPm);
        sp.end();
        m_coverShadow->setPixmap(shadowPm);
    };

    if (QPixmap cached = CoverCache::instance()->get(m_coverMusicId); !cached.isNull()) {
        applyCover(cached);
        return;
    }

    connect(CoverCache::instance(), &CoverCache::coverLoaded, this,
            [this, applyCover](const QString &id, const QPixmap &pix) {
                if (id == m_coverMusicId)
                    applyCover(pix);
            });
    CoverCache::instance()->fetchCover(m_coverMusicId, url);
}

void PlaylistDetailPage::applyFilter()
{
    const QString q = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    m_displaySongs.clear();
    if (q.isEmpty()) {
        m_displaySongs = m_allSongs;
    } else {
        for (const MusicInfo &info : m_allSongs) {
            if (info.title.contains(q, Qt::CaseInsensitive) || info.artist.contains(q, Qt::CaseInsensitive)
                || info.album.contains(q, Qt::CaseInsensitive))
                m_displaySongs.append(info);
        }
    }

    if (m_songList) {
        m_songList->setVisible(!m_displaySongs.isEmpty() || m_allSongs.isEmpty());
        m_songList->setSongs(m_displaySongs);
        m_songList->refreshFavoriteDisplay();
        m_songList->refreshDownloadDisplay();
    }
    updatePlayingHighlight();
    updateHeaderMeta();

    if (!q.isEmpty() && m_displaySongs.isEmpty() && !m_allSongs.isEmpty()) {
        showPageStatus(I18n::instance().tr(QStringLiteral("favoritesSearchEmpty")).arg(q), "SearchOff");
    } else if (!m_allSongs.isEmpty()) {
        hidePageStatus();
        if (m_songList)
            m_songList->show();
    }
}

int PlaylistDetailPage::currentPlayingMusicId() const
{
    const auto &mgr = PlaylistManager::instance();
    const int idx = mgr.currentIndex();
    if (idx < 0 || idx >= mgr.playlist().size())
        return -1;
    return mgr.playlist()[idx].id;
}

void PlaylistDetailPage::updatePlayingHighlight()
{
    if (m_songList)
        m_songList->setCurrentPlayingId(currentPlayingMusicId());
}

void PlaylistDetailPage::showPageStatus(const QString &text, const char *iconName)
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

void PlaylistDetailPage::hidePageStatus()
{
    if (m_emptyIcon)
        m_emptyIcon->hide();
    if (m_statusLabel)
        m_statusLabel->hide();
    if (m_emptyWrap)
        m_emptyWrap->hide();
}

void PlaylistDetailPage::reloadPlaylist()
{
    if (m_playlistId > 0)
        loadPlaylist(m_playlistId);
}

int PlaylistDetailPage::currentUserId() const
{
    if (!UserManager::instance().isLoggedIn())
        return 0;
    const QVariantMap user = UserManager::instance().userInfo();
    int id = user.value(QStringLiteral("id")).toInt();
    if (id <= 0)
        id = user.value(QStringLiteral("userId")).toInt();
    return id;
}

void PlaylistDetailPage::updateActionButtons()
{
    if (m_editPlaylistBtn)
        m_editPlaylistBtn->setVisible(m_isUserPlaylist);
    if (m_collectPlaylistBtn)
        m_collectPlaylistBtn->setVisible(!m_isUserPlaylist);
}

void PlaylistDetailPage::updateCollectPlaylistButton()
{
    if (!m_collectPlaylistBtn)
        return;

    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString secondaryBg = dark ? QStringLiteral("#2a2a2a") : QStringLiteral("#f0f0f0");
    const QString secondaryFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    m_collectPlaylistBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: %1;"
        "  color: %2;"
        "  border: none;"
        "  border-radius: 20px;"
        "  font-size: 14px;"
        "  font-weight: 500;"
        "  padding: 0 20px;"
        "}"
        "QPushButton:hover { background: rgba(230,57,80,0.15); }")
                                            .arg(secondaryBg, secondaryFg));

    if (m_isPlaylistCollected) {
        m_collectPlaylistBtn->setText(I18n::instance().tr(QStringLiteral("uncollectPlaylist")));
        m_collectPlaylistBtn->setIcon(Icons::renderNamed("Favorite", 18, QColor(255, 69, 69)));
    } else {
        m_collectPlaylistBtn->setText(I18n::instance().tr(QStringLiteral("collectPlaylist")));
        const QColor ic = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
        m_collectPlaylistBtn->setIcon(Icons::renderNamed("FavoriteBorder", 18, ic));
    }
    m_collectPlaylistBtn->setIconSize(QSize(18, 18));
}

void PlaylistDetailPage::refreshPlaylistCollectedState()
{
    m_isPlaylistCollected = false;
    if (m_isUserPlaylist || m_playlistId <= 0 || !m_apiClient) {
        updateCollectPlaylistButton();
        return;
    }

    if (!UserManager::instance().isLoggedIn()) {
        updateCollectPlaylistButton();
        return;
    }

    m_apiClient->fetchFavoritePlaylists([this](bool success, const QList<QVariantMap> &playlists) {
        m_isPlaylistCollected = false;
        if (success) {
            for (const QVariantMap &pl : playlists) {
                if (pl.value(QStringLiteral("id")).toInt() == m_playlistId) {
                    m_isPlaylistCollected = true;
                    break;
                }
            }
        }
        updateCollectPlaylistButton();
    });
}

void PlaylistDetailPage::toggleCollectPlaylist()
{
    if (!m_apiClient || m_playlistId <= 0 || m_isUserPlaylist)
        return;

    if (!UserManager::instance().isLoggedIn()) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("pleaseLoginFirst")), Toast::Info);
        return;
    }

    if (m_isPlaylistCollected) {
        m_apiClient->unfavoritePlaylist(m_playlistId, [this](bool success, const QString &message) {
            if (success) {
                m_isPlaylistCollected = false;
                updateCollectPlaylistButton();
                emit refreshSidebarPlaylists();
                Toast::show(this, I18n::instance().tr(QStringLiteral("cancelFavoritePlaylistSuccess")),
                            Toast::Success);
            } else {
                Toast::show(this,
                            I18n::instance().tr(QStringLiteral("cancelFavoritePlaylistFailed")) + ": "
                                + message,
                            Toast::Error);
            }
        });
    } else {
        m_apiClient->favoritePlaylist(m_playlistId, [this](bool success, const QString &message) {
            if (success) {
                m_isPlaylistCollected = true;
                updateCollectPlaylistButton();
                emit refreshSidebarPlaylists();
                Toast::show(this, I18n::instance().tr(QStringLiteral("collectPlaylistSuccess")),
                            Toast::Success);
            } else {
                Toast::show(this,
                            I18n::instance().tr(QStringLiteral("collectPlaylistFailed")) + ": " + message,
                            Toast::Error);
            }
        });
    }
}

void PlaylistDetailPage::showSongContextMenu(const MusicInfo &info, const QPoint &globalPos)
{
    if (info.id <= 0 && info.localPath.isEmpty())
        return;

    QList<SongContextMenuPopup::Entry> entries;

    SongContextMenuPopup::Entry nextEntry;
    nextEntry.iconName = "PlayNext";
    nextEntry.label = I18n::instance().tr(QStringLiteral("playNext"));
    nextEntry.action = [this, info]() { emit playNextRequested(info); };
    entries.append(nextEntry);

    if (m_isUserPlaylist && m_playlistId > 0 && info.id > 0) {
        SongContextMenuPopup::Entry removeEntry;
        removeEntry.iconName = "Delete";
        removeEntry.label = I18n::instance().tr(QStringLiteral("removeFromPlaylist"));
        removeEntry.action = [this, info]() { removeSongFromPlaylist(info); };
        entries.append(removeEntry);
    }

    SongContextMenuPopup::showAt(window() ? window() : this, globalPos, entries);
}

void PlaylistDetailPage::removeSongFromPlaylist(const MusicInfo &info)
{
    if (!m_apiClient || m_playlistId <= 0 || info.id <= 0)
        return;

    m_apiClient->removeMusicFromPlaylist(m_playlistId, info.id, [this, musicId = info.id](bool success, const QString &message) {
        if (!success) {
            const QString msg =
                message.isEmpty() ? I18n::instance().tr(QStringLiteral("musicAddFailed")) : message;
            Toast::show(this, msg, Toast::Error);
            return;
        }

        for (int i = m_allSongs.size() - 1; i >= 0; --i) {
            if (m_allSongs[i].id == musicId) {
                m_allSongs.removeAt(i);
                break;
            }
        }
        applyFilter();
        updateHeaderMeta();
        updateCoverImage();
        Toast::show(this, I18n::instance().tr(QStringLiteral("musicRemovedSuccess")), Toast::Success);
    });
}

void PlaylistDetailPage::editPlaylistDescription()
{
    if (!m_apiClient || m_playlistId <= 0)
        return;

    LineInputDialog dlg(this, I18n::instance().tr(QStringLiteral("modifyPlaylistDesc")),
                        I18n::instance().tr(QStringLiteral("inputPlaylistDesc")), QString(),
                        m_playlistDesc.isEmpty() ? QString() : m_playlistDesc,
                        I18n::instance().tr(QStringLiteral("save")), false);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString newDesc = dlg.value();
    if (newDesc.isEmpty() || newDesc == m_playlistDesc)
        return;

    m_apiClient->updatePlaylist(m_playlistId, m_playlistName, newDesc,
                                [this, newDesc](bool success, const QString &, const QVariantMap &) {
                                    if (success) {
                                        m_playlistDesc = newDesc;
                                        emit refreshSidebarPlaylists();
                                    }
                                });
}

void PlaylistDetailPage::loadPlaylist(int playlistId)
{
    if (m_searchDebounce)
        m_searchDebounce->stop();

    // 同一个歌单重新进入时回到原来的位置，换歌单则回到顶部
    m_pendingScrollTop = (playlistId == m_savedScrollPlaylistId) ? m_savedScrollTop : 0;
    m_savedScrollPlaylistId = playlistId;

    m_playlistId = playlistId;
    m_allSongs.clear();
    m_displaySongs.clear();
    if (m_songList)
        m_songList->setSongs({});
    if (m_searchEdit)
        m_searchEdit->clear();
    if (m_searchDebounce)
        m_searchDebounce->stop();
    setHeaderCompact(false);

    if (!m_apiClient) {
        m_playlistName = QStringLiteral("歌单详情");
        updateHeaderMeta();
        showPageStatus(I18n::instance().tr(QStringLiteral("noMusicInPlaylist")));
        return;
    }

    if (m_detailHeader)
        m_detailHeader->show();
    showPageStatus(I18n::instance().tr(QStringLiteral("loading")));

    m_apiClient->fetchPlaylistDetail(playlistId, [this](bool success, const QVariantMap &detail) {
        if (success) {
            m_playlistName = detail.value(QStringLiteral("name")).toString();
            m_playlistDesc = detail.value(QStringLiteral("description")).toString();
            m_firstMusicId = detail.value(QStringLiteral("firstMusicId")).toInt();
            const auto creatorObj = detail.value(QStringLiteral("creator")).toMap();
            m_creatorId = creatorObj.value(QStringLiteral("id")).toInt();
            m_creatorUsername = creatorObj.value(QStringLiteral("username")).toString();
            const int uid = currentUserId();
            m_isUserPlaylist = m_creatorId > 0 && uid > 0 && m_creatorId == uid;
        } else {
            m_playlistName = QStringLiteral("歌单详情");
            m_playlistDesc.clear();
            m_firstMusicId = 0;
            m_creatorId = 0;
            m_creatorUsername.clear();
            m_isUserPlaylist = false;
        }

        updateActionButtons();
        refreshPlaylistCollectedState();
        updateHeaderMeta();

        m_apiClient->fetchPlaylistMusic(m_playlistId,
                                        [this](bool ok, int, const QList<QVariantMap> &musicList) {
                                            m_allSongs.clear();
                                            if (ok) {
                                                for (const auto &m : musicList) {
                                                    MusicInfo info;
                                                    info.id = m.value(QStringLiteral("id")).toInt();
                                                    info.title = m.value(QStringLiteral("title")).toString();
                                                    info.artist = m.value(QStringLiteral("artist")).toString();
                                                    info.album = m.value(QStringLiteral("album")).toString();
                                                    info.duration = m.value(QStringLiteral("duration")).toInt();
                                                    info.coverUrl = m.value(QStringLiteral("coverUrl")).toString();
                                                    if (info.coverUrl.isEmpty())
                                                        info.coverUrl = m.value(QStringLiteral("coverPath")).toString();
                                                    m_allSongs.append(info);
                                                }
                                            }

                                            if (m_allSongs.isEmpty()) {
                                                showPageStatus(
                                                    I18n::instance().tr(QStringLiteral("noMusicInPlaylist")),
                                                    "SearchOff");
                                                updateCoverImage();
                                                updateHeaderMeta();
                                                return;
                                            }

                                            hidePageStatus();
                                            updateCoverImage();
                                            applyFilter();
                                            if (m_songList) {
                                                m_songList->show();
                                                if (m_pendingScrollTop > 0)
                                                    m_songList->scrollToOffset(m_pendingScrollTop);
                                            }
                                        });
    });
}

void PlaylistDetailPage::paintEvent(QPaintEvent *)
{
}
