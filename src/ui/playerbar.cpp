/**
 * @file playerbar.cpp
 * @brief 播放栏实现
 *
 * 80px，SPlayer 式三列底栏：顶栏进度 + 左信息 / 中控 / 右时间工具。
 * 播放按钮薰衣草渐变，进度条薰衣草填充。
 */

#include "playerbar.h"
#include "core/shellbackdropsettings.h"
#include "playerprogressslider.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/glasswidget.h"
#include "ui/glasspaint.h"
#include "core/playerengine.h"
#include "core/playlistmanager.h"
#include "ui/svgicon.h"
#include "core/i18n.h"
#include "core/covercache.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QStandardItemModel>
#include <QStyle>
#include <QStylePainter>
#include <QStyleOptionButton>
#include <QFont>
#include <QSignalBlocker>
#include <QDebug>
#include <QTimer>
#include <QEvent>
#include <QCursor>
#include <QRect>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QSettings>
#include <QUrl>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QFontMetrics>
#include <QVariantAnimation>
#include <QElapsedTimer>
#include <QRectF>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <functional>

namespace {

/** 底栏封面边长（略大于 SPlayer 56px，仍适配 80px 栏高） */
constexpr int kPbCoverSize = 64;
constexpr int kPbCoverRadius = 8;
constexpr int kPbTitleLineH = 22;
constexpr int kPbArtistLineH = 18;
constexpr int kPbInfoLineGap = 1;
constexpr int kPbHeartBtn = 24;
constexpr int kPbHeartIcon = 20;
constexpr int kPbMarqueeIntervalMs = 32;
constexpr int kPbMarqueeFrameMs = 16;
constexpr int kPbMarqueePauseTicks = 45;
constexpr qreal kPbLyricMarqueeSpeed = 2.0;

/** 底栏歌名 / 歌词：限宽显示，超出后向左滚动一次（不循环），到末尾即停 */
class PbMarqueeLabel final : public QLabel {
public:
    explicit PbMarqueeLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setWordWrap(false);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setContentsMargins(0, 0, 0, 0);

        m_timer.setTimerType(Qt::PreciseTimer);
        connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
        m_startDelay.setSingleShot(true);
        connect(&m_startDelay, &QTimer::timeout, this, [this]() { beginScroll(); });
    }

    void setLineHeight(int height)
    {
        height = qMax(12, height);
        if (m_lineHeight == height)
            return;
        m_lineHeight = height;
        rebuildMetrics();
    }

    /** 跑马灯速度倍率（底栏歌词行默认 2×） */
    void setMarqueeSpeed(qreal multiplier)
    {
        m_speedMul = qBound(0.5, multiplier, 8.0);
    }

    QString fullText() const { return m_fullText; }

    void setText(const QString &text)
    {
        m_fullText = text;
        QLabel::clear();
        rebuildMetrics();
        restartScroll(); // 从起点重新开始（带一次起始停顿）
    }

    QString text() const { return m_fullText; }

    void setMaxDisplayWidth(int width)
    {
        width = qMax(24, width);
        if (m_maxWidth == width)
            return;
        m_maxWidth = width;
        rebuildMetrics();
    }

    int maxDisplayWidth() const { return m_maxWidth; }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        if (m_fullText.isEmpty())
            return;

        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setFont(font());
        p.setPen(palette().color(QPalette::WindowText));
        const QRect clip = rect();
        p.setClipRect(clip);

        if (!m_scrolling) {
            p.drawText(clip, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_fullText);
            return;
        }

        // 只画一份，位置由已过时间算出并夹在 [0, 文本宽-可用宽]：滚到末尾即停，
        // 全程没有循环 / 复位，因此不存在末端跳变。
        const QRectF r(-m_offset, 0, qreal(m_textWidth), qreal(clip.height()));
        p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_fullText);
    }

private:
    qreal pxPerSecond() const
    {
        // 基准速度：每 kPbMarqueeIntervalMs 前进 1px，再乘以倍率。
        return 1000.0 / double(kPbMarqueeIntervalMs) * m_speedMul;
    }

    int initialDelayMs() const
    {
        return int(kPbMarqueePauseTicks * (kPbMarqueeIntervalMs / qMax<qreal>(0.5, m_speedMul)));
    }

    void beginScroll()
    {
        if (!m_scrolling)
            return;
        m_clock.start();
        m_timer.start(kPbMarqueeFrameMs);
    }

    void restartScroll()
    {
        m_timer.stop();
        m_startDelay.stop();
        m_offset = 0.0;
        update();
        if (m_scrolling)
            m_startDelay.start(initialDelayMs());
    }

    void tick()
    {
        if (!m_scrolling) {
            m_timer.stop();
            return;
        }
        const qreal seconds = m_clock.elapsed() / 1000.0;
        const qreal end = maxScroll();
        m_offset = qMin(seconds * pxPerSecond(), end);
        update();
        if (m_offset >= end)
            m_timer.stop(); // 到达末尾停止，不循环
    }

    qreal maxScroll() const { return qMax<qreal>(0.0, qreal(m_textWidth) - m_maxWidth); }

    void rebuildMetrics()
    {
        const QFontMetrics fm(font());
        m_textWidth = fm.horizontalAdvance(m_fullText);
        m_scrolling = m_textWidth > m_maxWidth;

        const int lineH = m_lineHeight > 0 ? m_lineHeight : kPbTitleLineH;
        setFixedHeight(lineH);
        setFixedWidth(m_scrolling ? m_maxWidth : qMin(m_textWidth, m_maxWidth));
        setToolTip(m_scrolling ? m_fullText : QString());

        if (!m_scrolling) {
            m_timer.stop();
            m_startDelay.stop();
            m_offset = 0.0;
        } else if (!m_timer.isActive() && !m_startDelay.isActive() && m_offset < maxScroll()) {
            beginScroll(); // 由不可滚动变为可滚动：直接开始
        }
        update();
    }

    QString m_fullText;
    int m_maxWidth = 120;
    int m_textWidth = 0;
    qreal m_offset = 0.0;
    int m_lineHeight = 0;
    qreal m_speedMul = 1.0;
    bool m_scrolling = false;
    QElapsedTimer m_clock;
    QTimer m_timer;
    QTimer m_startDelay;
};

inline PbMarqueeLabel *pbSongMarquee(QLabel *label)
{
    return static_cast<PbMarqueeLabel *>(label);
}

/** 底栏艺人 / 歌词行：竖向滚入切换（新行从下方滚入、旧行向上滚出），不再有复位一跳 */
class PbLyricArtistSlot final : public QWidget {
public:
    explicit PbLyricArtistSlot(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("pbLyricSlot"));
        setFixedHeight(kPbArtistLineH);
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);

        m_cur = makeLine();
        m_next = makeLine();
        m_next->move(0, kPbArtistLineH); // 备用行停在当前行下方
        m_next->hide();

        m_slide = new QVariantAnimation(this);
        m_slide->setDuration(250);
        m_slide->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_slide, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            applySlide(v.toReal());
        });
        connect(m_slide, &QVariantAnimation::finished, this, [this]() { commitSlide(); });
    }

    void setLineMaxWidth(int width)
    {
        width = qMax(24, width);
        if (m_lineMaxW == width)
            return;
        m_lineMaxW = width;
        setFixedWidth(width);
        m_cur->setMaxDisplayWidth(width);
        m_next->setMaxDisplayWidth(width);
    }

    void setArtistText(const QString &text)
    {
        m_artistText = text;
        if (!m_showLyric)
            showText(text);
    }

    QString artistText() const { return m_artistText; }

    void updateSecondLine(bool showLyric, const QString &lyricText, int lineIndex)
    {
        m_showLyric = showLyric;
        if (!showLyric) {
            m_lastLineIndex = -1;
            showText(m_artistText);
            return;
        }
        if (lyricText.isEmpty())
            return;
        if (lineIndex == m_lastLineIndex && m_currentText == lyricText)
            return;
        m_lastLineIndex = lineIndex;
        showText(lyricText);
    }

