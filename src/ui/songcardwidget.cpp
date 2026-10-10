#include "songcardwidget.h"
#include "roundcoverlabel.h"

#include "core/covercache.h"
#include "core/audioquality.h"
#include "core/i18n.h"
#include "core/listmetaformat.h"
#include "ui/localmusicbadgelabel.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QApplication>
#include <QTimer>
#include <QStyle>
#include <QStyleOption>

namespace {

constexpr int kRowH = 90;
constexpr int kCover = 50;
constexpr QColor kPrimary(240, 94, 122);

/** SPlayer SongCard .song-content — surface / play / hover 边框 */
QColor songFill(bool dark, bool playing, bool hover)
{
    if (playing)
        return QColor(kPrimary.red(), kPrimary.green(), kPrimary.blue(), 56);
    if (hover)
        return dark ? QColor(0x22, 0x25, 0x2d) : QColor(0xf4, 0xf5, 0xf7);
    return dark ? QColor(0x1b, 0x1f, 0x27) : QColor(Qt::white);
}

QColor songBorder(bool playing, bool hover)
{
    if (playing)
        return QColor(kPrimary.red(), kPrimary.green(), kPrimary.blue(), 120);
    if (hover)
        return QColor(kPrimary.red(), kPrimary.green(), kPrimary.blue(), 120);
    return QColor(kPrimary.red(), kPrimary.green(), kPrimary.blue(), 28);
}

QString formatByteSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + QStringLiteral(" B");
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KiB");
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MiB");
}

QString resolvedCoverUrlForSong(const MusicInfo &info)
{
    if (!info.coverUrl.isEmpty())
        return CoverCache::resolveCoverUrl(info.coverUrl);
    if (info.id > 0)
        return QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(info.id);
    return {};
}

QString coverCacheKeyForSong(const MusicInfo &info, const QString &resolvedCoverUrl)
{
    if (!resolvedCoverUrl.isEmpty()) {
        const QString key = CoverCache::musicIdFromCoverUrl(resolvedCoverUrl);
        if (!key.isEmpty())
            return key;
    }
    if (info.id > 0)
        return QString::number(info.id);
    return {};
}

/**
 * 单行省略标签：sizeHint 始终按完整文本计算（保证右侧徽标紧贴文字），
 * minimumSizeHint 宽度收窄到 0（窄窗口下允许布局压缩），
 * 绘制时按当前宽度用省略号截断，避免依赖 resize 时序拿到陈旧宽度而过早截断。
 */
class ElidedLabel : public QLabel
{
public:
    using QLabel::QLabel;

    QSize minimumSizeHint() const override
    {
        const QSize hint = QLabel::minimumSizeHint();
        return QSize(0, hint.height());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QRect content = contentsRect();
        const QString shown =
            fontMetrics().elidedText(text(), Qt::ElideRight, qMax(0, content.width() - indent()));
        QRect target = content;
        target.translate(indent(), 0);

        QStyleOption opt;
        opt.initFrom(this);
        style()->drawItemText(&painter, target, alignment(), palette(), isEnabled(), shown,
                              foregroundRole());
    }
};

} // namespace

SongCardWidget::SongCardWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SongCardWidget"));
    setFixedHeight(kRowH);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, false);
    rebuildLayout();
    applyTheme();
}

void SongCardWidget::setDurationColumnWidth(int width)
{
    m_durationWidth = qMax(24, width);
    if (m_timeLbl)
        m_timeLbl->setFixedWidth(m_durationWidth);
}

