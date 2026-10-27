/**
 * @file favoritespage.cpp
 * @brief 「我喜欢的音乐」— 1:1 SPlayer liked.vue + ListDetail + SongList + SongCard
 */

#include "favoritespage.h"
#include "songlistwidget.h"
#include "roundcoverlabel.h"
#include "batchactionbar.h"
#include "core/apiclient.h"
#include "core/i18n.h"
#include "core/covercache.h"
#include "core/usermanager.h"
#include "core/playlistmanager.h"
#include "core/musicdownloadmanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"
#include "ui/toast.h"
#include "ui/headeractionmenu.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QShowEvent>
#include <QHideEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QAction>
#include <QPoint>
#include <QSignalBlocker>

namespace {

constexpr int kHeaderFullH = 240;
constexpr int kHeaderCompactH = 120;
constexpr int kCoverFull = 216; // 240 - 12 - 12 padding approx
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

void layoutFavoriteCover(QWidget *wrap, RoundCoverLabel *shadow, RoundCoverLabel *cover, QWidget *mask,
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

QString formatPlayCount(int n)
{
    if (n >= 100000000)
        return QString::number(n / 100000000.0, 'f', 1) + QStringLiteral("亿");
    if (n >= 10000)
        return QString::number(n / 10000.0, 'f', 1) + QStringLiteral("万");
    return QString::number(n);
}

} // namespace

FavoritesPage::FavoritesPage(ApiClient *apiClient, QWidget *parent)
    : QWidget(parent), m_apiClient(apiClient)
{
    setObjectName(QStringLiteral("favoritesPage"));
    setupUi();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) { applyPageStyle(); });

    connect(&PlaylistManager::instance(), &PlaylistManager::playlistChanged, this,
            &FavoritesPage::updatePlayingHighlight);
    connect(&PlaylistManager::instance(), &PlaylistManager::currentIndexChanged, this,
            &FavoritesPage::updatePlayingHighlight);
}

void FavoritesPage::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 0, 24, 24);
    root->setSpacing(0);

    m_detailHeader = new QWidget(this);
    m_detailHeader->setObjectName(QStringLiteral("favListDetail"));
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
    pcIcon->setPixmap(Icons::renderNamed("Play", 16, Qt::white));
    pcIcon->setFixedSize(16, 16);
    pcLay->addWidget(pcIcon);
    m_playCountLbl = new QLabel(m_playCountRow);
    pcLay->addWidget(m_playCountLbl);
    layoutFavoriteCover(m_coverWrap, m_coverShadow, m_coverImg, m_coverMask, m_playCountRow, kCoverFull,
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

    m_titleLbl = new QLabel(I18n::instance().tr(QStringLiteral("myFavorites")), dataCol);
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
    m_menuRow = menuRow;
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

    m_moreBtn = new QPushButton(menuRow);
    m_moreBtn->setFixedSize(40, 40);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setFlat(true);
    m_moreBtn->setToolTip(I18n::instance().tr(QStringLiteral("moreActions")));
    connect(m_moreBtn, &QPushButton::clicked, this, &FavoritesPage::showMoreMenu);
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
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &) { applyFilter(); });
    searchLay->addWidget(m_searchEdit, 1);
    menuLay->addWidget(m_searchWrap);

    dataLay->addWidget(menuRow);
    detailLay->addWidget(dataCol, 1);

    root->addWidget(m_detailHeader);

    m_songList = new SongListWidget(this);
    m_songList->onSongActivate = [this](const MusicInfo &info) {
        emit playRequested(info.id, info.title, info.artist, info.coverUrl);
    };
    m_songList->onSongPlayNext = [this](const MusicInfo &info) {
        emit playRequested(info.id, info.title, info.artist, info.coverUrl);
    };
    m_songList->onUnfavorite = [this](int id) { emit unfavoriteRequested(id); };
    m_songList->onDownload = [this](const MusicInfo &info) { emit downloadRequested(info); };
    m_songList->isFavorited = [](int) { return true; };
    m_songList->isDownloaded = [](int id) { return MusicDownloadManager::instance().isDownloaded(id); };
    m_songList->onTogglePlayPause = [this]() { emit playPauseRequested(); };
    connect(m_songList, &SongListWidget::scrolled, this, &FavoritesPage::onListScrolled);
    root->addWidget(m_songList, 1);

    setupBatchBar();

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