private:
    PbMarqueeLabel *makeLine()
    {
        auto *label = new PbMarqueeLabel(this);
        label->setObjectName(QStringLiteral("pbLyric"));
        QFont f = label->font();
        f.setPixelSize(12);
        label->setFont(f);
        label->setLineHeight(kPbArtistLineH);
        label->setMarqueeSpeed(kPbLyricMarqueeSpeed);
        return label;
    }

    /** 竖向滚入：新行自下方一整行处滚入、旧行等距向上滚出；动画结束只交换两个
     *  标签的角色，可见内容的位置全程不变，因此没有「滚到位后复位」的一跳。 */
    void showText(const QString &text)
    {
        if (text.isEmpty() || text == m_currentText)
            return;
        m_currentText = text;

        if (!isVisible()) {
            m_cur->setText(text);
            m_cur->move(0, 0);
            return;
        }

        if (m_slide->state() == QAbstractAnimation::Running) {
            m_slide->stop();
            commitSlide(); // 先把上一段切换收尾，再开始新的
        }

        m_next->setText(text);
        m_next->move(0, kPbArtistLineH);
        m_next->show();
        m_next->raise();
        m_slide->setStartValue(0.0);
        m_slide->setEndValue(1.0);
        m_slide->start();
    }

    void applySlide(qreal progress)
    {
        const int h = kPbArtistLineH;
        m_next->move(0, int((1.0 - progress) * h));
        m_cur->move(0, int(-progress * h));
    }

    void commitSlide()
    {
        m_next->move(0, 0);
        m_cur->move(0, kPbArtistLineH);
        m_cur->hide();
        qSwap(m_cur, m_next);
        m_cur->show();
    }

    PbMarqueeLabel *m_cur = nullptr;
    PbMarqueeLabel *m_next = nullptr;
    QVariantAnimation *m_slide = nullptr;
    QString m_artistText;
    QString m_currentText;
    int m_lineMaxW = 0;
    int m_lastLineIndex = -1;
    bool m_showLyric = false;
};

int widgetEffectiveWidth(QWidget *w)
{
    if (!w)
        return 0;
    if (w->width() > 0)
        return w->width();
    return w->sizeHint().width();
}

/** 第一行布局后的实际内容宽（含歌名、按钮与间距） */
int titleRowContentWidth(QWidget *titleRow)
{
    if (!titleRow)
        return 0;
    auto *hl = qobject_cast<QHBoxLayout *>(titleRow->layout());
    if (!hl)
        return 0;

    int w = hl->contentsMargins().left() + hl->contentsMargins().right();
    bool first = true;
    for (int i = 0; i < hl->count(); ++i) {
        QWidget *item = hl->itemAt(i)->widget();
        if (!item || !item->isVisibleTo(titleRow))
            continue;
        if (!first)
            w += hl->spacing();
        w += widgetEffectiveWidth(item);
        first = false;
    }
    return w;
}

int titleRowAsideForSongSlot(QWidget *titleRow, QWidget *songWidget)
{
    if (!titleRow)
        return 0;
    auto *hl = qobject_cast<QHBoxLayout *>(titleRow->layout());
    if (!hl)
        return 0;

    int w = hl->contentsMargins().left() + hl->contentsMargins().right();
    bool first = true;
    for (int i = 0; i < hl->count(); ++i) {
        QWidget *item = hl->itemAt(i)->widget();
        if (!item || !item->isVisibleTo(titleRow))
            continue;
        if (item == songWidget) {
            if (!first)
                w += hl->spacing();
            first = false;
            continue;
        }
        if (!first)
            w += hl->spacing();
        w += widgetEffectiveWidth(item);
        first = false;
    }
    return w;
}

int titleRowWidthBesidesSong(QWidget *titleRow, QWidget *songWidget)
{
    if (!titleRow)
        return 0;
    if (!songWidget || !songWidget->isVisibleTo(titleRow))
        return titleRowContentWidth(titleRow);
    return qMax(0, titleRowContentWidth(titleRow) - widgetEffectiveWidth(songWidget));
}

QPixmap makeUnknownCover(int size, bool dark)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(0, 0, size, size, kPbCoverRadius, kPbCoverRadius);
    p.fillPath(path, dark ? QColor(26, 26, 46) : QColor(255, 240, 244));
    p.setPen(dark ? QColor(22, 119, 184, 200) : QColor(64, 197, 241, 180));
    QFont f = p.font();
    f.setPixelSize(13);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, I18n::instance().tr(QStringLiteral("unknown")));
    p.end();
    return pm;
}


// 与 old PlayerBar.vue 的 .control-btn / .play-btn 观感对齐：更大、更亮
const QColor kPbIconAccent = QColor(138, 223, 245, 255);
const QColor kPbPlayGlyph = QColor(18, 10, 14, 255);
const QColor kPbHeartOn = QColor(255, 69, 69, 255);
/** 对齐 SPlayer MainPlayer：控制键 38px、播放键 44px */
constexpr int kPbCtrlBtn = 38;
constexpr int kPbPlayBtn = 44;
constexpr int kPbCtrlIcon = 20;
constexpr int kPbPlayIcon = 26;
/** SPlayer .player-slider：height 16px，top -8px */
constexpr int kPbSliderHostH = 16;
constexpr int kVolumePanelExtraLiftPx = 16;
constexpr int kVolumeLeaveAutoHideMs = 3000;
const QString kSettingsKeyVolume = QStringLiteral("player/volume");

inline QColor pbCtrlIdleColor()
{
    return Theme::ThemeManager::instance().isDarkMode() ? QColor(255, 255, 255, 210)
                                                        : QColor(33, 37, 41, 210);
}

QString formatTime(qint64 ms) {
    qint64 sec = ms / 1000;
    return QString("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QChar('0'));
}

/** 列表/单曲循环 PNG 多为黑/灰 + Alpha；用目标色填充再按源 Alpha 裁切，避免整颗纯黑。 */
static QPixmap tintMaskedPixmap(const QPixmap &src, const QColor &c)
{
    if (src.isNull())
        return src;
    QPixmap out(src.size());
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.fillRect(out.rect(), c);
    p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    p.drawPixmap(0, 0, src);
    p.end();
    return out;
}

/** 播放栏图标绘制角色：完全不走 QIcon::pixmap，避免 Fusion+QSS 把图标当蒙版染黑。 */
enum class PbInk : int {
    None = 0,
    Prev,
    Next,
    PlayMain,
    Heart,
    PlaylistAdd,
    Share,
    Video,
    Download,
    DesktopLyric,
    Playlist,
    Volume,
    PlayModePng,
};

class PlayerBarInkButton final : public QPushButton {
public:
    explicit PlayerBarInkButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setFlat(true);
        setAutoDefault(false);
        setDefault(false);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
    }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override
    {
        QPushButton::enterEvent(event);
        update();
    }
    void leaveEvent(QEvent *event) override
    {
        QPushButton::leaveEvent(event);
        update();
    }
};

void PlayerBarInkButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStyleOptionButton option;
    initStyleOption(&option);
    option.icon = QIcon();
    option.text.clear();

    QStylePainter painter(this);
    painter.drawControl(QStyle::CE_PushButton, option);

    if (property("pbLoading").toBool()) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        const int ang = property("pbSpinAngle").toInt();
        const QRect r = rect();
        const QPoint c = r.center();
        const int R = qMin(r.width(), r.height()) / 2 - 4;
        painter.translate(c);

        const bool dark = Theme::ThemeManager::instance().isDarkMode();
        const QColor track = dark ? QColor(22, 119, 184, 72) : QColor(214, 90, 108, 90);
        QPen penTrack(track, 2.0, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(penTrack);
        painter.drawArc(-R, -R, 2 * R, 2 * R, 0, 360 * 16);

        painter.rotate(ang);
        const QColor arc = dark ? QColor(255, 230, 235, 240) : QColor(200, 60, 90, 245);
        QPen penArc(arc, 2.8, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(penArc);
        painter.drawArc(-R, -R, 2 * R, 2 * R, -90 * 16, 285 * 16);
        painter.restore();
        return;
    }

    const PbInk ink = static_cast<PbInk>(property("pbInk").toInt());
    if (ink == PbInk::None)
        return;

    const QSize sz = iconSize().isValid() ? iconSize() : QSize(22, 22);
    const int px = qMax(sz.width(), sz.height());
    const bool hi = isEnabled() && (underMouse() || isDown());
    const QColor cN = pbCtrlIdleColor();
    const QColor cA = kPbIconAccent;

    QPixmap pm;
    switch (ink) {
    case PbInk::Prev:
        pm = Icons::renderNamed("SkipPrev", px, hi ? cA : cN);
        break;
    case PbInk::Next:
        pm = Icons::renderNamed("SkipNext", px, hi ? cA : cN);
        break;
    case PbInk::PlayMain: {
        const bool playing = property("pbPlaying").toBool();
        pm = Icons::renderNamed(playing ? "Pause" : "Play", px, kPbPlayGlyph);
        break;
    }
    case PbInk::Heart: {
        const bool on = property("pbHeartOn").toBool();
        pm = Icons::renderNamed(on ? "Favorite" : "FavoriteBorder", px,
                               on ? kPbHeartOn : (hi ? cA : cN));
        break;
    }
    case PbInk::PlaylistAdd:
        pm = Icons::renderNamed("AddList", px, hi ? cA : cN);
        break;
    case PbInk::Share:
        pm = Icons::renderNamed("Share", px, hi ? cA : cN);
        break;
    case PbInk::Video:
        pm = Icons::renderNamed("Video", px, hi ? cA : cN);
        break;
    case PbInk::Download:
        pm = Icons::renderNamed("Download", px, hi ? cA : cN);
        break;
    case PbInk::DesktopLyric: {
        const bool on = isCheckable() && isChecked();
        pm = Icons::renderNamed("DesktopLyric2", px, on ? cA : (hi ? cA : cN));
        break;
    }
    case PbInk::Playlist:
        pm = Icons::renderNamed("PlayList", px, hi ? cA : cN);
        break;
    case PbInk::Volume: {
        const int band = property("pbVol").toInt();
        const char *name = "VolumeUp";
        if (band == 0)
            name = "VolumeOff";
        else if (band == 1)
            name = "VolumeMute";
        else if (band == 2)
            name = "VolumeDown";
        pm = Icons::renderNamed(name, px, hi ? cA : cN);
        break;
    }
    case PbInk::PlayModePng: {
        const int m = property("pbPlayMode").toInt();
        const char *name = "Repeat";
        if (m == 1)
            name = "RepeatSong";
        else if (m == 2)
            name = "Shuffle";
        pm = Icons::renderNamed(name, px, hi ? cA : cN);
        break;
    }
    default:
        return;
    }

    if (pm.isNull())
        return;

    const QRect r = rect();
    const qreal dpr = pm.devicePixelRatioF();
    const QSize pmLogical(qRound(pm.width() / dpr), qRound(pm.height() / dpr));
    const QPoint topLeft(r.x() + (r.width() - pmLogical.width()) / 2,
                         r.y() + (r.height() - pmLogical.height()) / 2);
    painter.drawPixmap(topLeft, pm);

    if (ink == PbInk::Heart) {
        const int badge = property("pbBadge").toInt();
        if (badge > 0) {
            const QString text = badge > 99 ? QStringLiteral("99+") : QString::number(badge);
            QFont f = painter.font();
            f.setPixelSize(9);
            f.setBold(true);
            painter.setFont(f);
            const QFontMetrics fm(f);
            const int bw = qMax(12, fm.horizontalAdvance(text) + 2);
            const QRect br(r.right() - bw + 1, r.top(), bw, 12);
            painter.setPen(kPbIconAccent);
            painter.drawText(br, Qt::AlignCenter, text);
        }
    }
}

}