void SongCardWidget::rebuildLayout()
{
    if (m_content)
        return;

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("songCardContent"));
    auto *lay = new QHBoxLayout(m_content);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setSpacing(0);

    m_numCol = new QWidget(m_content);
    m_numCol->setFixedWidth(40);

    m_indexLbl = new QLabel(m_numCol);
    m_indexLbl->setGeometry(0, 0, 40, 74);
    m_indexLbl->setAlignment(Qt::AlignCenter);
    m_indexLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_playOverlay = new QPushButton(m_numCol);
    m_playOverlay->setFlat(true);
    m_playOverlay->setFixedSize(28, 28);
    m_playOverlay->setCursor(Qt::PointingHandCursor);
    m_playOverlay->hide();
    connect(m_playOverlay, &QPushButton::clicked, this, [this](bool) {
        if (onPlayNext)
            onPlayNext(m_info);
    });

    m_statusOverlay = new QPushButton(m_numCol);
    m_statusOverlay->setFlat(true);
    m_statusOverlay->setFixedSize(28, 28);
    m_statusOverlay->setCursor(Qt::PointingHandCursor);
    m_statusOverlay->hide();
    connect(m_statusOverlay, &QPushButton::clicked, this, [this](bool) {
        if (onTogglePlayPause)
            onTogglePlayPause();
    });

    lay->addWidget(m_numCol);
    lay->addSpacing(12);

    auto *titleCol = new QWidget(m_content);
    auto *titleLay = new QHBoxLayout(titleCol);
    titleLay->setContentsMargins(0, 4, 20, 4);
    titleLay->setSpacing(0);

    m_coverLbl = new RoundCoverLabel(8, titleCol);
    m_coverLbl->setFixedSize(kCover, kCover);
    titleLay->addWidget(m_coverLbl);
    titleLay->addSpacing(12);

    auto *infoCol = new QWidget(titleCol);
    auto *infoLay = new QVBoxLayout(infoCol);
    infoLay->setContentsMargins(0, 0, 0, 0);
    infoLay->setSpacing(2);

    auto *titleRow = new QWidget(infoCol);
    auto *titleRowLay = new QHBoxLayout(titleRow);
    titleRowLay->setContentsMargins(0, 0, 0, 0);
    titleRowLay->setSpacing(6);
    m_localBadge = new QLabel(titleRow);
    m_localBadge->setVisible(false);
    titleRowLay->addWidget(m_localBadge, 0, Qt::AlignVCenter);
    m_titleLbl = new ElidedLabel(titleRow);
    titleRowLay->addWidget(m_titleLbl, 0, Qt::AlignVCenter);
    m_qualityBadge = new QLabel(titleRow);
    m_qualityBadge->setVisible(false);
    titleRowLay->addWidget(m_qualityBadge, 0, Qt::AlignVCenter);
    m_lrcBadge = new QLabel(titleRow);
    m_lrcBadge->setFixedSize(14, 14);
    m_lrcBadge->setScaledContents(true);
    m_lrcBadge->setVisible(false);
    titleRowLay->addWidget(m_lrcBadge, 0, Qt::AlignVCenter);
    // 徽标紧跟标题，剩余空间留在行尾，避免标题较短时徽标悬在行中间
    titleRowLay->addStretch(1);

    m_artistLbl = new ElidedLabel(infoCol);
    infoLay->addWidget(titleRow);
    infoLay->addWidget(m_artistLbl);
    titleLay->addWidget(infoCol, 1);

    lay->addWidget(titleCol, 1);

    m_albumLbl = new ElidedLabel(m_content);
    m_albumLbl->setMinimumWidth(80);
    lay->addWidget(m_albumLbl, 1);

    m_progressCol = new QWidget(m_content);
    m_progressCol->setMinimumWidth(120);
    auto *progressLay = new QVBoxLayout(m_progressCol);
    progressLay->setContentsMargins(0, 18, 8, 18);
    progressLay->setSpacing(6);
    m_progressLbl = new QLabel(m_progressCol);
    m_progressBar = new QProgressBar(m_progressCol);
    m_progressBar->setFixedHeight(8);
    m_progressBar->setTextVisible(false);
    progressLay->addWidget(m_progressLbl);
    progressLay->addWidget(m_progressBar);
    m_progressCol->hide();
    lay->addWidget(m_progressCol, 1);

    m_heartBtn = new QPushButton(m_content);
    m_heartBtn->setFixedSize(40, 40);
    m_heartBtn->setFlat(true);
    m_heartBtn->setCursor(Qt::PointingHandCursor);
    connect(m_heartBtn, &QPushButton::clicked, this, [this](bool) {
        if (m_downloadTaskMode) {
            if (onCancelDownload)
                onCancelDownload(m_info.id);
            return;
        }
        if (onUnfavorite)
            onUnfavorite(m_info.id);
    });
    lay->addWidget(m_heartBtn);

    m_downloadBtn = new QPushButton(m_content);
    m_downloadBtn->setFixedSize(40, 40);
    m_downloadBtn->setFlat(true);
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    connect(m_downloadBtn, &QPushButton::clicked, this, [this](bool) {
        if (onDownload && !m_downloaded)
            onDownload(m_info);
    });
    lay->addWidget(m_downloadBtn);

    m_timeLbl = new QLabel(m_content);
    m_timeLbl->setFixedWidth(m_durationWidth);
    m_timeLbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(m_timeLbl);

    // 行右侧「三点菜单」入口（设计稿）：与右键菜单共用同一套 onContextMenu 回调，
    // 因此所有使用 SongCardWidget 的界面自动获得统一菜单。
    m_moreBtn = new QPushButton(m_content);
    m_moreBtn->setFixedSize(40, 40);
    m_moreBtn->setFlat(true);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    connect(m_moreBtn, &QPushButton::clicked, this, [this](bool) {
        if (onContextMenu)
            onContextMenu(m_info, m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
    });
    lay->addWidget(m_moreBtn);

    outer->addWidget(m_content, 1);

    for (QLabel *lbl : {m_indexLbl, m_coverLbl, m_titleLbl, m_lrcBadge, m_qualityBadge,
                        m_artistLbl, m_albumLbl, m_progressLbl, m_timeLbl})
        lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    installContentEventFilters();
    applyTheme();
}

