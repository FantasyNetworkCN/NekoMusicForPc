#include "songlistwidget.h"
#include "songcardwidget.h"
#include "songcontextmenu.h"
#include "songmenubuilder.h"
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
#include <QFontMetrics>
#include <QPainter>
#include <QLinearGradient>
#include <QPixmap>
#include "ui/svgicon.h"
#include "ui/mainwindow.h"

namespace {

constexpr int kCoverGap = 62; // 50 cover + 12 spacing in header title gap
// 数据行 titleLay 的右内边距，表头必须一致才能对齐后面的列
constexpr int kTitleColRightPad = 20;
/** 顶部渐隐向表头方向额外延伸的高度；0 = 上沿与视口顶严格对齐（避免压到表头）。 */
constexpr int kTopOverhang = 0;

/**
 * 列表上下边缘渐隐。
 *
 * 挂在 QScrollArea 的 viewport 上，几何恒等于 viewport->rect()，
 * 所以无论表头折叠、窗口缩放还是列表内边距变化都不会错位。
 * 绘制内容取主窗口当前背景像素，换壁纸/主题自动适配。
 */
class EdgeFadeOverlay final : public QWidget
{
public:
    explicit EdgeFadeOverlay(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setFocusPolicy(Qt::NoFocus);
    }

    void setFades(bool top, bool bottom)
    {
        if (m_top == top && m_bottom == bottom)
            return;
        m_top = top;
        m_bottom = bottom;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if ((!m_top && !m_bottom) || width() <= 0 || height() <= 0)
            return;
        QWidget *win = window();
        if (!win)
            return;

        QPixmap src;
        if (auto *mw = qobject_cast<MainWindow *>(win))
            src = mw->shellBackdropCachePixmap();
        if (src.isNull()) {
            if (auto *mw = qobject_cast<MainWindow *>(win))
                src = mw->shellBackdropPixmapForSize(win->size());
        }
        if (src.isNull())
            return;

        if (m_bg.isNull() || m_bgKey != src.cacheKey() || m_bgSize != win->size()) {
            m_bg = (src.size() == win->size())
                       ? src
                       : src.scaled(win->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            m_bgKey = src.cacheKey();
            m_bgSize = win->size();
        }

        const int topBand = qMin(kTopBandH, height() / 2);
        const int bottomBand = qMin(kBottomBandH, height() / 2);
        if (topBand <= 0 && bottomBand <= 0)
            return;

        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QPoint offset = mapTo(win, QPoint(0, 0));

        const auto paintBand = [&](int y, int band, bool topSide) {
            if (band <= 0)
                return;
            QPixmap strip(width(), band);
            strip.fill(Qt::transparent);
            {
                QPainter bp(&strip);
                bp.drawPixmap(QRect(0, 0, strip.width(), band), m_bg,
                              QRect(offset.x(), offset.y() + y, strip.width(), band));
                QLinearGradient g(0, 0, 0, band);
                if (topSide) {
                    g.setColorAt(0.0, QColor(0, 0, 0, 255));
                    g.setColorAt(1.0, QColor(0, 0, 0, 0));
                } else {
                    g.setColorAt(0.0, QColor(0, 0, 0, 0));
                    g.setColorAt(1.0, QColor(0, 0, 0, 255));
                }
                bp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
                bp.fillRect(strip.rect(), g);
            }
            p.drawPixmap(0, y, strip);
        };

        if (m_top)
            paintBand(0, topBand, true);
        if (m_bottom)
            paintBand(height() - bottomBand, bottomBand, false);
    }

private:
    // 上/下渐隐带高度分开控制：
    //   顶部 24px —— 尽量少吃歌曲内容；
    //   底部 32px —— 往下多铺一点，和底栏之间的过渡更缓。
    static constexpr int kTopBandH = 24;
    static constexpr int kBottomBandH = 32;
    bool m_top = false;
    bool m_bottom = false;
    QPixmap m_bg;
    qint64 m_bgKey = 0;
    QSize m_bgSize;
};

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

    // 与数据行同构：标题列是一个 stretch 容器，封面占位与 20px 右内边距都在列内，
    // 这样表头的「专辑」「操作」「时长」才能和行内列边界对齐。
    auto *hdrTitleCol = new QWidget(m_header);
    auto *hdrTitleLay = new QHBoxLayout(hdrTitleCol);
    hdrTitleLay->setContentsMargins(0, 0, kTitleColRightPad, 0);
    hdrTitleLay->setSpacing(0);