PlayerBar::PlayerBar(PlayerEngine *engine, QWidget *parent)
    : QWidget(parent), m_engine(engine)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setupUi();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged,
            this, [this](Theme::ThemeMode) {
                applyPlayerBarGlassStyle();
                for (auto *b : findChildren<QPushButton *>())
                    b->update();
                if (m_localBadge && m_localBadge->isVisible())
                    applyLocalBadgeChrome();
                update();
            });
}

void PlayerBar::relayoutChrome()
{
    layoutPlayerBarChrome();
}

void PlayerBar::setChromeVisible(bool visible)
{
    if (m_chromeVisible == visible)
        return;
    m_chromeVisible = visible;
    if (!m_progress)
        return;
    if (!visible) {
        m_progress->hide();
        return;
    }
    if (isVisible())
        layoutPlayerBarChrome();
}

void PlayerBar::setFloatingProgressSuppressed(bool suppressed)
{
    if (m_floatingProgressSuppressed == suppressed)
        return;
    m_floatingProgressSuppressed = suppressed;
    layoutPlayerBarChrome();
}

void PlayerBar::layoutCenterControls()
{
    if (!m_pbCenter)
        return;

    QWidget *row = m_pbCenter->parentWidget();
    if (!row)
        return;

    const int rowW = row->width();
    const int rowH = row->height();
    if (rowW < 1 || rowH < 1)
        return;

    const QSize hint = m_pbCenter->sizeHint();
    const int cw = hint.width();
    const int ch = qMax(m_pbCenter->height(), hint.height());
    if (m_pbCenterSpacer)
        m_pbCenterSpacer->setFixedWidth(cw);
    // 宽窗口居中；窄窗口先给时间、音质和工具保留完整宽度，避免绝对居中盖住文字。
    const int rightMinimum = m_pbRight ? m_pbRight->minimumSizeHint().width() : 0;
    const int leftWidth = qMax(kPbCoverSize + 80, qMin((rowW - cw) / 2, rowW - cw - rightMinimum));
    if (m_pbLeft)
        m_pbLeft->setFixedWidth(leftWidth);
    if (m_pbRight)
        m_pbRight->setFixedWidth(qMax(0, rowW - cw - leftWidth));
    m_pbCenter->setGeometry(leftWidth, (rowH - ch) / 2, cw, ch);
    m_pbCenter->raise();
}

int PlayerBar::playerBarSideBudget() const
{
    int marginH = 0;
    if (m_glass) {
        if (QWidget *body = m_glass->contentWidget()) {
            if (QLayout *bodyLay = body->layout())
                marginH = bodyLay->contentsMargins().left() + bodyLay->contentsMargins().right();
        }
    }

    const int barInner = qMax(0, width() - marginH);
    const int centerW = m_pbCenter
        ? qMax(m_pbCenter->minimumSizeHint().width(), m_pbCenter->sizeHint().width())
        : 0;
    const int rightMinimum = m_pbRight ? m_pbRight->minimumSizeHint().width() : 0;
    const int side = qMin((barInner - centerW) / 2, barInner - centerW - rightMinimum);
    return qBound(kPbCoverSize + 40, side, 640);
}

void PlayerBar::layoutPlayerBarChrome()
{
    const int w = width();
    if (w < 1)
        return;

    if (m_glass)
        m_glass->setGeometry(0, 0, w, Theme::kPlayerBarBodyH);

    layoutCenterControls();

    if (!m_progress)
        return;

    if (!m_chromeVisible || m_floatingProgressSuppressed) {
        m_progress->hide();
        return;
    }

    QWidget *host = window();
    if (!host)
        host = this;

    if (m_progress->parentWidget() != host)
        m_progress->setParent(host);

    const QPoint topLeft = mapTo(host, QPoint(0, 0));
    m_progress->setGeometry(topLeft.x() + 24,
                            topLeft.y() - 2,
                            qMax(1, w - 48),
                            kPbSliderHostH);
    m_progress->raise();
    m_progress->show();
}

void PlayerBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    layoutPlayerBarChrome();
    scheduleTitleMarqueeWidthUpdate();
}

void PlayerBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutPlayerBarChrome();
    scheduleTitleMarqueeWidthUpdate();
}

void PlayerBar::scheduleTitleMarqueeWidthUpdate()
{
    if (m_titleMarqueeUpdateScheduled)
        return;
    m_titleMarqueeUpdateScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_titleMarqueeUpdateScheduled = false;
        updateTitleMarqueeWidth();
    });
}