void SongCardWidget::installContentEventFilters()
{
    if (!m_content)
        return;

    std::function<void(QWidget *)> walk = [&](QWidget *w) {
        if (!w || isInteractiveButton(w))
            return;
        w->installEventFilter(this);
        for (QObject *child : w->children()) {
            if (auto *cw = qobject_cast<QWidget *>(child))
                walk(cw);
        }
    };
    walk(m_content);
}

bool SongCardWidget::isInteractiveButton(QObject *obj) const
{
    return obj == m_heartBtn || obj == m_downloadBtn || obj == m_moreBtn || obj == m_playOverlay
        || obj == m_statusOverlay;
}

void SongCardWidget::setHover(bool hover)
{
    if (m_hover == hover)
        return;
    m_hover = hover;
    updateHoverOverlays();
    update();
}

void SongCardWidget::syncHoverFromCursor()
{
    QWidget *under = QApplication::widgetAt(QCursor::pos());
    const bool inside = under && (under == this || isAncestorOf(under));
    setHover(inside);
}

bool SongCardWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (isInteractiveButton(watched))
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::Enter:
        setHover(true);
        break;
    case QEvent::Leave:
        QTimer::singleShot(0, this, [this]() { syncHoverFromCursor(); });
        break;
    case QEvent::MouseButtonDblClick: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton && onActivate) {
            onActivate(m_info);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void SongCardWidget::prepareForPool()
{
    disconnect(m_coverConn);
    m_coverConn = {};
    m_coverKey.clear();
    setHover(false);
}

void SongCardWidget::setDisplayMode(DisplayMode mode)
{
    if (m_displayMode == mode)
        return;
    m_displayMode = mode;
    updateSecondaryColumn();
}

void SongCardWidget::bind(const MusicInfo &info, int index)
{
    const QString newCoverUrl = resolvedCoverUrlForSong(info);
    const QString newCoverKey = coverCacheKeyForSong(info, newCoverUrl);
    const bool sameSong = m_info.id == info.id
        && m_info.coverUrl == info.coverUrl
        && m_info.localPath == info.localPath
        && m_info.maxQuality == info.maxQuality
        && m_coverKey == newCoverKey;
    const int oldIndex = m_index;
    m_index = index;

    if (sameSong) {
        if (oldIndex != index)
            updateIndexColumn();
        return;
    }

    disconnect(m_coverConn);
    m_coverConn = {};
    m_coverKey.clear();

    setHover(false);
    m_info = info;
    m_titleLbl->setText(info.title);
    m_artistLbl->setText(info.artist);
    updateLocalBadge();
    updateLrcBadge();
    updateQualityBadge();
    updateDownloadIcon();
    updateSecondaryColumn();
    if (!m_downloadTaskMode)
        m_timeLbl->setText(formatDuration(info.duration));
    else
        updateDownloadTaskUi();

    loadCover();
    updateIndexColumn();
}

void SongCardWidget::loadCover()
{
    const QString url = resolvedCoverUrlForSong(m_info);
    const QString cacheKey = coverCacheKeyForSong(m_info, url);
    m_coverKey = cacheKey;

    if (cacheKey.isEmpty() || url.isEmpty()) {
        QPixmap pm(kCover, kCover);
        pm.fill(QColor(230, 57, 80, 40));
        m_coverLbl->setPixmap(pm);
        return;
    }

    if (QPixmap cached = CoverCache::instance()->get(cacheKey); !cached.isNull()) {
        m_coverLbl->setPixmap(cached);
        return;
    }

    QPixmap pm(kCover, kCover);
    pm.fill(QColor(230, 57, 80, 40));
    m_coverLbl->setPixmap(pm);

    m_coverConn = connect(CoverCache::instance(), &CoverCache::coverLoaded, this,
                          [this, cacheKey](const QString &id, const QPixmap &pix) {
                              if (id == cacheKey) {
                                  m_coverLbl->setPixmap(pix);
                                  m_coverLbl->update();
                              }
                          });
    CoverCache::instance()->fetchCover(cacheKey, url);
}

void SongCardWidget::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    updateIndexColumn();
    updateHoverOverlays();
    update();
}

