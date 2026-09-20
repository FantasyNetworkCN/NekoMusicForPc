#include "songlistwidget.h"
#include "songcardwidget.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/scrollareafix.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QFrame>
#include <QPushButton>
#include <QTimer>
#include "ui/svgicon.h"

namespace {

constexpr int kCoverGap = 62; // 50 cover + 12 spacing in header title gap

} // namespace

SongListWidget::SongListWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SongListWidget"));
    setupUi();
}

void SongListWidget::setListDisplayMode(ListDisplayMode mode)
{
    if (m_listDisplayMode == mode)
        return;
    m_listDisplayMode = mode;
    retranslate();
    for (SongCardWidget *card : m_rowCards)
        card->setDisplayMode(static_cast<SongCardWidget::DisplayMode>(mode));
    for (SongCardWidget *card : m_cardPool)
        card->setDisplayMode(static_cast<SongCardWidget::DisplayMode>(mode));
}

void SongListWidget::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_header = new QWidget(this);
    m_header->setObjectName(QStringLiteral("songListHeader"));
    m_header->setFixedHeight(kHeaderHeight);
    auto *hdrLay = new QHBoxLayout(m_header);
    hdrLay->setContentsMargins(12, 8, 12, 8);
    hdrLay->setSpacing(0);

    m_hdrNum = new QLabel(QStringLiteral("#"), m_header);
    m_hdrNum->setFixedWidth(40);
    m_hdrNum->setAlignment(Qt::AlignCenter);
    hdrLay->addWidget(m_hdrNum);
    hdrLay->addSpacing(12);

    auto *titleGap = new QWidget(m_header);
    titleGap->setFixedWidth(kCoverGap);
    hdrLay->addWidget(titleGap);

    m_hdrTitle = new QLabel(I18n::instance().tr(QStringLiteral("listColTitle")), m_header);
    hdrLay->addWidget(m_hdrTitle, 1);

    m_hdrAlbum = new QLabel(I18n::instance().tr(QStringLiteral("listColAlbum")), m_header);
    hdrLay->addWidget(m_hdrAlbum, 1);

    m_hdrActions = new QLabel(I18n::instance().tr(QStringLiteral("listColActions")), m_header);
    m_hdrActions->setFixedWidth(40);
    m_hdrActions->setAlignment(Qt::AlignCenter);
    hdrLay->addWidget(m_hdrActions);

    m_hdrDuration = new QLabel(I18n::instance().tr(QStringLiteral("duration")), m_header);
    m_hdrDuration->setFixedWidth(50);
    m_hdrDuration->setAlignment(Qt::AlignCenter);
    hdrLay->addWidget(m_hdrDuration);

    root->addWidget(m_header);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("favoritesSongScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_container = new QWidget(m_scroll);
    m_container->setObjectName(QStringLiteral("songListContainer"));
    m_scroll->setWidget(m_container);
    nekoPolishScrollAreaViewport(m_scroll);

    root->addWidget(m_scroll, 1);

    m_scrollCurrentBtn = new QPushButton(this);
    m_scrollCurrentBtn->setFixedSize(42, 42);
    m_scrollCurrentBtn->setCursor(Qt::PointingHandCursor);
    m_scrollCurrentBtn->setFlat(true);
    m_scrollCurrentBtn->hide();
    connect(m_scrollCurrentBtn, &QPushButton::clicked, this, &SongListWidget::scrollToPlaying);

    m_scrollTopBtn = new QPushButton(this);
    m_scrollTopBtn->setFixedSize(42, 42);
    m_scrollTopBtn->setCursor(Qt::PointingHandCursor);
    m_scrollTopBtn->setFlat(true);
    m_scrollTopBtn->hide();
    connect(m_scrollTopBtn, &QPushButton::clicked, this, &SongListWidget::scrollToTop);

    m_visibleUpdateTimer = new QTimer(this);
    m_visibleUpdateTimer->setSingleShot(true);
    m_visibleUpdateTimer->setInterval(16);
    connect(m_visibleUpdateTimer, &QTimer::timeout, this, &SongListWidget::updateVisibleRows);

    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        emit scrolled(v);
        scheduleVisibleUpdate();
        if (m_scrollTopBtn)
            m_scrollTopBtn->setVisible(v > 100);
        if (m_scrollCurrentBtn)
            m_scrollCurrentBtn->setVisible(hasCurrentPlaying());
    });

    applyTheme();
}