void PlayerBar::setupUi()
{
    setFixedHeight(Theme::kPlayerBarBodyH);

    m_progress = new PlayerProgressSlider(this);
    m_progress->setObjectName(QStringLiteral("pbProgress"));
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);

    m_glass = new GlassWidget(this);
    m_glass->setBorderRadius(22);

    QWidget *const body = m_glass->contentWidget();

    auto *bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(20, 8, 20, 0);
    bodyLay->setSpacing(0);

    // ─── 主行：左(封面+信息) | 中(控制) | 右(时间+工具) ─────
    auto *mainRow = new QWidget(body);
    mainRow->setObjectName(QStringLiteral("pbMainRow"));
    auto *lay = new QHBoxLayout(mainRow);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ─── 左侧：封面 + 曲名/歌手（max 640px，对齐 play-data）────
    auto *left = new QWidget(mainRow);
    m_pbLeft = left;
    left->setObjectName(QStringLiteral("pbLeft"));
    left->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *ll = new QHBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(12);
    ll->setAlignment(Qt::AlignVCenter);

    auto *coverBtn = new QPushButton(left);
    coverBtn->setObjectName("pbCover");
    coverBtn->setFixedSize(kPbCoverSize, kPbCoverSize);
    coverBtn->setCursor(Qt::PointingHandCursor);
    coverBtn->setFlat(true);
    QPixmap ph(kPbCoverSize, kPbCoverSize);
    ph.fill(Qt::transparent);
    QPainter pp(&ph);
    QPainterPath ppp;
    ppp.addRoundedRect(0, 0, kPbCoverSize, kPbCoverSize, kPbCoverRadius, kPbCoverRadius);
    QLinearGradient g(0, 0, kPbCoverSize, kPbCoverSize);
    g.setColorAt(0.0, QColor(22, 119, 184));
    g.setColorAt(1.0, QColor(64, 197, 241));
    pp.fillPath(ppp, g);
    pp.end();
    coverBtn->setIcon(QIcon(ph));
    coverBtn->setIconSize(QSize(kPbCoverSize, kPbCoverSize));
    m_cover = coverBtn;
    connect(coverBtn, &QPushButton::clicked, this, [this]() {
        emit coverClicked();
    });
    ll->addWidget(coverBtn, 0, Qt::AlignVCenter);

    auto *infoBlock = new QWidget(left);
    infoBlock->setObjectName(QStringLiteral("pbInfoBlock"));
    infoBlock->setFixedHeight(kPbCoverSize);
    infoBlock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *infoL = new QVBoxLayout(infoBlock);
    infoL->setContentsMargins(0, 0, 0, 0);
    infoL->setSpacing(kPbInfoLineGap);
    infoL->setAlignment(Qt::AlignVCenter);

    auto *titleRow = new QWidget(infoBlock);
    m_titleRow = titleRow;
    titleRow->setObjectName(QStringLiteral("pbTitleRow"));
    titleRow->setAttribute(Qt::WA_TranslucentBackground);
    titleRow->setFixedHeight(kPbTitleLineH);
    titleRow->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *titleRowLay = new QHBoxLayout(titleRow);
    titleRowLay->setContentsMargins(0, 0, 0, 0);
    titleRowLay->setSpacing(8);

    m_localBadge = new QLabel(titleRow);
    m_localBadge->setObjectName(QStringLiteral("pbLocalBadge"));
    m_localBadge->setVisible(false);
    applyLocalBadgeChrome();
    titleRowLay->addWidget(m_localBadge, 0, Qt::AlignVCenter);

    m_songName = new PbMarqueeLabel(titleRow);
    m_songName->setObjectName("pbSong");
    QFont songFont = m_songName->font();
    songFont.setPixelSize(16);
    songFont.setWeight(QFont::Bold);
    m_songName->setFont(songFont);
    pbSongMarquee(m_songName)->setText(I18n::instance().tr("notPlaying"));
    titleRowLay->addWidget(m_songName, 0, Qt::AlignVCenter);

    m_heartBtn = new PlayerBarInkButton(titleRow);
    m_heartBtn->setObjectName("pbHeartBtn");
    m_heartBtn->setFixedSize(kPbHeartBtn, kPbHeartBtn);
    m_heartBtn->setIconSize(QSize(kPbHeartIcon, kPbHeartIcon));
    m_heartBtn->setProperty("pbInk", int(PbInk::Heart));
    m_heartBtn->setProperty("pbHeartOn", false);
    m_heartBtn->setCursor(Qt::PointingHandCursor);
    m_heartBtn->setToolTip(I18n::instance().tr("addToFavorites"));
    connect(m_heartBtn, &QPushButton::clicked, this, [this]() {
        emit favoriteClicked(m_currentMusicId);
    });
    titleRowLay->addWidget(m_heartBtn, 0, Qt::AlignVCenter);

    m_addToPlaylistBtn = new PlayerBarInkButton(titleRow);
    m_addToPlaylistBtn->setObjectName("pbAddToPlaylistBtn");
    m_addToPlaylistBtn->setFixedSize(kPbHeartBtn, kPbHeartBtn);
    m_addToPlaylistBtn->setIconSize(QSize(kPbHeartIcon, kPbHeartIcon));
    m_addToPlaylistBtn->setProperty("pbInk", int(PbInk::PlaylistAdd));
    m_addToPlaylistBtn->setCursor(Qt::PointingHandCursor);
    m_addToPlaylistBtn->setToolTip(I18n::instance().tr("addToPlaylist"));
    m_addToPlaylistBtn->setEnabled(false);
    connect(m_addToPlaylistBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentMusicId > 0)
            emit addToPlaylistClicked(m_currentMusicId);
    });
    titleRowLay->addWidget(m_addToPlaylistBtn, 0, Qt::AlignVCenter);

    infoL->addWidget(titleRow, 0, Qt::AlignLeft);

    m_lyricSlot = new PbLyricArtistSlot(infoBlock);
    static_cast<PbLyricArtistSlot *>(m_lyricSlot)->setArtistText(I18n::instance().tr("unknown"));
    m_songName->setWordWrap(false);
    infoL->addWidget(m_lyricSlot, 0, Qt::AlignLeft);
    ll->addWidget(infoBlock, 1, Qt::AlignVCenter);
    lay->addWidget(left, 1);

    // ─── 中间：播放控制（SPlayer play-control）────────────────
    auto *center = new QWidget(mainRow);
    m_pbCenter = center;
    center->setObjectName(QStringLiteral("pbCenter"));
    center->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    auto *ctrlL = new QHBoxLayout(center);
    ctrlL->setContentsMargins(16, 0, 16, 0);
    ctrlL->setSpacing(8);
    ctrlL->setAlignment(Qt::AlignCenter);

    m_playModeBtn = new PlayerBarInkButton(center);
    m_playModeBtn->setObjectName("pbPlayModeBtn");
    m_playModeBtn->setFixedSize(kPbCtrlBtn, kPbCtrlBtn);
    m_playModeBtn->setIconSize(QSize(20, 20));
    m_playModeBtn->setProperty("pbInk", int(PbInk::PlayModePng));
    m_playModeBtn->setProperty("pbPlayMode", 0);
    m_playModeBtn->setCursor(Qt::PointingHandCursor);
    m_playModeBtn->setToolTip(I18n::instance().tr("playModeList"));
    connect(m_playModeBtn, &QPushButton::clicked, this, [this]() {
        emit playModeClicked();
    });
    ctrlL->addWidget(m_playModeBtn);

    auto *prevBtn = new PlayerBarInkButton(center);
    prevBtn->setObjectName("pbCtrlBtn");
    prevBtn->setFixedSize(kPbCtrlBtn, kPbCtrlBtn);
    prevBtn->setIconSize(QSize(kPbCtrlIcon, kPbCtrlIcon));
    prevBtn->setProperty("pbInk", int(PbInk::Prev));
    prevBtn->setCursor(Qt::PointingHandCursor);
    prevBtn->setToolTip(I18n::instance().tr("previous"));
    connect(prevBtn, &QPushButton::clicked, this, [this]() {
        emit previousClicked();
    });
    ctrlL->addWidget(prevBtn);

    m_playBtn = new PlayerBarInkButton(center);
    m_playBtn->setObjectName("pbPlayBtn");
    m_playBtn->setFixedSize(kPbPlayBtn, kPbPlayBtn);
    m_playBtn->setIconSize(QSize(kPbPlayIcon, kPbPlayIcon));
    m_playBtn->setProperty("pbInk", int(PbInk::PlayMain));
    m_playBtn->setProperty("pbPlaying", false);
    m_playBtn->setProperty("pbLoading", false);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setToolTip(I18n::instance().tr("play"));
    ctrlL->addWidget(m_playBtn);

    auto *nextBtn = new PlayerBarInkButton(center);
    nextBtn->setObjectName("pbCtrlBtn");
    nextBtn->setFixedSize(kPbCtrlBtn, kPbCtrlBtn);
    nextBtn->setIconSize(QSize(kPbCtrlIcon, kPbCtrlIcon));
    nextBtn->setProperty("pbInk", int(PbInk::Next));
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setToolTip(I18n::instance().tr("next"));
    connect(nextBtn, &QPushButton::clicked, this, [this]() {
        emit nextClicked();
    });
    ctrlL->addWidget(nextBtn);

    m_downloadBtn = new PlayerBarInkButton(center);
    m_downloadBtn->setObjectName("pbCtrlBtn");
    m_downloadBtn->setFixedSize(kPbCtrlBtn, kPbCtrlBtn);
    m_downloadBtn->setIconSize(QSize(kPbCtrlIcon, kPbCtrlIcon));
    m_downloadBtn->setProperty("pbInk", int(PbInk::Download));
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    m_downloadBtn->setEnabled(false);
    m_downloadBtn->setToolTip(I18n::instance().tr("downloadMusic"));
    connect(m_downloadBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentMusicId > 0)
            emit downloadClicked(m_currentMusicId);
    });
    ctrlL->addWidget(m_downloadBtn);

    // 布局占位保留原三列 flex；实际控件浮层绝对居中
    m_pbCenterSpacer = new QWidget(mainRow);
    m_pbCenterSpacer->setObjectName(QStringLiteral("pbCenterSpacer"));
    m_pbCenterSpacer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_pbCenterSpacer->setFixedWidth(center->sizeHint().width());
    lay->addWidget(m_pbCenterSpacer, 0, Qt::AlignVCenter);
    center->raise();

    // ─── 右侧：时间 + 工具（SPlayer play-menu）────────────────
    auto *right = new QWidget(mainRow);
    m_pbRight = right;
    right->setObjectName(QStringLiteral("pbRight"));
    right->setMaximumWidth(640);
    right->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *rl = new QHBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rl->setSpacing(4);

    rl->addStretch();

    auto *timeBlock = new QWidget(right);
    timeBlock->setObjectName(QStringLiteral("pbTimeBlock"));
    auto *timeLay = new QHBoxLayout(timeBlock);
    timeLay->setContentsMargins(0, 0, 8, 0);
    timeLay->setSpacing(4);

    m_curTime = new QLabel(QStringLiteral("0:00"), timeBlock);
    m_curTime->setObjectName("pbTime");
    m_curTime->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    timeLay->addWidget(m_curTime);

    auto *timeSep = new QLabel(QStringLiteral("/"), timeBlock);
    timeSep->setObjectName(QStringLiteral("pbTimeSep"));
    timeLay->addWidget(timeSep);

    m_durTime = new QLabel(QStringLiteral("0:00"), timeBlock);
    m_durTime->setObjectName("pbTime");
    m_durTime->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    timeLay->addWidget(m_durTime);
    rl->addWidget(timeBlock, 0, Qt::AlignVCenter);

    m_qualitySelector = new QComboBox(right);
    m_qualitySelector->setObjectName(QStringLiteral("pbQualitySelector"));
    m_qualitySelector->addItem(I18n::instance().tr(QStringLiteral("standard")), QStringLiteral("standard"));
    m_qualitySelector->addItem(QStringLiteral("HQ"), QStringLiteral("hq"));
    m_qualitySelector->addItem(QStringLiteral("SQ"), QStringLiteral("sq"));
    m_qualitySelector->addItem(QStringLiteral("Hi-Res"), QStringLiteral("hires"));
    m_qualitySelector->setMinimumWidth(78);
    m_qualitySelector->setFixedHeight(30);
    m_qualitySelector->setToolTip(QStringLiteral("选择播放音质"));
    const QSettings qualitySettings;
    const QString savedQuality = qualitySettings.value(
        QStringLiteral("player/audioQuality"), QStringLiteral("hq")).toString().trimmed().toLower();
    m_preferredQuality = savedQuality.isEmpty() ? QStringLiteral("hq") : savedQuality;
    const int savedIndex = m_qualitySelector->findData(m_preferredQuality);
    if (savedIndex >= 0)
        m_qualitySelector->setCurrentIndex(savedIndex);
    connect(m_qualitySelector, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
        if (index < 0)
            return;
        const QString quality = m_qualitySelector->itemData(index).toString();
        m_preferredQuality = quality;
        QSettings settings;
        settings.setValue(QStringLiteral("player/audioQuality"), quality);
        emit audioQualityChanged(quality);
    });
    rl->addWidget(m_qualitySelector, 0, Qt::AlignVCenter);

    m_shareBtn = new PlayerBarInkButton(right);
    m_shareBtn->setObjectName(QStringLiteral("pbShareBtn"));
    m_shareBtn->setFixedSize(kPbCtrlBtn, kPbCtrlBtn);
    m_shareBtn->setIconSize(QSize(kPbCtrlIcon, kPbCtrlIcon));
    m_shareBtn->setProperty("pbInk", int(PbInk::Share));
    m_shareBtn->setCursor(Qt::PointingHandCursor);
    m_shareBtn->setToolTip(I18n::instance().tr(QStringLiteral("shareTrack")));
    connect(m_shareBtn, &QPushButton::clicked, this, [this]() { emit shareClicked(); });
    rl->addWidget(m_shareBtn);

    m_videoShareBtn = new PlayerBarInkButton(right);
    m_videoShareBtn->setObjectName(QStringLiteral("pbVideoShareBtn"));
    m_videoShareBtn->setFixedSize(kPbCtrlBtn, kPbCtrlBtn);
    m_videoShareBtn->setIconSize(QSize(kPbCtrlIcon, kPbCtrlIcon));
    m_videoShareBtn->setProperty("pbInk", int(PbInk::Video));
    m_videoShareBtn->setCursor(Qt::PointingHandCursor);
    m_videoShareBtn->hide();
    connect(m_videoShareBtn, &QPushButton::clicked, this, [this]() { emit videoShareClicked(); });
    rl->addWidget(m_videoShareBtn);

    m_desktopLrcBtn = new PlayerBarInkButton(right);
    m_desktopLrcBtn->setObjectName("pbDesktopLrcBtn");
    m_desktopLrcBtn->setFixedSize(kPbCtrlBtn, kPbCtrlBtn);
    m_desktopLrcBtn->setIconSize(QSize(kPbCtrlIcon, kPbCtrlIcon));
    m_desktopLrcBtn->setProperty("pbInk", int(PbInk::DesktopLyric));
    m_desktopLrcBtn->setCheckable(true);
    m_desktopLrcBtn->setCursor(Qt::PointingHandCursor);
    m_desktopLrcBtn->setToolTip(I18n::instance().tr("desktopLyrics"));
    {
        QSettings lrcSettings;
        m_desktopLrcBtn->setChecked(lrcSettings.value(QStringLiteral("desktopLyrics"), false).toBool());
    }
    connect(m_desktopLrcBtn, &QPushButton::toggled, this, [this](bool on) {
        QSettings s;
        s.setValue(QStringLiteral("desktopLyrics"), on);
        emit desktopLyricsToggled(on);
        m_desktopLrcBtn->update();
    });
    rl->addWidget(m_desktopLrcBtn);

    // 音量控制
    auto *volWrapper = new QWidget(right);
    volWrapper->setObjectName("pbVolumeWrapper");
    auto *volLay = new QHBoxLayout(volWrapper);
    volLay->setContentsMargins(0, 0, 0, 0);
    volLay->setSpacing(0);

    m_volumeBtn = new PlayerBarInkButton(right);
    m_volumeBtn->setObjectName("pbVolumeBtn");
    m_volumeBtn->setFixedSize(kPbCtrlBtn, kPbCtrlBtn);
    m_volumeBtn->setIconSize(QSize(kPbCtrlIcon, kPbCtrlIcon));
    m_volumeBtn->setProperty("pbInk", int(PbInk::Volume));
    m_volumeBtn->setProperty("pbVol", 3);
    m_volumeBtn->setCursor(Qt::PointingHandCursor);
    volLay->addWidget(m_volumeBtn);
    rl->addWidget(volWrapper);

    auto *playlistBtn = new PlayerBarInkButton(right);
    playlistBtn->setObjectName("pbPlaylistBtn");
    playlistBtn->setFixedSize(kPbCtrlBtn, kPbCtrlBtn);
    playlistBtn->setIconSize(QSize(kPbCtrlIcon, kPbCtrlIcon));
    playlistBtn->setProperty("pbInk", int(PbInk::Playlist));
    playlistBtn->setCursor(Qt::PointingHandCursor);
    playlistBtn->setToolTip(I18n::instance().tr("playlist"));
    connect(playlistBtn, &QPushButton::clicked, this, [this]() {
        emit playlistClicked();
    });
    rl->addWidget(playlistBtn);

    // 音量面板 (垂直弹出) — 父级用顶层窗口，避免面板在播放栏上方时被父控件裁剪
    QWidget *volHost = window();
    if (!volHost)
        volHost = this;
    m_volumePanel = new QWidget(volHost);
    m_volumePanel->setObjectName("pbVolumePanel");
    m_volumePanel->setFixedWidth(40);
    m_volumePanel->setFixedHeight(160);
    m_volumePanel->setFocusPolicy(Qt::NoFocus);
    m_volumePanel->hide();
    m_volumePanel->installEventFilter(this);
    volWrapper->installEventFilter(this);

    auto *vpLay = new QVBoxLayout(m_volumePanel);
    vpLay->setContentsMargins(8, 12, 8, 12);
    vpLay->setSpacing(8);

    m_volumeSlider = new QSlider(Qt::Vertical, m_volumePanel);
    m_volumeSlider->setObjectName("pbVolumeSlider");
    m_volumeSlider->setRange(0, 100);
    vpLay->addWidget(m_volumeSlider, 1, Qt::AlignHCenter);

    QSettings volSettings;
    const int initialVol = qBound(0, volSettings.value(kSettingsKeyVolume, 80).toInt(), 100);

    m_volumeLabel = new QLabel(QStringLiteral("%1%").arg(initialVol), m_volumePanel);
    m_volumeLabel->setObjectName("pbVolumeLabel");
    m_volumeLabel->setAlignment(Qt::AlignCenter);
    vpLay->addWidget(m_volumeLabel);

    // 参考 old 中 .volume-panel 的 opacity 0.2s ease，并加轻微上滑
    m_volumeOpacityFx = new QGraphicsOpacityEffect(m_volumePanel);
    m_volumeOpacityFx->setOpacity(0.0);
    m_volumePanel->setGraphicsEffect(m_volumeOpacityFx);
    m_volumeOpAnim = new QPropertyAnimation(m_volumeOpacityFx, "opacity", this);
    m_volumePosAnim = new QPropertyAnimation(m_volumePanel, "pos", this);
    connect(m_volumeOpAnim, &QPropertyAnimation::finished, this, [this]() {
        if (!m_volumePanelClosing || !m_volumePanel)
            return;
        m_volumePanel->hide();
        m_volumePanelClosing = false;
        removeVolumePanelAppFilter();
    });

    m_volumeLeaveTimer = new QTimer(this);
    m_volumeLeaveTimer->setSingleShot(true);
    m_volumeLeaveTimer->setInterval(kVolumeLeaveAutoHideMs);
    connect(m_volumeLeaveTimer, &QTimer::timeout, this, [this]() {
        if (!m_volumePanel || !m_volumePanel->isVisible() || m_volumePanelClosing)
            return;
        if (!volumePanelHotRectGlobal().contains(QCursor::pos()))
            hideVolumePanelAnimated();
    });

    lay->addWidget(right, 1);

    bodyLay->addWidget(mainRow, 1);

    layoutPlayerBarChrome();
    applyPlayerBarGlassStyle();
    scheduleTitleMarqueeWidthUpdate();

    // 连接引擎
    if (m_engine) {
        connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int v) {
            m_engine->setVolume(v / 100.0f);
            m_volumeLabel->setText(QString("%1%").arg(v));
            updateVolumeIcon(v);
            emit volumePercentChanged(v);
        });
        connect(m_volumeSlider, &QSlider::sliderReleased, this, [this]() {
            if (!m_volumeSlider)
                return;
            QSettings s;
            s.setValue(kSettingsKeyVolume, m_volumeSlider->value());
        });
        connect(m_playBtn, &QPushButton::clicked, this, [this]() {
            if (m_isLoading)
                return;
            if (m_engine->isActuallyPlaying()) m_engine->fadeOut();
            else m_engine->fadeIn();
        });
        connect(m_engine, &PlayerEngine::stateChanged, this, [this]() { updateState(); });
        // 进度条跟随播放位置（拖动/按下时不要 setValue，否则会抢鼠标导致无法拖动）
        connect(m_engine, &PlayerEngine::positionChanged, this, [this](qint64 pos) {
            if (m_curTime) m_curTime->setText(formatTime(pos));
            if (!m_progress || m_progress->isSliderDown())
                return;
            const qint64 dur = m_engine->duration();
            if (dur > 0)
                m_progress->setValue(static_cast<int>(pos * 1000 / dur));
        });
        connect(m_engine, &PlayerEngine::durationChanged, this, [this](qint64 dur) {
            if (m_durTime) m_durTime->setText(formatTime(dur));
        });
        // 进度条控制播放位置
        connect(m_progress, &QSlider::sliderReleased, this, [this]() {
            // 释放时应用最终位置
            if (m_engine && m_engine->duration() > 0) {
                qint64 position = static_cast<qint64>(m_progress->value()) * m_engine->duration() / 1000;
                m_engine->setPosition(position);
            }
        });
        m_volumeSlider->setValue(initialVol);
        updateVolumeIcon(initialVol);
    }
}