void SongCardWidget::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    updateHoverOverlays();
    update();
}

void SongCardWidget::setRemoveMode(bool remove)
{
    if (m_removeMode == remove)
        return;
    m_removeMode = remove;
    updateHeartIcon();
}

void SongCardWidget::setFavorited(bool favorited)
{
    if (m_favorited == favorited)
        return;
    m_favorited = favorited;
    updateHeartIcon();
}

void SongCardWidget::setDownloaded(bool downloaded)
{
    if (m_downloaded == downloaded)
        return;
    m_downloaded = downloaded;
    updateDownloadIcon();
}

void SongCardWidget::setShowDownloadButton(bool show)
{
    if (m_showDownloadButton == show)
        return;
    m_showDownloadButton = show;
    if (m_downloadBtn && !m_downloadTaskMode)
        m_downloadBtn->setVisible(show && !m_info.isLocalFile());
}

void SongCardWidget::setDownloadTaskMode(bool enabled)
{
    if (m_downloadTaskMode == enabled)
        return;
    m_downloadTaskMode = enabled;
    setCursor(enabled ? Qt::ArrowCursor : Qt::PointingHandCursor);
    updateDownloadTaskUi();
}

void SongCardWidget::setDownloadTaskState(bool active, qint64 received, qint64 total)
{
    m_downloadActive = active;
    m_progressReceived = received;
    m_progressTotal = total;
    if (m_downloadTaskMode)
        updateDownloadTaskUi();
}

void SongCardWidget::updateDownloadTaskUi()
{
    if (!m_progressCol || !m_progressBar || !m_progressLbl)
        return;

    if (!m_downloadTaskMode) {
        m_progressCol->hide();
        if (m_albumLbl)
            m_albumLbl->show();
        if (m_downloadBtn)
            m_downloadBtn->setVisible(m_showDownloadButton && !m_info.isLocalFile());
        updateHeartIcon();
        updateSecondaryColumn();
        if (m_timeLbl)
            m_timeLbl->setText(formatDuration(m_info.duration));
        return;
    }

    if (m_albumLbl)
        m_albumLbl->hide();
    m_progressCol->show();
    if (m_downloadBtn)
        m_downloadBtn->hide();
    updateCancelIcon();

    if (!m_downloadActive) {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        m_progressLbl->setText(I18n::instance().tr(QStringLiteral("downloadWaiting")));
        if (m_timeLbl)
            m_timeLbl->setText(I18n::instance().tr(QStringLiteral("downloadQueued")));
        return;
    }

    if (m_timeLbl)
        m_timeLbl->setText(I18n::instance().tr(QStringLiteral("downloadingStatus")));

    if (m_progressTotal > 0) {
        const int pct = int(m_progressReceived * 100 / m_progressTotal);
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(qBound(0, pct, 100));
        m_progressLbl->setText(
            QStringLiteral("%1 / %2 (%3%)")
                .arg(formatByteSize(m_progressReceived), formatByteSize(m_progressTotal),
                     QString::number(pct)));
    } else {
        m_progressBar->setRange(0, 0);
        m_progressLbl->setText(formatByteSize(m_progressReceived)
                               + QStringLiteral(" · ")
                               + I18n::instance().tr(QStringLiteral("downloadingStatus")));
    }
}

void SongCardWidget::updateCancelIcon()
{
    if (!m_heartBtn)
        return;
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QColor cancelIc = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
    m_heartBtn->setIcon(Icons::renderNamed("Close", 20, cancelIc));
    m_heartBtn->setIconSize(QSize(20, 20));
    m_heartBtn->setToolTip(I18n::instance().tr(QStringLiteral("cancelDownload")));
    m_heartBtn->setVisible(true);
}