    auto *titleGap = new QWidget(hdrTitleCol);
    titleGap->setFixedWidth(kCoverGap);
    hdrTitleLay->addWidget(titleGap);

    m_hdrTitle = new QLabel(I18n::instance().tr(QStringLiteral("listColTitle")), hdrTitleCol);
    hdrTitleLay->addWidget(m_hdrTitle, 1);
    hdrLay->addWidget(hdrTitleCol, 1);

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
    // 与数据行最右侧的「三点菜单」按钮对齐（见 SongCardWidget::m_moreBtn）
    hdrLay->addSpacing(40);

    root->addWidget(m_header);
    updateHeaderColumnWidths();

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("favoritesSongScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);
    // 容器的最小高度 = 全部歌曲内容高度，QScrollArea 会把它算进 minimumSizeHint，
    // 导致整个列表把页面撑得比窗口还高（底部被裁掉，底栏也压不住）。
    // 这里明确允许滚动区收缩到 0，把"内容多高"和"控件多高"彻底解耦。
    m_scroll->setMinimumHeight(0);
    m_scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);

    m_container = new QWidget(m_scroll);
    m_container->setObjectName(QStringLiteral("songListContainer"));
    m_scroll->setWidget(m_container);
    nekoPolishScrollAreaViewport(m_scroll);

    root->addWidget(m_scroll, 1);