PlayerBar::~PlayerBar()
{
    removeVolumePanelAppFilter();
}

void PlayerBar::setVolumePercentSynced(int percent)
{
    if (!m_volumeSlider || !m_engine)
        return;
    percent = qBound(0, percent, 100);
    QSignalBlocker blocker(m_volumeSlider);
    m_volumeSlider->setValue(percent);
    m_engine->setVolume(percent / 100.0f);
    if (m_volumeLabel)
        m_volumeLabel->setText(QStringLiteral("%1%").arg(percent));
    updateVolumeIcon(percent);
}

QRect PlayerBar::volumePanelHotRectGlobal() const
{
    if (!m_volumeBtn || !m_volumePanel)
        return {};
    const QRect btnGlobal(m_volumeBtn->mapToGlobal(QPoint(0, 0)), m_volumeBtn->size());
    const QRect panelGlobal(m_volumePanel->mapToGlobal(QPoint(0, 0)), m_volumePanel->size());
    return btnGlobal.united(panelGlobal);
}

void PlayerBar::installVolumePanelAppFilter()
{
    if (m_volumeAppFilterInstalled)
        return;
    if (QCoreApplication *app = QCoreApplication::instance()) {
        app->installEventFilter(this);
        m_volumeAppFilterInstalled = true;
    }
}