void FavoritesPage::showMoreMenu()
{
    const bool hasSongs = !m_displaySongs.isEmpty();
    HeaderActionMenu menu(this);
    QAction *downloadAll = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("downloadAll")), "Download", hasSongs);
    QAction *addAll = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("addAllToQueue")), "MusicList", hasSongs);
    menu.addSeparator();
    QAction *batchManage = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("batchManage")), "Batch", hasSongs);
    menu.addSeparator();
    QAction *refreshAction = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("refresh")), "Refresh");

    QAction *picked = menu.exec(m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
    if (picked == downloadAll)
        emit downloadAllRequested(m_displaySongs);
    else if (picked == addAll)
        addAllToQueue();
    else if (picked == batchManage)
        enterBatchMode();
    else if (picked == refreshAction)
        refresh();
}

/** 批量编辑模式：顶部按钮排 ↔ 批量操作条 的公共接线 */
void FavoritesPage::setupBatchBar()
{
    if (!m_menuRow || !m_songList)
        return;

    m_batchBar = new BatchActionBar(m_menuRow->parentWidget());
    m_batchBar->setCapabilities(true, true, true, true, false);
    m_batchBar->setActionMenuMode(true);
    m_batchBar->hide();
    if (auto *lay = qobject_cast<QBoxLayout *>(m_menuRow->parentWidget()->layout()))
        lay->insertWidget(lay->indexOf(m_menuRow), m_batchBar);

    connect(m_batchBar, &BatchActionBar::exitRequested, this, &FavoritesPage::exitBatchMode);
    connect(m_batchBar, &BatchActionBar::selectAllToggled, this, [this]() {
        if (!m_songList)
            return;
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
            ids.reserve(picked.size());
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
        // 本地播放队列一次性写入，避免逐首触发 playlistChanged
        PlaylistManager::instance().addAllToPlaylist(picked);
        Toast::show(this, I18n::instance().tr(QStringLiteral("batchQueueAdded")).arg(picked.size()),
                    Toast::Success);
        exitBatchMode();
    });
    connect(m_songList, &SongListWidget::batchSelectionChanged, this,
            &FavoritesPage::onBatchSelectionChanged);
}

void FavoritesPage::enterBatchMode()
{
    if (!m_songList || !m_batchBar || m_displaySongs.isEmpty())
        return;

    m_songList->clearBatchSelection();
    m_songList->setBatchMode(true);
    m_batchBar->reset();
    m_menuRow->hide();
    m_batchBar->show();
    updateHeaderMeta();
}

void FavoritesPage::exitBatchMode()
{
    if (!m_songList || !m_batchBar)
        return;

    m_songList->setBatchMode(false);
    m_batchBar->reset();
    m_batchBar->hide();
    if (m_menuRow)
        m_menuRow->show();
}

void FavoritesPage::onBatchSelectionChanged(int count)
{
    if (!m_batchBar || !m_songList)
        return;
    m_batchBar->setSelectedCount(count);
    m_batchBar->setAllSelected(m_songList->batchAllSelected());
}

/** 一键把当前筛选后的全部曲目加入本地播放队列 */
void FavoritesPage::addAllToQueue()
{
    const QList<MusicInfo> &songs = m_displaySongs;
    if (songs.isEmpty()) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("queueAddAllEmpty")), Toast::Info);
        return;
    }

    auto &mgr = PlaylistManager::instance();
    const int before = mgr.count();
    mgr.addAllToPlaylist(songs);
    const int added = qMax(0, mgr.count() - before);

    // addAllToPlaylist 自带去重，实际新增数可能小于曲目数；两种结果分开提示
    if (added <= 0)
        Toast::show(this, I18n::instance().tr(QStringLiteral("queueAddAllNone")), Toast::Info);
    else
        Toast::show(this, I18n::instance().tr(QStringLiteral("addAllToQueueDone")).arg(added),
                    Toast::Success);
}