void SongListWidget::refreshFavoriteDisplay()
{
    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it) {
        SongCardWidget *card = it.value();
        const bool fav = isFavorited ? isFavorited(card->info().id) : false;
        card->setFavorited(fav);
    }
}

void SongListWidget::refreshDownloadDisplay()
{
    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it) {
        SongCardWidget *card = it.value();
        const bool downloaded = isDownloaded ? isDownloaded(card->info().id) : false;
        card->setDownloaded(downloaded);
    }
}

void SongListWidget::setShowDownloadActions(bool show)
{
    if (m_showDownloadActions == show)
        return;
    m_showDownloadActions = show;
    for (SongCardWidget *card : m_rowCards)
        card->setShowDownloadButton(show);
}

void SongListWidget::setDownloadTaskMode(bool enabled)
{
    if (m_downloadTaskMode == enabled)
        return;
    m_downloadTaskMode = enabled;
    for (SongCardWidget *card : m_rowCards)
        card->setDownloadTaskMode(enabled);
    for (SongCardWidget *card : m_cardPool)
        card->setDownloadTaskMode(enabled);
    updateVisibleRows();
}

void SongListWidget::refreshDownloadProgress()
{
    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it) {
        SongCardWidget *card = it.value();
        const int id = card->info().id;
        const bool active = isActiveDownload ? isActiveDownload(id) : false;
        const qint64 received = downloadProgressReceived ? downloadProgressReceived(id) : 0;
        const qint64 total = downloadProgressTotal ? downloadProgressTotal(id) : 0;
        card->setDownloadTaskState(active, received, total);
    }
}

void SongListWidget::setRemoveMode(bool remove)
{
    if (m_removeMode == remove)
        return;
    m_removeMode = remove;
    for (SongCardWidget *card : m_rowCards)
        card->setRemoveMode(remove);
    for (SongCardWidget *card : m_cardPool)
        card->setRemoveMode(remove);
}

bool SongListWidget::hasCurrentPlaying() const
{
    return m_currentRowInList >= 0;
}

void SongListWidget::rebuildCurrentRowCache()
{
    m_currentRowInList = -1;
    if (m_currentId < 0)
        return;
    for (int i = 0; i < m_songs.size(); ++i) {
        if (m_songs[i].id == m_currentId) {
            m_currentRowInList = i;
            return;
        }
    }
}

void SongListWidget::scheduleVisibleUpdate()
{
    if (!m_visibleUpdateTimer)
        return;
    if (!m_visibleUpdateTimer->isActive())
        m_visibleUpdateTimer->start();
}

void SongListWidget::setSongs(const QList<MusicInfo> &songs)
{
    const QList<int> rows = m_rowCards.keys();
    for (int row : rows)
        releaseCard(m_rowCards.take(row));

    m_songs = songs;
    rebuildCurrentRowCache();
    syncContainerHeight();
    if (songs.size() > 40)
        scheduleVisibleUpdate();
    else
        updateVisibleRows();
}

void SongListWidget::setCurrentPlayingId(int musicId)
{
    if (m_currentId == musicId)
        return;
    m_currentId = musicId;
    rebuildCurrentRowCache();
    refreshPlayingState();
    if (m_scrollCurrentBtn)
        m_scrollCurrentBtn->setVisible(hasCurrentPlaying());
}

void SongListWidget::setPlaybackPaused(bool paused)
{
    m_paused = paused;
    refreshPlayingState();
}

void SongListWidget::refreshPlayingState()
{
    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it) {
        SongCardWidget *card = it.value();
        const bool playing = card->info().id == m_currentId;
        card->setPlaying(playing);
        card->setPaused(playing && m_paused);
    }
}