void PlayerBar::removeVolumePanelAppFilter()
{
    if (!m_volumeAppFilterInstalled)
        return;
    if (QCoreApplication *app = QCoreApplication::instance())
        app->removeEventFilter(this);
    m_volumeAppFilterInstalled = false;
}

void PlayerBar::showVolumePanelAnimated()
{
    if (!m_volumePanel || !m_volumeBtn || !m_volumeOpacityFx || !m_volumeOpAnim || !m_volumePosAnim)
        return;

    m_volumePanelClosing = false;
    m_volumeOpAnim->stop();
    m_volumePosAnim->stop();
    if (m_volumeLeaveTimer)
        m_volumeLeaveTimer->stop();

    QWidget *host = m_volumePanel->parentWidget();
    if (!host)
        host = window();
    if (!host)
        host = this;
    const QPoint ref = m_volumeBtn->mapTo(host, QPoint(0, 0));
    const int x = ref.x() + (m_volumeBtn->width() - m_volumePanel->width()) / 2;
    const int overlap = 8;
    const int y = ref.y() - m_volumePanel->height() + overlap - kVolumePanelExtraLiftPx;
    const QPoint finalPos(x, y);
    const QPoint startPos(x, y + 10);

    m_volumePanel->move(startPos);
    m_volumeOpacityFx->setOpacity(0.0);
    m_volumePanel->show();
    m_volumePanel->raise();

    m_volumeOpAnim->setDuration(200);
    m_volumeOpAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_volumeOpAnim->setStartValue(0.0);
    m_volumeOpAnim->setEndValue(1.0);

    m_volumePosAnim->setDuration(200);
    m_volumePosAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_volumePosAnim->setStartValue(startPos);
    m_volumePosAnim->setEndValue(finalPos);

    m_volumeOpAnim->start();
    m_volumePosAnim->start();

    installVolumePanelAppFilter();
}

void PlayerBar::hideVolumePanelAnimated()
{
    if (!m_volumePanel || !m_volumeOpacityFx || !m_volumeOpAnim)
        return;
    if (!m_volumePanel->isVisible())
        return;

    if (m_volumeLeaveTimer)
        m_volumeLeaveTimer->stop();

    m_volumeOpAnim->stop();
    if (m_volumePosAnim)
        m_volumePosAnim->stop();

    m_volumePanelClosing = true;
    m_volumeOpAnim->setDuration(180);
    m_volumeOpAnim->setEasingCurve(QEasingCurve::InCubic);
    m_volumeOpAnim->setStartValue(m_volumeOpacityFx->opacity());
    m_volumeOpAnim->setEndValue(0.0);
    m_volumeOpAnim->start();
}