void FavoritesPage::applyPageStyle()
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
    if (m_moreBtn) {
        const QColor moreIc = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
        m_moreBtn->setIcon(Icons::renderNamed("More", 20, moreIc));
        m_moreBtn->setIconSize(QSize(20, 20));
        m_moreBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: none; border-radius: 20px; }"
            "QPushButton:hover { background: rgba(230,57,80,0.15); }")
                                     .arg(secondaryBg));
    }
    if (m_batchBar)
        m_batchBar->applyTheme();
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

void FavoritesPage::retranslate()
{
    if (m_titleLbl)
        m_titleLbl->setText(I18n::instance().tr(QStringLiteral("myFavorites")));
    if (m_playBtn)
        m_playBtn->setText(I18n::instance().tr(QStringLiteral("play")));
    if (m_moreBtn)
        m_moreBtn->setToolTip(I18n::instance().tr(QStringLiteral("moreActions")));
    if (m_searchEdit)
        m_searchEdit->setPlaceholderText(I18n::instance().tr(QStringLiteral("fuzzySearch")));
    if (m_batchBar)
        m_batchBar->retranslate();
    if (m_songList)
        m_songList->retranslate();
    updateHeaderMeta();
    applyPageStyle();
}

void FavoritesPage::refresh()
{
    loadFavorites();
}

void FavoritesPage::setPlaybackPaused(bool paused)
{
    if (m_songList)
        m_songList->setPlaybackPaused(paused);
}

void FavoritesPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updatePlayingHighlight();
}

void FavoritesPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void FavoritesPage::setHeaderCompact(bool compact)
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

    layoutFavoriteCover(m_coverWrap, m_coverShadow, m_coverImg, m_coverMask, m_playCountRow, coverSz,
                        compact);
    if (m_playCountRow)
        m_playCountRow->setVisible(!compact);
    m_metaRow->setVisible(!compact);

    applyPageStyle();
}

void FavoritesPage::onListScrolled(int scrollTop)
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

void FavoritesPage::updateHeaderMeta()
{
    QString name = QStringLiteral("—");
    if (UserManager::instance().isLoggedIn()) {
        const auto info = UserManager::instance().userInfo();
        name = info.value(QStringLiteral("nickname")).toString();
        if (name.isEmpty())
            name = info.value(QStringLiteral("nickname")).toString();
    }
    if (m_creatorLbl)
        m_creatorLbl->setText(name.isEmpty() ? QStringLiteral("—") : name);

    if (m_playCountLbl)
        m_playCountLbl->setText(formatPlayCount(m_allFavorites.size()));

    if (m_playBtn)
        m_playBtn->setEnabled(!m_displaySongs.isEmpty());

    const bool hasSongs = !m_displaySongs.isEmpty();
    const bool batchActive = m_songList && m_songList->batchMode();
    if (!hasSongs && batchActive)
        exitBatchMode();
    // 批量模式下按钮排已让位给批量操作条，别被这里又显示出来
    else if (m_menuRow && batchActive)
        m_menuRow->hide();
}

void FavoritesPage::refreshDownloadDisplay()
{
    if (m_songList)
        m_songList->refreshDownloadDisplay();
}