void SongCardWidget::updateHeartIcon()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    if (m_removeMode) {
        const QColor delIc = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
        m_heartBtn->setIcon(Icons::renderNamed("Delete", 20, delIc));
    } else {
        const QColor heartOn(255, 69, 69);
        const QColor heartOff = dark ? QColor(244, 246, 255, 120) : QColor(33, 37, 41, 120);
        m_heartBtn->setIcon(Icons::renderNamed(m_favorited ? "Favorite" : "FavoriteBorder", 20,
                                               m_favorited ? heartOn : heartOff));
    }
    m_heartBtn->setIconSize(QSize(20, 20));
}

void SongCardWidget::updateDownloadIcon()
{
    if (!m_downloadBtn)
        return;
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QColor doneIc(76, 175, 80);
    const QColor normalIc = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
    m_downloadBtn->setIcon(Icons::renderNamed(m_downloaded ? "DownloadDone" : "Download", 20,
                                              m_downloaded ? doneIc : normalIc));
    m_downloadBtn->setIconSize(QSize(20, 20));
    m_downloadBtn->setEnabled(!m_downloaded);
    m_downloadBtn->setVisible(m_showDownloadButton && !m_info.isLocalFile());
}

void SongCardWidget::updateMoreIcon()
{
    if (!m_moreBtn)
        return;
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QColor ic = dark ? QColor(244, 246, 255, 165) : QColor(33, 37, 41, 140);
    m_moreBtn->setIcon(Icons::renderNamed("More", 20, ic));
    m_moreBtn->setIconSize(QSize(20, 20));
}

void SongCardWidget::updateOverlayIcons()
{
    const QColor overlayIc = kPrimary;
    m_playOverlay->setIcon(Icons::renderNamed("Play", 28, overlayIc));
    m_playOverlay->setIconSize(QSize(28, 28));
    m_statusOverlay->setIcon(Icons::renderNamed(m_paused ? "Play" : "Pause", 28, overlayIc));
    m_statusOverlay->setIconSize(QSize(28, 28));
}

void SongCardWidget::applyTheme()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString titleFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QString subFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.62)");
    const QString idxFg = dark ? QString::fromUtf8(Theme::kTextMuted) : QStringLiteral("rgba(33,37,41,0.45)");

    m_indexLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 14px; font-weight: 700; }").arg(idxFg));
    m_titleLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 16px; font-weight: 500; }").arg(titleFg));
    m_artistLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; opacity: 0.6; }").arg(subFg));
    m_albumLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; }").arg(subFg));
    m_timeLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; }").arg(subFg));

    m_heartBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 8px; }"
        "QPushButton:hover { background: rgba(230,57,80,0.15); }"));
    if (m_downloadBtn) {
        m_downloadBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: none; border-radius: 8px; }"
            "QPushButton:hover:enabled { background: rgba(230,57,80,0.15); }"
            "QPushButton:disabled { background: transparent; }"));
    }
    if (m_moreBtn) {
        m_moreBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: none; border-radius: 8px; }"
            "QPushButton:hover { background: rgba(230,57,80,0.15); }"));
    }
    if (m_progressBar) {
        const QString track = dark ? QStringLiteral("#3a3a3a") : QStringLiteral("#e8e8e8");
        m_progressBar->setStyleSheet(QStringLiteral(
            "QProgressBar {"
            "  background: %1;"
            "  border: none;"
            "  border-radius: 4px;"
            "}"
            "QProgressBar::chunk {"
            "  background: #D84B63;"
            "  border-radius: 4px;"
            "}").arg(track));
    }
    if (m_progressLbl) {
        m_progressLbl->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 12px; }")
                                         .arg(dark ? QString::fromUtf8(Theme::kTextSub)
                                                   : QStringLiteral("rgba(33,37,41,0.62)")));
    }
    m_playOverlay->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; }"));
    m_statusOverlay->setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; }"));

    if (m_downloadTaskMode)
        updateDownloadTaskUi();
    else {
        updateHeartIcon();
        updateDownloadIcon();
    }
    updateMoreIcon();
    updateOverlayIcons();
    updateLocalBadge();
    updateLrcBadge();
    updateQualityBadge();
    update();
}

void SongCardWidget::updateLocalBadge()
{
    if (!m_localBadge)
        return;
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    updateLocalMusicBadge(m_localBadge, m_info.isLocalFile(), dark);
}