    // 边缘渐隐浮层：直接挂在滚动视口上，几何 = 视口 rect()，
    // 表头折叠/窗口缩放/内边距变化都自动跟随，不会错位。
    m_edgeFade = new EdgeFadeOverlay(m_scroll->viewport());
    m_edgeFade->hide();
    if (QScrollBar *bar = m_scroll->verticalScrollBar()) {
        connect(bar, &QScrollBar::valueChanged, this, [this]() { updateEdgeFade(); });
        connect(bar, &QScrollBar::rangeChanged, this, [this]() { updateEdgeFade(); });
    }

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
    updateHeaderColumnWidths();
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

void SongListWidget::setBatchMode(bool enabled)
{
    if (m_batchMode == enabled)
        return;

    m_batchMode = enabled;
    if (!enabled)
        m_batchSelectedIds.clear();
    m_rowBatchChecked.clear();

    for (SongCardWidget *card : m_rowCards)
        card->setBatchMode(enabled);
    for (SongCardWidget *card : m_cardPool)
        card->setBatchMode(enabled);

    syncBatchToVisibleRows();
    emit batchModeChanged(enabled);
    emit batchSelectionChanged(m_batchSelectedIds.size());
}

void SongListWidget::clearBatchSelection()
{
    if (m_batchSelectedIds.isEmpty())
        return;
    m_batchSelectedIds.clear();
    m_rowBatchChecked.clear();
    syncBatchToVisibleRows();
    emit batchSelectionChanged(0);
}

QList<MusicInfo> SongListWidget::batchSelectedSongs() const
{
    QList<MusicInfo> picked;
    if (m_batchSelectedIds.isEmpty())
        return picked;
    picked.reserve(m_batchSelectedIds.size());
    for (const MusicInfo &song : m_songs) {
        if (m_batchSelectedIds.contains(song.id))
            picked.append(song);
    }
    return picked;
}

bool SongListWidget::batchAllSelected() const
{
    return !m_songs.isEmpty() && m_batchSelectedIds.size() >= m_songs.size();
}

void SongListWidget::setBatchSelectAll(bool selectAll)
{
    if (selectAll)
        pruneBatchSelection();
    m_batchSelectedIds.clear();
    if (selectAll) {
        for (const MusicInfo &song : m_songs)
            m_batchSelectedIds.insert(song.id);
    }
    m_rowBatchChecked.clear();
    syncBatchToVisibleRows();
    emit batchSelectionChanged(m_batchSelectedIds.size());
}

void SongListWidget::toggleBatchSelection(const MusicInfo &info)
{
    if (!m_batchMode)
        return;
    if (m_batchSelectedIds.contains(info.id))
        m_batchSelectedIds.remove(info.id);
    else
        m_batchSelectedIds.insert(info.id);
    syncBatchToVisibleRows();
    emit batchSelectionChanged(m_batchSelectedIds.size());
}

void SongListWidget::syncBatchToVisibleRows()
{
    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it) {
        SongCardWidget *card = it.value();
        if (!card)
            continue;
        const bool checked = m_batchMode && m_batchSelectedIds.contains(card->info().id);
        if (checked)
            m_rowBatchChecked.insert(it.key());
        else
            m_rowBatchChecked.remove(it.key());
        card->setBatchChecked(checked);
    }
}

void SongListWidget::pruneBatchSelection()
{
    if (m_batchSelectedIds.isEmpty())
        return;
    QSet<int> valid;
    valid.reserve(m_songs.size());
    for (const MusicInfo &song : m_songs)
        valid.insert(song.id);
    m_batchSelectedIds.intersect(valid);
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

    // 列表内容被整批替换（切页 / 刷新 / 过滤）时，选中集合里已经不存在的曲目要丢掉，
    // 否则「已选 N 首」会把看不见的曲目也算进去。
    const int before = m_batchSelectedIds.size();
    pruneBatchSelection();
    m_rowBatchChecked.clear();
    if (m_batchSelectedIds.size() != before)
        emit batchSelectionChanged(m_batchSelectedIds.size());

    syncContainerHeight();
    if (songs.size() > 40)
        scheduleVisibleUpdate();
    else
        updateVisibleRows();
    updateEdgeFade();
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
        "  border: 1px solid rgba(22, 119, 184,0.28);"
        "  border-radius: 21px;"
        "}"
        "QPushButton:hover { background: %2; }")
                                 .arg(dark ? QStringLiteral("#223149") : QStringLiteral("#ffffff"),
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
            "QScrollBar::handle:vertical { background: rgba(22, 119, 184,%1); border-radius: 3px; min-height: 40px; }"
            "QScrollBar::handle:vertical:hover { background: rgba(22, 119, 184,%2); }"
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

void SongListWidget::updateHeaderColumnWidths()
{
    auto textPx = [](const QLabel *label, int padding) -> int {
        if (!label || label->text().isEmpty())
            return 0;
        return label->fontMetrics().horizontalAdvance(label->text()) + padding;
    };

    // 「操作」列必须覆盖数据行里的收藏 + 下载两个按钮，否则「专辑」「操作」列都会错位。
    if (m_hdrActions) {
        const int actionCols = m_showDownloadActions ? 80 : 40;
        m_hdrActions->setFixedWidth(qMax(actionCols, textPx(m_hdrActions, 12)));
    }

    // 「时长」列需与数据行的时间标签保持一致，否则会错位或裁切。
    if (m_hdrDuration) {
        m_durationColumnWidth = qMax(50, textPx(m_hdrDuration, 16));
        m_hdrDuration->setFixedWidth(m_durationColumnWidth);
    }

    for (SongCardWidget *card : m_rowCards)
        card->setDurationColumnWidth(m_durationColumnWidth);
    for (SongCardWidget *card : m_cardPool)
        card->setDurationColumnWidth(m_durationColumnWidth);
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
    updateHeaderColumnWidths();
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

void SongListWidget::updateEdgeFade()
{
    if (!m_edgeFade || !m_scroll || !m_scroll->viewport())
        return;
    QWidget *vp = m_scroll->viewport();

    // 浮层挂在列表控件（而不是视口）上，并向表头方向多延伸 kTopOverhang：
    // 渐隐最上面那一段压在表头的空白里，真正被淡掉的歌曲内容更少，"不浪费空间"。
    if (m_edgeFade->parentWidget() != this)
        m_edgeFade->setParent(this);
    QRect geo(vp->mapTo(this, QPoint(0, 0)), vp->size());
    geo.setTop(qMax(0, geo.top() - kTopOverhang));
    geo = geo.intersected(rect());
    if (geo.isEmpty()) {
        m_edgeFade->hide();
        return;
    }
    if (m_edgeFade->geometry() != geo)
        m_edgeFade->setGeometry(geo);
    m_edgeFade->raise();

    QScrollBar *bar = m_scroll->verticalScrollBar();
    const int value = bar ? bar->value() : 0;
    const int maxValue = bar ? bar->maximum() : 0;
    // 停在顶部时不要画顶部渐隐（否则第一行会被吃掉半截）；
    // 滚到底部时同理不画底部渐隐。
    const bool canTop = value > 4;
    const bool canBottom = (maxValue - value) > 4;
    static_cast<EdgeFadeOverlay *>(m_edgeFade)->setFades(canTop, canBottom);
    m_edgeFade->setVisible(canTop || canBottom);
}

void SongListWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    syncContainerHeight();
    updateVisibleRows();
    updateEdgeFade();
    QTimer::singleShot(0, this, [this]() {
        syncContainerHeight();
        updateVisibleRows();
        updateEdgeFade();
    });
}

void SongListWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    syncContainerHeight();
    scheduleVisibleUpdate();
    updateEdgeFade();

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

void SongListWidget::showStandardContextMenu(const MusicInfo &info, const QPoint &globalPos)
{
    if (info.id <= 0 && info.localPath.isEmpty())
        return;

    SongMenuBuilder::State state;
    state.favorited = isFavorited ? isFavorited(info.id) : false;
    state.downloaded = isDownloaded ? isDownloaded(info.id) : false;
    state.inPlayQueue = isInPlayQueue ? isInPlayQueue(info.id)
                                      : SongMenuBuilder::queueContains(info.id);

    SongMenuBuilder::Handlers handlers;
    handlers.playNext = [info]() { SongMenuBuilder::playNextNow(info); }; // 下一首播放（全局实现）
    if (onToggleFavorite)
        handlers.toggleFavorite = [this, info]() { onToggleFavorite(info); };
    else if (onUnfavorite && state.favorited)
        handlers.toggleFavorite = [this, info]() { onUnfavorite(info.id); };
    if (onToggleQueue && isInPlayQueue)
        handlers.toggleQueue = [this, info]() { onToggleQueue(info); };
    else
        handlers.toggleQueue = [info]() { SongMenuBuilder::toggleQueue(info); }; // 回退到全局播放队列
    if (onDownload && !state.downloaded)
        handlers.download = [this, info]() { onDownload(info); };

    // 没有动作的菜单项直接不显示，避免出现点了没反应的死项
    state.canFavorite = static_cast<bool>(handlers.toggleFavorite);
    state.canQueue = static_cast<bool>(handlers.toggleQueue);
    state.canDownload = static_cast<bool>(handlers.download);

    const QList<SongContextMenuPopup::Entry> entries =
        SongMenuBuilder::buildStandard(info, state, handlers);
    if (entries.isEmpty())
        return;

    SongContextMenuPopup::showAt(window() ? window() : this, globalPos, entries);
}

SongCardWidget *SongListWidget::acquireCard()
{
    SongCardWidget *card = m_cardPool.isEmpty() ? new SongCardWidget(m_container)
                                                : m_cardPool.takeLast();
    card->setDurationColumnWidth(m_durationColumnWidth);
    return card;
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
    card->onBatchToggle = nullptr;
    card->setDownloadTaskMode(false);
    card->setBatchChecked(false);
    m_cardPool.append(card);
}
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
    for (int row : stale) {
        m_rowBatchChecked.remove(row);
        releaseCard(m_rowCards.take(row));
    }

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
            // 页面未接管右键菜单时，统一使用设计稿的菜单（收藏 / 播放队列 / 下载）
            card->onContextMenu = [this](const MusicInfo &info, const QPoint &pos) {
                if (onSongContextMenu) {
                    onSongContextMenu(info, pos);
                    return;
                }
                showStandardContextMenu(info, pos);
            };
            card->onUnfavorite = onUnfavorite;
            card->onDownload = onDownload;
            card->onCancelDownload = onCancelDownload;
            card->onTogglePlayPause = onTogglePlayPause;
            card->onBatchToggle = [this](const MusicInfo &info) { toggleBatchSelection(info); };
            card->setRemoveMode(m_removeMode);
            card->setShowDownloadButton(m_showDownloadActions);
            card->setDownloadTaskMode(m_downloadTaskMode);
            card->setBatchMode(m_batchMode);
            card->setDisplayMode(static_cast<SongCardWidget::DisplayMode>(m_listDisplayMode));
        }

        const bool songChanged = isNew || card->info().id != song.id;
        card->bind(song, row);
        if (songChanged) {
            card->setFavorited(isFavorited ? isFavorited(song.id) : false);
            card->setDownloaded(isDownloaded ? isDownloaded(song.id) : false);
        }
        // 行被回收复用后选中态必须按 music_id 重新回灌，否则滚动会把选中态带错行
        if (m_batchMode || m_rowBatchChecked.contains(row)) {
            const bool checked = m_batchSelectedIds.contains(song.id);
            if (checked)
                m_rowBatchChecked.insert(row);
            else
                m_rowBatchChecked.remove(row);
            card->setBatchChecked(checked);
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