bool PlayerBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress && m_volumePanel && m_volumePanel->isVisible()
        && !m_volumePanelClosing) {
        auto *me = static_cast<QMouseEvent *>(event);
        const QPoint g = me->globalPosition().toPoint();
        if (!volumePanelHotRectGlobal().contains(g))
            hideVolumePanelAnimated();
    }

    if (watched->objectName() == "pbVolumeWrapper") {
        if (event->type() == QEvent::Enter) {
            if (m_volumeLeaveTimer)
                m_volumeLeaveTimer->stop();
            if (m_volumePanel && m_volumeBtn)
                showVolumePanelAnimated();
        } else if (event->type() == QEvent::Leave) {
            if (m_volumePanel && m_volumePanel->isVisible()) {
                if (!volumePanelHotRectGlobal().contains(QCursor::pos())) {
                    if (m_volumeLeaveTimer)
                        m_volumeLeaveTimer->start();
                } else if (m_volumeLeaveTimer) {
                    m_volumeLeaveTimer->stop();
                }
            }
        }
    } else if (watched == m_volumePanel) {
        if (event->type() == QEvent::Enter) {
            if (m_volumeLeaveTimer)
                m_volumeLeaveTimer->stop();
        } else if (event->type() == QEvent::Leave) {
            if (!volumePanelHotRectGlobal().contains(QCursor::pos())) {
                if (m_volumeLeaveTimer)
                    m_volumeLeaveTimer->start();
            } else if (m_volumeLeaveTimer) {
                m_volumeLeaveTimer->stop();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PlayerBar::updateVolumeIcon(int value)
{
    if (!m_volumeBtn) return;
    int band = 3;
    if (value == 0)
        band = 0;
    else if (value < 40)
        band = 1;
    else if (value < 70)
        band = 2;
    m_volumeBtn->setProperty("pbVol", band);
    m_volumeBtn->update();
}

void PlayerBar::updateVideoShareUi(bool available, bool busy, const QString &jobStatus)
{
    if (!m_videoShareBtn)
        return;
    m_videoShareBtn->setVisible(available);
    scheduleTitleMarqueeWidthUpdate();
    const bool processing = jobStatus == QStringLiteral("pending")
                            || jobStatus == QStringLiteral("processing");
    m_videoShareBtn->setEnabled(available && !busy && !processing);
    if (jobStatus == QStringLiteral("done"))
        m_videoShareBtn->setToolTip(I18n::instance().tr(QStringLiteral("videoRenderDownload")));
    else if (busy)
        m_videoShareBtn->setToolTip(I18n::instance().tr(QStringLiteral("videoRenderButtonBusy")));
    else if (processing)
        m_videoShareBtn->setToolTip(I18n::instance().tr(QStringLiteral("videoRenderStatusProcessing")));
    else
        m_videoShareBtn->setToolTip(I18n::instance().tr(QStringLiteral("videoRenderButton")));
}

void PlayerBar::retranslate()
{
    // Update default labels if still showing defaults
    if (m_songName) {
        const QString cur = pbSongMarquee(m_songName)->fullText();
        if (cur == QStringLiteral("未在播放") || cur == I18n::instance().tr("notPlaying"))
            pbSongMarquee(m_songName)->setText(I18n::instance().tr("notPlaying"));
    }
    if (m_lyricSlot) {
        auto *slot = static_cast<PbLyricArtistSlot *>(m_lyricSlot);
        const QString cur = slot->artistText();
        if (cur == QStringLiteral("--") || cur == I18n::instance().tr("unknown"))
            slot->setArtistText(I18n::instance().tr("unknown"));
    }

    if (m_playBtn) {
        bool playing = m_engine && m_engine->isActuallyPlaying();
        m_playBtn->setToolTip(playing ? I18n::instance().tr("pause") : I18n::instance().tr("play"));
    }

    // Update prev/next tooltips by finding buttons in order
    auto btns = findChildren<QPushButton *>();
    int ctrlCount = 0;
    for (auto *btn : btns) {
        if (btn->objectName() == "pbCtrlBtn") {
            if (ctrlCount == 0) btn->setToolTip(I18n::instance().tr("previous"));
            else if (ctrlCount == 1) btn->setToolTip(I18n::instance().tr("next"));
            else if (ctrlCount == 2) btn->setToolTip(I18n::instance().tr("downloadMusic"));
            ctrlCount++;
        }
    }

    if (m_addToPlaylistBtn)
        m_addToPlaylistBtn->setToolTip(I18n::instance().tr("addToPlaylist"));
    if (m_desktopLrcBtn)
        m_desktopLrcBtn->setToolTip(I18n::instance().tr("desktopLyrics"));
    if (m_shareBtn)
        m_shareBtn->setToolTip(I18n::instance().tr(QStringLiteral("shareTrack")));
    if (m_downloadBtn)
        m_downloadBtn->setToolTip(I18n::instance().tr("downloadMusic"));
    if (m_videoShareBtn && m_videoShareBtn->isVisible())
        updateVideoShareUi(true, false, QString());
    if (m_qualitySelector && m_qualitySelector->count() >= 1)
        m_qualitySelector->setItemText(0, I18n::instance().tr(QStringLiteral("standard")));

    refreshLocalBadge();
}

void PlayerBar::setSongInfo(const QString &title, const QString &artist, const QString &coverUrl)
{
    if (m_songName) {
        pbSongMarquee(m_songName)->setText(title.isEmpty() ? I18n::instance().tr("unknown") : title);
        scheduleTitleMarqueeWidthUpdate();
    }
    if (m_lyricSlot)
        static_cast<PbLyricArtistSlot *>(m_lyricSlot)->setArtistText(
            artist.isEmpty() ? I18n::instance().tr("unknown") : artist);
    m_barLyricText.clear();
    m_barLyricLineIndex = -1;
    m_trackHasLyrics = false;
    refreshBarLyricSlot();
    refreshLocalBadge();

    if (!m_cover)
        return;

    QString fetchUrl = CoverCache::resolveCoverUrl(coverUrl);
    if (fetchUrl.isEmpty() && m_currentMusicId > 0) {
        fetchUrl = QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(m_currentMusicId);
    }

    if (m_currentMusicId < 0) {
        disconnect(m_coverConn);
        m_coverConn = {};
        if (fetchUrl.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
            QPixmap px;
            if (px.load(QUrl(fetchUrl).toLocalFile()))
                setCoverPixmap(px);
            else
                setCoverUnknownPlaceholder();
        } else {
            setCoverUnknownPlaceholder();
        }
        return;
    }

    if (fetchUrl.isEmpty()) {
        disconnect(m_coverConn);
        m_coverConn = {};
        if (QPixmap ph = Icons::renderNamed("Music", 28, pbCtrlIdleColor()); !ph.isNull())
            setCoverPixmap(ph);
        return;
    }

    const QString cacheKey = CoverCache::musicIdFromCoverUrl(fetchUrl);
    if (cacheKey.isEmpty()) {
        disconnect(m_coverConn);
        m_coverConn = {};
        if (QPixmap ph = Icons::renderNamed("Music", 28, pbCtrlIdleColor()); !ph.isNull())
            setCoverPixmap(ph);
        return;
    }

    CoverCache *cc = CoverCache::instance();
    if (QPixmap hit = cc->get(cacheKey); !hit.isNull()) {
        disconnect(m_coverConn);
        m_coverConn = {};
        setCoverPixmap(hit);
        return;
    }

    disconnect(m_coverConn);
    const int boundId = m_currentMusicId;
    m_coverConn = connect(cc, &CoverCache::coverLoaded, this,
                          [this, cacheKey, boundId](const QString &id, const QPixmap &pix) {
                              if (id != cacheKey)
                                  return;
                              if (m_currentMusicId != boundId)
                                  return;
                              if (pix.isNull())
                                  return;
                              setCoverPixmap(pix);
                          });

    if (QPixmap ph = Icons::renderNamed("Music", 28, pbCtrlIdleColor()); !ph.isNull())
        setCoverPixmap(ph);
    cc->fetchCover(cacheKey, fetchUrl);
}

void PlayerBar::setCoverVisible(bool visible)
{
    if (m_cover) m_cover->setVisible(visible);
}

void PlayerBar::setCurrentMusicId(int musicId)
{
    qDebug() << "[播放栏] 设置当前音乐ID:" << musicId;
    const bool changed = m_currentMusicId != musicId;
    m_currentMusicId = musicId;
    if (m_addToPlaylistBtn)
        m_addToPlaylistBtn->setEnabled(musicId > 0);
    if (m_downloadBtn)
        m_downloadBtn->setEnabled(musicId > 0);
    if (changed)
        setMaxQuality(musicId > 0 ? QStringLiteral("hq") : QStringLiteral("standard"), false);
    // 不重置状态，由调用方自行检查收藏状态后设置
    refreshLocalBadge();
}

void PlayerBar::setMaxQuality(const QString &quality, bool authoritative)
{
    if (!m_qualitySelector)
        return;
    const QString normalized = quality.trimmed().toLower();
    int maxRank = 1;
    if (normalized == QStringLiteral("standard")) maxRank = 0;
    else if (normalized == QStringLiteral("sq")) maxRank = 2;
    else if (normalized == QStringLiteral("hires")) maxRank = 3;

    auto *model = qobject_cast<QStandardItemModel *>(m_qualitySelector->model());
    if (model) {
        for (int i = 0; i < m_qualitySelector->count(); ++i) {
            auto *item = model->item(i);
            if (!item)
                continue;
            Qt::ItemFlags flags = item->flags();
            if (i <= maxRank)
                flags |= Qt::ItemIsEnabled;
            else
                flags &= ~Qt::ItemIsEnabled;
            item->setFlags(flags);
        }
    }
    m_qualitySelector->setEnabled(m_currentMusicId > 0);

    if (!authoritative)
        return;

    // 拿到真实 maxQuality 后，只夹取**显示**档位到合法范围：选中项若超过 maxQuality，
    // 会显示成不可用档位，误导用户。这里用 QSignalBlocker 静默回退到合法档
    // （不触发 audioQualityChanged、不覆盖 QSettings，也不改 m_preferredQuality，
    // 避免把用户偏好真正降级；偏好下次遇到支持它的歌曲会自动恢复）。
    const QSignalBlocker blocker(m_qualitySelector);
    int desired = m_qualitySelector->findData(m_preferredQuality);
    if (desired < 0)
        desired = maxRank;
    desired = qBound(0, qMin(desired, maxRank), m_qualitySelector->count() - 1);
    if (m_qualitySelector->currentIndex() != desired)
        m_qualitySelector->setCurrentIndex(desired);
}

QString PlayerBar::selectedAudioQuality() const
{
    // 以用户偏好为准，而非可能被 maxQuality 夹取过的下拉框显示值，
    // 否则切到只支持低档的歌曲后，偏好会被永久降级。
    if (!m_preferredQuality.isEmpty())
        return m_preferredQuality;
    return m_qualitySelector ? m_qualitySelector->currentData().toString() : QStringLiteral("hq");
}

bool PlayerBar::setAudioQuality(const QString &quality)
{
    if (!m_qualitySelector)
        return false;
    const QString normalized = quality.trimmed().toLower();
    const int index = m_qualitySelector->findData(normalized);
    if (index < 0)
        return false;
    if (m_qualitySelector->currentIndex() != index)
        m_qualitySelector->setCurrentIndex(index); // 触发已有 currentIndexChanged 逻辑
    else {
        // 档位未变时下拉框不会发信号，这里手动落盘并广播，保证调用方预期成立。
        m_preferredQuality = normalized;
        QSettings settings;
        settings.setValue(QStringLiteral("player/audioQuality"), normalized);
        emit audioQualityChanged(normalized);
    }
    return true;
}

void PlayerBar::applyLocalBadgeChrome()
{
    if (!m_localBadge)
        return;
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    if (dark) {
        m_localBadge->setStyleSheet(QStringLiteral(
            "QLabel#pbLocalBadge {"
            "  font-size: 11px;"
            "  font-weight: 800;"
            "  color: #100818;"
            "  padding: 2px 10px;"
            "  border-radius: 8px;"
            "  background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #7EE8C8, stop:0.42 #C8FFD8, stop:1 #ECC8FF);"
            "  border: 1px solid rgba(255,255,255,0.78);"
            "}"));
    } else {
        m_localBadge->setStyleSheet(QStringLiteral(
            "QLabel#pbLocalBadge {"
            "  font-size: 11px;"
            "  font-weight: 800;"
            "  color: #160f22;"
            "  padding: 2px 10px;"
            "  border-radius: 8px;"
            "  background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #42C4C4, stop:0.45 #A8F0D8, stop:1 #D0B0FF);"
            "  border: 1px solid rgba(70,40,120,0.45);"
            "}"));
    }
}

void PlayerBar::refreshLocalBadge()
{
    if (!m_localBadge)
        return;
    const bool show = m_currentMusicId < 0;
    m_localBadge->setVisible(show);
    if (show) {
        m_localBadge->setText(I18n::instance().tr(QStringLiteral("localMusicBadge")));
        applyLocalBadgeChrome();
    }
    scheduleTitleMarqueeWidthUpdate();
}

void PlayerBar::updateTitleMarqueeWidth()
{
    if (!m_songName)
        return;

    auto *marquee = pbSongMarquee(m_songName);
    if (!marquee)
        return;

    // 由底栏几何推算歌名区可用宽，不读 m_pbLeft 当前宽（长歌名后该值会偏大且无法收回）
    const int sideBudget = playerBarSideBudget();

    layoutCenterControls();

    const int aside = titleRowAsideForSongSlot(m_titleRow, m_songName);
    const int infoW = qMax(0, sideBudget - kPbCoverSize - 12);
    const int titleMax = qMax(24, infoW - aside);

    marquee->setMaxDisplayWidth(titleMax);

    // 第二行保持最大可用宽；第一行按钮紧贴歌名，行宽随歌名自然收缩
    const int secondLineMax = qMax(24, qMin(infoW, aside + titleMax));

    if (m_titleRow)
        m_titleRow->setMaximumWidth(infoW);

    if (m_lyricSlot)
        static_cast<PbLyricArtistSlot *>(m_lyricSlot)->setLineMaxWidth(secondLineMax);
}

void PlayerBar::setBarLyricLine(const QString &displayText, int lineIndex, bool trackHasLyrics)
{
    m_barLyricText = displayText;
    m_barLyricLineIndex = lineIndex;
    m_trackHasLyrics = trackHasLyrics;
    refreshBarLyricSlot();
}

void PlayerBar::refreshBarLyricSlot()
{
    if (!m_lyricSlot)
        return;

    const bool playing = m_engine && m_engine->isActuallyPlaying();
    const bool showLyric = playing && m_trackHasLyrics && m_barLyricLineIndex >= 0
                           && !m_barLyricText.isEmpty();

    auto *slot = static_cast<PbLyricArtistSlot *>(m_lyricSlot);
    slot->updateSecondLine(showLyric, m_barLyricText, m_barLyricLineIndex);
    scheduleTitleMarqueeWidthUpdate();
}

void PlayerBar::setFavoriteCount(int count)
{
    if (!m_heartBtn)
        return;
    m_heartBtn->setProperty("pbBadge", count);
    m_heartBtn->update();
}

void PlayerBar::setFavoriteStatus(bool isFavorited)
{
    qDebug() << "[播放栏] setFavoriteStatus:" << isFavorited;
    m_isFavorited = isFavorited;
    if (m_heartBtn) {
        m_heartBtn->setProperty("pbHeartOn", isFavorited);
        m_heartBtn->setToolTip(isFavorited ? I18n::instance().tr("removeFromFavorites") : I18n::instance().tr("addToFavorites"));
        m_heartBtn->update();
    }
    if (m_addToPlaylistBtn)
        m_addToPlaylistBtn->setToolTip(I18n::instance().tr("addToPlaylist"));
}

void PlayerBar::setDesktopLyricsChecked(bool checked)
{
    if (!m_desktopLrcBtn)
        return;
    const QSignalBlocker blocker(m_desktopLrcBtn);
    m_desktopLrcBtn->setChecked(checked);
    m_desktopLrcBtn->update();
}

void PlayerBar::setLoading(bool loading)
{
    if (m_isLoading == loading) return;
    m_isLoading = loading;
    m_loadingAngle = 0;

    if (loading) {
        if (m_playBtn) {
            m_playBtn->setProperty("pbLoading", true);
            m_playBtn->setProperty("pbSpinAngle", 0);
            m_playBtn->setEnabled(false);
            m_playBtn->setToolTip(I18n::instance().tr("loading"));
            m_playBtn->update();
        }
        if (!m_loadingTimer) {
            m_loadingTimer = new QTimer(this);
            connect(m_loadingTimer, &QTimer::timeout, this, [this]() {
                if (!m_isLoading) {
                    m_loadingTimer->stop();
                    return;
                }
                m_loadingAngle = (m_loadingAngle + 6) % 360;
                if (m_playBtn) {
                    m_playBtn->setProperty("pbSpinAngle", m_loadingAngle);
                    m_playBtn->update();
                }
            });
        }
        m_loadingTimer->setInterval(16);
        m_loadingTimer->start();
    } else {
        if (m_loadingTimer)
            m_loadingTimer->stop();
        if (m_playBtn) {
            m_playBtn->setProperty("pbLoading", false);
            m_playBtn->setEnabled(true);
        }
        updateState();
    }
}

void PlayerBar::setCoverUnknownPlaceholder()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    setCoverPixmap(makeUnknownCover(kPbCoverSize, dark));
}

void PlayerBar::setCoverPixmap(const QPixmap &pm)
{
    auto *btn = qobject_cast<QPushButton *>(m_cover);
    if (!btn || pm.isNull()) return;
    QPixmap scaled = pm.scaled(kPbCoverSize, kPbCoverSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap rounded(kPbCoverSize, kPbCoverSize);
    rounded.fill(Qt::transparent);
    QPainter p(&rounded);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(0, 0, kPbCoverSize, kPbCoverSize, kPbCoverRadius, kPbCoverRadius);
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled);
    btn->setIcon(QIcon(rounded));
}

void PlayerBar::updateState()
{
    if (!m_engine) return;
    // Use isActuallyPlaying() to check the real QMediaPlayer state,
    // so the button shows pause icon during fadeOut until audio actually pauses.
    bool playing = m_engine->isActuallyPlaying();
    refreshBarLyricSlot();
    if (m_playBtn) {
        if (!m_isLoading)
            m_playBtn->setProperty("pbLoading", false);
        m_playBtn->setProperty("pbPlaying", playing);
        if (!m_isLoading) {
            m_playBtn->setToolTip(playing ? I18n::instance().tr("pause") : I18n::instance().tr("play"));
        }
        m_playBtn->update();
    }
}

void PlayerBar::updatePlayModeBtn(const QString &mode)
{
    if (!m_playModeBtn) return;
    if (mode == QStringLiteral("single")) {
        m_playModeBtn->setProperty("pbPlayMode", 1);
        m_playModeBtn->setToolTip(I18n::instance().tr("playModeSingle"));
    } else if (mode == QStringLiteral("random")) {
        m_playModeBtn->setProperty("pbPlayMode", 2);
        m_playModeBtn->setToolTip(I18n::instance().tr("playModeRandom"));
    } else {
        m_playModeBtn->setProperty("pbPlayMode", 0);
        m_playModeBtn->setToolTip(I18n::instance().tr("playModeList"));
    }
    m_playModeBtn->update();
}

void PlayerBar::applyShellBackdropChrome()
{
    applyPlayerBarGlassStyle();
}

void PlayerBar::applyPlayerBarGlassStyle()
{
    if (!m_glass)
        return;
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    GlassPaint::applyFrostedSurface(m_glass, dark);
    m_glass->setBaseColor(dark ? QColor(22, 34, 54, 210) : QColor(255, 255, 255, 210));
    m_glass->setBorderRadius(22);
}

void PlayerBar::refreshGlassBackdrop()
{
    if (m_glass)
        m_glass->refreshBackdrop();
}