void SongCardWidget::updateLrcBadge()
{
    if (!m_lrcBadge)
        return;
    const bool show = m_info.lrc;
    m_lrcBadge->setVisible(show);
    if (!show)
        return;
    static const QColor kLrcColor(0xF0, 0x5E, 0x7A);
    m_lrcBadge->setPixmap(Icons::renderNamed("DesktopLyric2", 14, kLrcColor));
    m_lrcBadge->setToolTip(I18n::instance().tr(QStringLiteral("hasLyrics")));
}

void SongCardWidget::updateQualityBadge()
{
    if (!m_qualityBadge)
        return;
    const AudioQuality::Tier tier = AudioQuality::tierFromApiQuality(m_info.maxQuality);
    if (tier == AudioQuality::Tier::Unknown) {
        m_qualityBadge->hide();
        return;
    }

    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    // 每档一个色相：描边 + 15% 同色底，在深色列表里辨识度更高
    QColor fg = AudioQuality::tierColor(tier, dark);
    if (!fg.isValid())
        fg = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 190);
    constexpr int kBadgeH = 16;
    const QByteArray iconName = AudioQuality::tierIconName(tier).toUtf8();
    QPixmap pm = Icons::renderResourceHeight(Icons::resourcePath(iconName.constData()), kBadgeH, fg);
    if (pm.isNull())
        pm = Icons::renderResourceHeight(Icons::resourcePath("HQ"), kBadgeH, fg);

    m_qualityBadge->setPixmap(pm);
    m_qualityBadge->setFixedSize(pm.deviceIndependentSize().toSize());
    m_qualityBadge->setToolTip(AudioQuality::tierTooltip(tier, {}));
    m_qualityBadge->show();
}

void SongCardWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRectF body = QRectF(m_content->geometry());
    if (body.isEmpty())
        body = QRectF(0, 0, width(), height());

    const QRectF r = body.adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, 12, 12);

    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    p.fillPath(path, songFill(dark, m_playing, m_hover && !m_playing));
    p.setPen(QPen(songBorder(m_playing, m_hover && !m_playing), 2));
    p.drawPath(path);
}

void SongCardWidget::enterEvent(QEnterEvent *e)
{
    setHover(true);
    QWidget::enterEvent(e);
}

void SongCardWidget::leaveEvent(QEvent *e)
{
    setHover(false);
    QWidget::leaveEvent(e);
}

void SongCardWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (onContextMenu)
        onContextMenu(m_info, event->globalPos());
    event->accept();
}

void SongCardWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && onActivate)
        onActivate(m_info);
    QWidget::mouseDoubleClickEvent(event);
}

void SongCardWidget::updateIndexColumn()
{
    if (m_playing) {
        m_indexLbl->setPixmap(Icons::renderNamed("Music", 22, kPrimary));
        m_indexLbl->setText(QString());
    } else {
        m_indexLbl->setPixmap(QPixmap());
        m_indexLbl->setText(QString::number(m_index + 1));
    }
    updateHoverOverlays();
    update();
}

void SongCardWidget::updateHoverOverlays()
{
    const bool showPlay = !m_downloadTaskMode && m_hover && !m_playing;
    const bool showStatus = !m_downloadTaskMode && m_hover && m_playing;

    m_indexLbl->setVisible(!showPlay && !showStatus);
    m_playOverlay->setVisible(showPlay);
    m_statusOverlay->setVisible(showStatus);

    if (showStatus) {
        m_statusOverlay->setIcon(Icons::renderNamed(m_paused ? "Play" : "Pause", 28, kPrimary));
    }

    if (m_numCol) {
        QPoint center((m_numCol->width() - 28) / 2, (m_numCol->height() - 28) / 2);
        m_playOverlay->move(center);
        m_statusOverlay->move(center);
    }
}

void SongCardWidget::updateSecondaryColumn()
{
    m_secondaryText = secondaryColumnText();
    if (m_albumLbl)
        m_albumLbl->setText(m_secondaryText);
}

QString SongCardWidget::secondaryColumnText() const
{
    switch (m_displayMode) {
    case DisplayMode::HotRanking:
        return formatPlayCountText(m_info.playCount);
    case DisplayMode::LatestUpload:
        return formatRelativeUploadTime(m_info.uploadedAtMs);
    default:
        break;
    }
    return m_info.album.isEmpty() ? QStringLiteral("—") : m_info.album;
}

QString SongCardWidget::formatDuration(int seconds) const
{
    if (seconds <= 0)
        return QStringLiteral("--:--");
    return QStringLiteral("%1:%2")
        .arg(seconds / 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}