void FavoritesPage::updateCoverImage()
{
    if (m_allFavorites.isEmpty()) {
        QPixmap pm(kCoverFull, kCoverFull);
        pm.fill(QColor(230, 57, 80, 50));
        m_coverImg->setPixmap(pm);
        m_coverShadow->setPixmap(pm);
        return;
    }

    const MusicInfo &first = m_allFavorites.first();
    m_coverMusicId = QString::number(first.id);
    const QString url = first.coverUrl.isEmpty()
        ? QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(first.id)
        : first.coverUrl;

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

void FavoritesPage::applyFilter()
{
    const QString q = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    m_displaySongs.clear();
    if (q.isEmpty()) {
        m_displaySongs = m_allFavorites;
    } else {
        for (const MusicInfo &info : m_allFavorites) {
            if (info.title.contains(q, Qt::CaseInsensitive) || info.artist.contains(q, Qt::CaseInsensitive)
                || info.album.contains(q, Qt::CaseInsensitive))
                m_displaySongs.append(info);
        }
    }

    if (m_songList) {
        m_songList->setVisible(!m_displaySongs.isEmpty() || m_allFavorites.isEmpty());
        m_songList->setSongs(m_displaySongs);
        m_songList->refreshDownloadDisplay();
    }
    updatePlayingHighlight();
    updateHeaderMeta();

    if (!q.isEmpty() && m_displaySongs.isEmpty() && !m_allFavorites.isEmpty()) {
        showPageStatus(I18n::instance().tr(QStringLiteral("favoritesSearchEmpty")).arg(q), "SearchOff");
    } else if (!m_allFavorites.isEmpty()) {
        hidePageStatus();
        if (m_songList)
            m_songList->show();
    }
}

int FavoritesPage::currentPlayingMusicId() const
{
    const auto &mgr = PlaylistManager::instance();
    const int idx = mgr.currentIndex();
    if (idx < 0 || idx >= mgr.playlist().size())
        return -1;
    return mgr.playlist()[idx].id;
}

void FavoritesPage::updatePlayingHighlight()
{
    if (m_songList)
        m_songList->setCurrentPlayingId(currentPlayingMusicId());
}

void FavoritesPage::showPageStatus(const QString &text, const char *iconName)
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

void FavoritesPage::hidePageStatus()
{
    if (m_emptyIcon)
        m_emptyIcon->hide();
    if (m_statusLabel)
        m_statusLabel->hide();
    if (m_emptyWrap)
        m_emptyWrap->hide();
}

void FavoritesPage::loadFavorites()
{
    m_allFavorites.clear();
    m_displaySongs.clear();
    if (m_songList)
        m_songList->setSongs({});
    if (m_searchEdit)
        m_searchEdit->clear();
    setHeaderCompact(false);

    if (!UserManager::instance().isLoggedIn()) {
        if (m_detailHeader)
            m_detailHeader->hide();
        showPageStatus(I18n::instance().tr(QStringLiteral("pleaseLoginFirst")));
        return;
    }

    if (m_detailHeader)
        m_detailHeader->show();
    showPageStatus(I18n::instance().tr(QStringLiteral("loading")));

    m_apiClient->fetchFavorites([this](bool success, const QList<QVariantMap> &favorites) {
        m_allFavorites.clear();

        if (!success) {
            showPageStatus(I18n::instance().tr(QStringLiteral("networkErrorRetry")));
            return;
        }

        for (const auto &fav : favorites) {
            MusicInfo info;
            info.id = fav.value(QStringLiteral("id")).toInt();
            info.title = fav.value(QStringLiteral("title")).toString();
            info.artist = fav.value(QStringLiteral("artist")).toString();
            info.album = fav.value(QStringLiteral("album")).toString();
            info.duration = fav.value(QStringLiteral("duration")).toInt();
            info.maxQuality = fav.value(QStringLiteral("maxQuality")).toString();
            info.coverUrl = QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(info.id);
            m_allFavorites.append(info);
        }

        if (m_allFavorites.isEmpty()) {
            showPageStatus(I18n::instance().tr(QStringLiteral("emptyFavorites")), "SearchOff");
            updateCoverImage();
            updateHeaderMeta();
            return;
        }

        hidePageStatus();
        updateCoverImage();
        applyFilter();
        if (m_songList)
            m_songList->show();
    });
}

void FavoritesPage::paintEvent(QPaintEvent *)
{
}