void SongListWidget::applyTheme()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString hdrFg = dark ? QString::fromUtf8(Theme::kTextMuted) : QStringLiteral("rgba(33,37,41,0.55)");
    const QString hdrStyle = QStringLiteral(
        "QLabel { font-size: 13px; font-weight: 700; color: %1; opacity: 0.6; }").arg(hdrFg);

    for (QLabel *lbl : {m_hdrNum, m_hdrTitle, m_hdrAlbum, m_hdrActions, m_hdrDuration}) {
        if (lbl)
            lbl->setStyleSheet(hdrStyle);
    }

    if (m_header) {
        m_header->setStyleSheet(QStringLiteral(
            "QWidget#songListHeader { background: transparent; border: 1px solid transparent; }"));
    }

    const QString floatBtn = QStringLiteral(
        "QPushButton {"
        "  background: %1;"
        "  border: 1px solid rgba(230,57,80,0.28);"
        "  border-radius: 21px;"
        "}"
        "QPushButton:hover { background: %2; }")
                                 .arg(dark ? QStringLiteral("#2a2a2a") : QStringLiteral("#ffffff"),
                                      dark ? QStringLiteral("#333333") : QStringLiteral("#f5f5f5"));
    const QColor floatIc = dark ? QColor(244, 246, 255, 210) : QColor(33, 37, 41, 210);
    if (m_scrollCurrentBtn) {
        m_scrollCurrentBtn->setIcon(Icons::renderNamed("Location", 22, floatIc));
        m_scrollCurrentBtn->setIconSize(QSize(22, 22));
        m_scrollCurrentBtn->setStyleSheet(floatBtn);
    }
    if (m_scrollTopBtn) {
        m_scrollTopBtn->setIcon(Icons::renderNamed("Up", 22, floatIc));
        m_scrollTopBtn->setIconSize(QSize(22, 22));
        m_scrollTopBtn->setStyleSheet(floatBtn);
    }

    if (m_scroll) {
        m_scroll->setStyleSheet(QStringLiteral(
            "QScrollArea#favoritesSongScroll { border: none; background: transparent; }"
            "QScrollBar:vertical { width: 6px; background: transparent; margin: 2px 0; }"
            "QScrollBar::handle:vertical { background: rgba(230,57,80,%1); border-radius: 3px; min-height: 40px; }"
            "QScrollBar::handle:vertical:hover { background: rgba(230,57,80,%2); }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
                                    .arg(dark ? 70 : 82)
                                    .arg(dark ? 108 : 125));
    }

    if (m_container) {
        m_container->setStyleSheet(QStringLiteral(
            "QWidget#songListContainer { background: transparent; }"));
    }

    for (SongCardWidget *card : m_rowCards)
        card->applyTheme();
    for (SongCardWidget *card : m_cardPool)
        card->applyTheme();
}

void SongListWidget::retranslate()
{
    auto &i18n = I18n::instance();
    if (m_hdrNum) {
        m_hdrNum->setText(m_listDisplayMode == ListDisplayMode::HotRanking
                              ? i18n.tr(QStringLiteral("listColRank"))
                              : QStringLiteral("#"));
    }
    if (m_hdrTitle)
        m_hdrTitle->setText(i18n.tr(QStringLiteral("listColTitle")));
    if (m_hdrAlbum) {
        const char *albumKey = "listColAlbum";
        if (m_listDisplayMode == ListDisplayMode::HotRanking)
            albumKey = "listColPlayCount";
        else if (m_listDisplayMode == ListDisplayMode::LatestUpload)
            albumKey = "listColUploaded";
        m_hdrAlbum->setText(i18n.tr(QString::fromUtf8(albumKey)));
    }
    if (m_hdrActions)
        m_hdrActions->setText(i18n.tr(QStringLiteral("listColActions")));
    if (m_hdrDuration)
        m_hdrDuration->setText(i18n.tr(QStringLiteral("duration")));
}

void SongListWidget::scrollToTop()
{
    if (m_scroll)
        m_scroll->verticalScrollBar()->setValue(0);
}

int SongListWidget::scrollOffset() const
{
    return m_scroll ? m_scroll->verticalScrollBar()->value() : 0;
}

void SongListWidget::scrollToOffset(int offset)
{
    if (!m_scroll)
        return;
    const int target = qMax(0, offset);
    m_scroll->verticalScrollBar()->setValue(target);
    scheduleVisibleUpdate();
    // setSongs 之后容器高度要等一次布局，滚动条范围此刻可能还偏小，
    // 直接 setValue 会被夹断；延后一帧再对齐一次（setValue 自带夹取，不会越界）。
    QTimer::singleShot(0, this, [this, target]() {
        if (m_scroll)
            m_scroll->verticalScrollBar()->setValue(target);
    });
}

void SongListWidget::scrollToPlaying()
{
    if (m_currentRowInList < 0 || !m_scroll)
        return;
    const int row = m_currentRowInList;
    const int y = kListPad + row * kRowStride;
    const int viewH = m_scroll->viewport()->height();
    int scrollVal = m_scroll->verticalScrollBar()->value();
    if (y < scrollVal)
        scrollVal = y;
    else if (y + kRowHeight > scrollVal + viewH)
        scrollVal = y + kRowHeight - viewH;
    m_scroll->verticalScrollBar()->setValue(scrollVal);
    scheduleVisibleUpdate();
}

void SongListWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    syncContainerHeight();
    updateVisibleRows();
    QTimer::singleShot(0, this, [this]() {
        syncContainerHeight();
        updateVisibleRows();
    });
}

void SongListWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    syncContainerHeight();
    scheduleVisibleUpdate();

    const int right = width() - 52;
    const int bottom = height() - 100;
    if (m_scrollCurrentBtn)
        m_scrollCurrentBtn->move(right, bottom - 50);
    if (m_scrollTopBtn)
        m_scrollTopBtn->move(right, bottom);
}

void SongListWidget::syncContainerHeight()
{
    const int count = m_songs.size();
    const int contentH = count > 0 ? count * kRowStride - kRowGap + 80 : 0;
    const int viewH = m_scroll ? m_scroll->viewport()->height() : 0;
    m_container->setMinimumHeight(qMax(contentH, viewH));
    if (m_scroll)
        m_container->setFixedWidth(m_scroll->viewport()->width());
}

SongCardWidget *SongListWidget::acquireCard()
{
    if (m_cardPool.isEmpty())
        return new SongCardWidget(m_container);
    return m_cardPool.takeLast();
}

void SongListWidget::releaseCard(SongCardWidget *card)
{
    if (!card)
        return;
    card->prepareForPool();
    card->hide();
    card->onActivate = nullptr;
    card->onPlayNext = nullptr;
    card->onContextMenu = nullptr;
    card->onUnfavorite = nullptr;
    card->onDownload = nullptr;
    card->onCancelDownload = nullptr;
    card->onTogglePlayPause = nullptr;
    card->setDownloadTaskMode(false);
    m_cardPool.append(card);
}

void SongListWidget::updateVisibleRows()
{
    const int count = m_songs.size();
    if (count <= 0 || !m_scroll)
        return;

    const int viewW = m_scroll->viewport() ? m_scroll->viewport()->width() : 0;
    if (viewW <= 80) {
        scheduleVisibleUpdate();
        return;
    }

    const int scrollY = m_scroll->verticalScrollBar()->value();
    const int viewH = m_scroll->viewport()->height();
    const int first = qMax(0, scrollY / kRowStride - kVisibleBuffer);
    const int last = qMin(count - 1, (scrollY + viewH) / kRowStride + kVisibleBuffer);

    QList<int> stale;
    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it) {
        if (it.key() < first || it.key() > last)
            stale.append(it.key());
    }
    for (int row : stale)
        releaseCard(m_rowCards.take(row));

    const int w = viewW;
    for (int row = first; row <= last; ++row) {
        const MusicInfo &song = m_songs[row];
        SongCardWidget *card = m_rowCards.value(row, nullptr);
        const bool isNew = !card;
        if (isNew) {
            card = acquireCard();
            card->setParent(m_container);
            m_rowCards[row] = card;
            card->onActivate = onSongActivate;
            card->onPlayNext = onSongPlayNext;
            card->onContextMenu = onSongContextMenu;
            card->onUnfavorite = onUnfavorite;
            card->onDownload = onDownload;
            card->onCancelDownload = onCancelDownload;
            card->onTogglePlayPause = onTogglePlayPause;
            card->setRemoveMode(m_removeMode);
            card->setShowDownloadButton(m_showDownloadActions);
            card->setDownloadTaskMode(m_downloadTaskMode);
            card->setDisplayMode(static_cast<SongCardWidget::DisplayMode>(m_listDisplayMode));
        }

        const bool songChanged = isNew || card->info().id != song.id;
        card->bind(song, row);
        if (songChanged) {
            card->setFavorited(isFavorited ? isFavorited(song.id) : false);
            card->setDownloaded(isDownloaded ? isDownloaded(song.id) : false);
        }
        if (m_downloadTaskMode) {
            const bool active = isActiveDownload ? isActiveDownload(song.id) : false;
            const qint64 received = downloadProgressReceived ? downloadProgressReceived(song.id) : 0;
            const qint64 total = downloadProgressTotal ? downloadProgressTotal(song.id) : 0;
            card->setDownloadTaskState(active, received, total);
        }

        card->setFixedSize(w, kRowHeight);
        card->move(0, row * kRowStride);
        card->setPlaying(song.id == m_currentId);
        card->setPaused(song.id == m_currentId && m_paused);
        card->show();
    }
}
