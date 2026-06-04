#include "playerpage.h"
#include "playerprogressslider.h"
#include "videorenderdialog.h"
#include "toast.h"
#include "svgicon.h"
#include "../core/apiclient.h"
#include "../core/playerengine.h"
#include "../core/usermanager.h"
#include "../core/i18n.h"
#include "../core/covercache.h"
#include "../core/httpprotocollabel.h"
#include "../core/audioquality.h"
#include "../core/embeddedlyrics.h"
#include "../core/musicdownloader.h"
#include "../core/playlistmanager.h"
#include "../theme/theme.h"
#include "../theme/thememanager.h"
#include "glasspaint.h"
#include "ui/scrollareafix.h"

#include <QPainter>
#include <QFontMetrics>
#include <QStylePainter>
#include <QStyleOptionButton>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsBlurEffect>
#include <QMouseEvent>
#include <QPushButton>
#include <QLineEdit>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QWindow>
#include <QPointer>
#include <QTimer>
#include <QtConcurrent>

namespace {

constexpr int kPlayerCoverRadius = 32;
constexpr int kPlayerMenuH = 80;
constexpr int kLyricPadLeft = 10;
constexpr int kLyricPadRight = 80;
constexpr int kLyricTopPad = 300;
constexpr int kPlayerStyleRatio = 50;
constexpr int kPlayerControlH = 80;
constexpr int kPpSidePad = 30;
constexpr int kPpMenuPad = 20;
constexpr int kControlHideMs = 3000;
/** 对齐 SPlayer overlay blur(80px) */
constexpr qreal kBackdropBlurRadius = 80.0;
/** SPlayer PlayerBackground.blur .bg-img transform: scale(1.5) */
constexpr qreal kCoverBgBlurScale = 1.5;
constexpr int kBackdropBlurMaxSide = 720;
/** 遮罩：SPlayer .full-player #00000060；PlayerBackground ::after rgba(0,0,0,0.5) 取较轻一层 */
constexpr int kBackdropOverlayAlpha = 0x60;
constexpr int kPpMenuBtn = 40;
constexpr int kPpCtrlBtn = 38;
constexpr int kPpPlayBtn = 44;
constexpr int kPpSideIcon = 24;
constexpr int kPpModeIcon = 20;
constexpr int kPpTransportIcon = 26;
constexpr int kPpPlayIcon = 28;
constexpr int kPpProgressMaxW = 480;
/** SPlayer .player-cover：暂停 scale(0.9)，播放 scale(1) */
constexpr qreal kCoverScalePaused = 0.9;
constexpr qreal kCoverScalePlaying = 1.0;
constexpr int kCoverScaleAnimMs = 500;
constexpr int kVolumePanelExtraLiftPx = 12;
constexpr int kVolumeLeaveAutoHideMs = 3000;
const QString kSettingsKeyVolume = QStringLiteral("player/volume");

const QColor kPpIconAccent = QColor(255, 143, 158, 255);

inline QColor blendCoverTint(const QColor &cover, const QColor &fallback, int coverWeight = 72)
{
    if (!cover.isValid())
        return fallback;
    const int w = qBound(0, coverWeight, 100);
    const int inv = 100 - w;
    return QColor((cover.red() * w + fallback.red() * inv) / 100,
                  (cover.green() * w + fallback.green() * inv) / 100,
                  (cover.blue() * w + fallback.blue() * inv) / 100);
}

inline int lyricMainFontPx(int pageH)
{
    const int h = pageH > 0 ? pageH : 720;
    return qBound(28, int(46.0 * h / 1080.0 + 0.5), 52);
}

/** SPlayer DefaultLyric：滚动 easeInOutQuad 500ms */
constexpr int kLyricScrollAnimMs = 500;
constexpr int kLyricUserScrollPauseMs = 3000;
constexpr qreal kLyricScrollCenterOffset = 0.5;
/** 距视口中心该范围内无模糊（当前行居中时保持清晰） */
constexpr int kLyricFocusBandPx = 60;
constexpr qreal kLyricFocusBlurMax = 9.0;
constexpr qreal kLyricFocusBlurRampPx = 28.0;
/** 顶部/底部渐隐区向内收：顶部消失线下移、底部消失线上移 */
constexpr int kLyricFocusTopEasePx = 48;
constexpr int kLyricFocusBottomEasePx = 48;
/** SPlayer INTRO_THRESHOLD：首句歌词 ≥3s 时在顶部显示三圆点倒计时 */
constexpr qint64 kLyricIntroCountdownMs = 3000;

/** SPlayer DefaultLyric .countdown-line */
class LyricCountdownWidget final : public QWidget {
public:
    explicit LyricCountdownWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void configure(qint64 startMs, qint64 durationMs, const QColor &dotColor)
    {
        m_startMs = startMs;
        m_durationMs = qMax<qint64>(1, durationMs);
        m_dotColor = dotColor.isValid() ? dotColor : QColor(230, 57, 80);
        updatePosition(startMs);
    }

    void setDotColor(const QColor &c)
    {
        if (c.isValid())
            m_dotColor = c;
    }

    bool shouldShow(qint64 positionMs) const
    {
        return positionMs + 500 < m_startMs + m_durationMs;
    }

    void updatePosition(qint64 positionMs)
    {
        const qint64 local = positionMs - m_startMs;
        for (int i = 0; i < 3; ++i)
            m_opacities[i] = pointOpacity(local, i);
        update();
    }

    QSize sizeHint() const override { return {280, 48}; }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        constexpr int dotPx = 28;
        constexpr int dotGap = 12;
        constexpr int leftPad = 16;
        const int y = (height() - dotPx) / 2;

        for (int visual = 0; visual < 3; ++visual) {
            const int index = 2 - visual;
            QColor c = m_dotColor;
            c.setAlphaF(qBound(0.0, m_opacities[index], 1.0));
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            const int x = leftPad + visual * (dotPx + dotGap);
            p.drawEllipse(QRectF(x, y, dotPx, dotPx));
        }
    }

private:
    qreal pointOpacity(qint64 localMs, int index) const
    {
        if (localMs <= 0 || index < 0 || index > 2)
            return 0.0;
        const qreal perPoint = qreal(m_durationMs) / 3.0;
        const qreal current = qreal(localMs);
        if (current < perPoint * (index + 1)) {
            const qreal percentage = (current - perPoint * qreal(index)) / perPoint;
            return 0.1 + 0.7 * (1.0 - percentage);
        }
        return 0.1;
    }

    qint64 m_startMs = 0;
    qint64 m_durationMs = 1;
    QColor m_dotColor = QColor(230, 57, 80);
    qreal m_opacities[3] = {0.0, 0.0, 0.0};
};

/** SPlayer smoothScrollTo：easeInOutQuad */
static QEasingCurve splayerLyricScrollCurve()
{
    return QEasingCurve(QEasingCurve::InOutQuad);
}

static void configureLyricLabel(QLabel *label)
{
    if (!label)
        return;
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setContentsMargins(0, 0, 0, 0);
}

/** 仅约束宽度，高度由 sizeHint 决定，避免多行歌词与翻译之间被撑出空隙 */
static void applyLyricLabelWrapWidth(QLabel *label, int textW)
{
    if (!label || textW <= 0)
        return;
    configureLyricLabel(label);
    label->setFixedWidth(textW);
    label->setMinimumHeight(0);
    label->setMaximumHeight(QWIDGETSIZE_MAX);
}

static QWidget *lyricLineInnerHost(QWidget *lineWidget)
{
    if (!lineWidget)
        return nullptr;
    if (auto *inner = lineWidget->findChild<QWidget *>(QStringLiteral("lyricLineInner")))
        return inner;
    return lineWidget;
}

static void relayoutSingleLyricLine(QWidget *lineWidget, int textW)
{
    if (!lineWidget || textW <= 0)
        return;
    for (auto *label : lineWidget->findChildren<QLabel *>()) {
        const QString on = label->objectName();
        if (on == QLatin1String("lyricText") || on == QLatin1String("lyricTranslation")) {
            applyLyricLabelWrapWidth(label, textW);
            label->adjustSize();
        }
    }
    if (auto *inner = lyricLineInnerHost(lineWidget))
        inner->adjustSize();
    lineWidget->adjustSize();
    lineWidget->updateGeometry();
}

QString formatPlaybackTime(qint64 ms)
{
    const qint64 sec = ms / 1000;
    return QStringLiteral("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QChar('0'));
}

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

/** 顶栏高度内空白区用于拖动无边框窗口（对齐 SPlayer PlayerMenu drag-dom） */
static bool isInPlayerPageTopDragRegion(const PlayerPage *page, const QPoint &globalPos)
{
    if (!page || !page->isVisible())
        return false;
    const QPoint local = page->mapFromGlobal(globalPos);
    return local.y() >= 0 && local.y() < kPlayerMenuH;
}

static bool blocksPlayerPageWindowDrag(QWidget *w)
{
    if (!w)
        return false;
    if (qobject_cast<QPushButton *>(w) || qobject_cast<QLineEdit *>(w)
        || qobject_cast<QAbstractSlider *>(w) || qobject_cast<QAbstractScrollArea *>(w)) {
        return true;
    }
    for (QWidget *p = w; p; p = p->parentWidget()) {
        if (qobject_cast<QPushButton *>(p) || qobject_cast<QAbstractSlider *>(p)
            || qobject_cast<QAbstractScrollArea *>(p)) {
            return true;
        }
    }
    return false;
}

static void startFramelessWindowMove(QWidget *page)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QWidget *win = page ? page->window() : nullptr;
    if (win && win->windowHandle())
        win->windowHandle()->startSystemMove();
#else
    Q_UNUSED(page);
#endif
}

static void toggleFramelessWindowMaximize(QWidget *page)
{
    QWidget *win = page ? page->window() : nullptr;
    if (!win)
        return;
    win->isMaximized() ? win->showNormal() : win->showMaximized();
}

static QColor pixmapAverageColor(const QPixmap &src)
{
    if (src.isNull())
        return QColor(230, 57, 80);
    QImage img = src.toImage().scaled(24, 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    qint64 r = 0, g = 0, b = 0;
    const int n = img.width() * img.height();
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            r += qRed(px);
            g += qGreen(px);
            b += qBlue(px);
        }
    }
    if (n <= 0)
        return QColor(230, 57, 80);
    return QColor(int(r / n), int(g / n), int(b / n));
}

static QSize backdropWorkSize(const QSize &target)
{
    QSize w = target;
    if (w.width() < 1)
        w.setWidth(1280);
    if (w.height() < 1)
        w.setHeight(720);
    const int maxSide = qMax(w.width(), w.height());
    if (maxSide > kBackdropBlurMaxSide) {
        w.scale(kBackdropBlurMaxSide, kBackdropBlurMaxSide, Qt::KeepAspectRatio);
    }
    return w;
}

/** 按模糊工作分辨率渲染主界面，避免全尺寸 grab 阻塞 UI */
static QPixmap grabBackdropSource(QWidget *source, const QSize &hintTarget)
{
    if (!source)
        return {};

    const QSize sourceSize = source->size();
    if (sourceSize.width() < 1 || sourceSize.height() < 1)
        return {};

    QSize target = hintTarget;
    if (target.width() < 1 || target.height() < 1)
        target = sourceSize;
    const QSize work = backdropWorkSize(target);

    QPixmap shot(work);
    shot.fill(Qt::transparent);
    QPainter painter(&shot);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.scale(qreal(work.width()) / sourceSize.width(),
                  qreal(work.height()) / sourceSize.height());
    source->render(&painter);
    return shot;
}

/** 高斯模糊（QGraphicsBlurEffect），避免缩小-放大带来的马赛克 */
static QPixmap makeBlurredBackdrop(const QPixmap &src, const QSize &target,
                                  qreal blurRadius = kBackdropBlurRadius)
{
    if (src.isNull() || target.isEmpty())
        return {};

    const QSize work = backdropWorkSize(target);
    QPixmap cover = src.scaled(work, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
    if (cover.width() > work.width() || cover.height() > work.height()) {
        const int x = (cover.width() - work.width()) / 2;
        const int y = (cover.height() - work.height()) / 2;
        cover = cover.copy(x, y, work.width(), work.height());
    } else if (cover.size() != work) {
        cover = cover.scaled(work, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }

    QGraphicsScene scene;
    QGraphicsPixmapItem item(cover);
    QGraphicsBlurEffect effect;
    effect.setBlurRadius(blurRadius);
    effect.setBlurHints(QGraphicsBlurEffect::PerformanceHint);
    item.setGraphicsEffect(&effect);
    scene.addItem(&item);
    const QRectF bounds = item.boundingRect();
    scene.setSceneRect(bounds);

    const int pad = int(blurRadius * 2);
    QPixmap blurred(work.width() + pad * 2, work.height() + pad * 2);
    blurred.fill(Qt::transparent);
    {
        QPainter p(&blurred);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        scene.render(&p, QRectF(pad, pad, work.width(), work.height()), bounds);
    }

    QPixmap cropped = blurred.copy(pad, pad, work.width(), work.height());

    const QSize out(target.width() > 0 ? target.width() : work.width(),
                    target.height() > 0 ? target.height() : work.height());
    if (cropped.size() != out)
        cropped = cropped.scaled(out, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    // SPlayer .bg-img: blur(80px) contrast(1.2)
    // 注意：这里不能用 Premultiplied 格式直接做 RGB 对比度拉伸，
    // 半透明像素的 RGB 已经按 alpha 预乘，二次放大会导致“炸色”。
    QImage img = cropped.toImage().convertToFormat(QImage::Format_ARGB32);
    constexpr qreal kContrast = 1.18;
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QRgb px = line[x];
            auto adj = [](int v) {
                return qBound(0, int((v - 128) * kContrast + 128), 255);
            };
            line[x] = qRgba(adj(qRed(px)), adj(qGreen(px)), adj(qBlue(px)), qAlpha(px));
        }
    }
    return QPixmap::fromImage(img);
}

/** 顶/底常驻暗角（与 SPlayer 一致：整页统一 backdrop，边缘略压暗，非二次模糊块） */
static void paintChromeEdgeGradients(QPainter &p, const QRect &pageRect)
{
    if (pageRect.width() < 1 || pageRect.height() < 1)
        return;
    {
        QLinearGradient g(0, pageRect.top(), 0, pageRect.top() + kPlayerMenuH);
        g.setColorAt(0.0, QColor(0, 0, 0, 72));
        g.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(QRect(pageRect.x(), pageRect.y(), pageRect.width(), kPlayerMenuH), g);
    }
    {
        const int y0 = pageRect.bottom() - kPlayerControlH + 1;
        QLinearGradient g(0, y0, 0, pageRect.bottom());
        g.setColorAt(0.0, QColor(0, 0, 0, 0));
        g.setColorAt(1.0, QColor(0, 0, 0, 72));
        p.fillRect(QRect(pageRect.x(), y0, pageRect.width(), kPlayerControlH), g);
    }
}

static QPixmap makeRoundedCoverPixmap(const QPixmap &source, int size)
{
    if (size < 1 || source.isNull())
        return {};
    QPixmap rounded(size, size);
    rounded.fill(Qt::transparent);
    QPainter p(&rounded);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(0, 0, size, size, kPlayerCoverRadius, kPlayerCoverRadius);
    p.setClipPath(path);
    p.drawPixmap(0, 0, source.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    p.end();
    return rounded;
}

/** SPlayer .bg-img：width 100%、scale(1.5)、blur(80px)，居中铺放勿拉伸铺满 */
static void drawCoverBlurBackground(QPainter &p, const QPixmap &blur, const QRect &pageRect)
{
    if (blur.isNull() || pageRect.isEmpty())
        return;
    const QSize target(qMax(1, int(pageRect.width() * kCoverBgBlurScale)),
                       qMax(1, int(pageRect.height() * kCoverBgBlurScale)));
    QPixmap layer = blur.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = pageRect.x() + (pageRect.width() - layer.width()) / 2;
    const int y = pageRect.y() + (pageRect.height() - layer.height()) / 2;
    p.drawPixmap(x, y, layer);
}

enum class PpInk : int {
    Back,
    Prev,
    Next,
    PlayMain,
    Favorite,
    PlaylistAdd,
    PlayMode,
    Playlist,
    Volume,
    DesktopLyric,
};

class PlayerPageInkButton final : public QPushButton {
public:
    explicit PlayerPageInkButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setFlat(true);
        setAutoDefault(false);
        setDefault(false);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
        setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QStylePainter sp(this);
        QStyleOptionButton opt;
        initStyleOption(&opt);
        sp.drawControl(QStyle::CE_PushButton, opt);

        const auto ink = static_cast<PpInk>(property("ppInk").toInt());
        const QSize sz = iconSize().isValid() ? iconSize() : QSize(kPpSideIcon, kPpSideIcon);
        const int px = qMax(sz.width(), sz.height());
        const bool hi = isEnabled() && (underMouse() || isDown());
        QColor cN(255, 255, 255, 210);
        QColor cA = kPpIconAccent;
        for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
            if (auto *page = qobject_cast<PlayerPage *>(w)) {
                cN = page->idleIconColor();
                cA = page->accentIconColor();
                break;
            }
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRect r = rect();

        if (ink == PpInk::PlayMain) {
            QColor disc = cA;
            disc.setAlpha(hi ? 51 : 36);
            painter.setPen(Qt::NoPen);
            painter.setBrush(disc);
            painter.drawEllipse(r.adjusted(1, 1, -1, -1));
        }

        const bool dim = property("ppDim").toBool();
        if (dim)
            cN.setAlpha(153);

        QPixmap pm;
        switch (ink) {
        case PpInk::Back:
            pm = Icons::renderNamed("Down", px, hi ? cA : cN);
            break;
        case PpInk::Prev:
            pm = Icons::renderNamed("SkipPrev", px, hi ? cA : cN);
            break;
        case PpInk::Next:
            pm = Icons::renderNamed("SkipNext", px, hi ? cA : cN);
            break;
        case PpInk::PlayMain:
            pm = Icons::renderNamed(property("ppPlaying").toBool() ? "Pause" : "Play", px, hi ? cA : cN);
            break;
        case PpInk::Favorite: {
            const bool on = property("ppHeartOn").toBool();
            pm = Icons::renderNamed(on ? "Favorite" : "FavoriteBorder", px,
                                    on ? cA : (hi ? cA : cN));
            break;
        }
        case PpInk::PlaylistAdd:
            pm = Icons::renderNamed("AddList", px, hi ? cA : cN);
            break;
        case PpInk::Playlist:
            pm = Icons::renderNamed("PlayList", px, hi ? cA : cN);
            break;
        case PpInk::Volume: {
            const int band = property("ppVol").toInt();
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
        case PpInk::DesktopLyric: {
            const bool on = isCheckable() && isChecked();
            pm = Icons::renderNamed("DesktopLyric2", px, on ? cA : (hi ? cA : cN));
            break;
        }
        case PpInk::PlayMode: {
            const int m = property("ppPlayMode").toInt();
            const char *name = "Repeat";
            if (m == 1)
                name = "RepeatSong";
            else if (m == 2)
                name = "Shuffle";
            pm = Icons::renderNamed(name, px, hi ? cA : cN);
            break;
        }
        }
        if (pm.isNull())
            return;
        const QPoint topLeft(r.x() + (r.width() - pm.width()) / 2, r.y() + (r.height() - pm.height()) / 2);
        painter.drawPixmap(topLeft, pm);
        Q_UNUSED(event);
    }
};

} // namespace

#include <QPainterPath>
#include <QColor>
#include <QFrame>
#include <QFontMetrics>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QScrollBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGraphicsOpacityEffect>
#include <QGraphicsScene>
#include <QGraphicsBlurEffect>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QAbstractAnimation>
#include <QScrollBar>
#include <QDebug>
#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QDialog>
#include <QFileDialog>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QWheelEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QEasingCurve>
#include <QSlider>
#include <QCoreApplication>
#include <QCursor>

PlayerPage::PlayerPage(PlayerEngine *engine, ApiClient *apiClient, QWidget *parent)
    : QWidget(parent), m_engine(engine), m_apiClient(apiClient)
{
    m_coverMainColor = QColor(255, 200, 210);
    m_coverSecondColor = QColor(255, 255, 255, 32);
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(true);
    setMouseTracking(true);
    setupUi();

    m_controlHideTimer = new QTimer(this);
    m_controlHideTimer->setSingleShot(true);
    m_controlHideTimer->setInterval(kControlHideMs);
    connect(m_controlHideTimer, &QTimer::timeout, this, [this]() { hidePlayerChrome(); });
    if (m_controlBar)
        m_controlBar->installEventFilter(this);
    if (m_menuBar)
        m_menuBar->installEventFilter(this);
    if (m_contentHost)
        m_contentHost->installEventFilter(this);
    installEventFilter(this);
    for (QWidget *child : findChildren<QWidget *>())
        child->installEventFilter(this);

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) {
                applyPlayerPageStyle();
                update();
                if (m_ppPrevBtn)
                    m_ppPrevBtn->update();
                if (m_ppPlayBtn)
                    m_ppPlayBtn->update();
                if (m_ppNextBtn)
                    m_ppNextBtn->update();
                rebuildLyricLabels();
                // 重建后控件已换，若行号未变 updateLyricHighlight 会早退，高亮样式不会套到新 QLabel
                m_currentLyricLine = -1;
                updateLyricHighlight(m_engine->position());
                if (m_lastQuality.tier != AudioQuality::Tier::Unknown)
                    applyAudioQualityBadge(m_lastQuality);
                else
                    updateQualityBadgeStyle();
            });

    m_qualityNam = new QNetworkAccessManager(this);
    connect(&MusicDownloader::instance(), &MusicDownloader::downloadFinished, this,
            [this](const QString &) {
                if (m_musicId > 0)
                    scheduleAudioQualityProbe();
            });
    if (m_engine)
        connect(m_engine, &PlayerEngine::audioMetaReady, this, &PlayerPage::refineAudioQualityFromEngine);

    m_underlayResizeTimer = new QTimer(this);
    m_underlayResizeTimer->setSingleShot(true);
    m_underlayResizeTimer->setInterval(200);
    connect(m_underlayResizeTimer, &QTimer::timeout, this, [this]() {
        if (m_underlaySnapshot.isNull())
            return;
        const QSize target = size().isEmpty() ? m_underlayBlurTarget : size();
        if (target.isEmpty())
            return;
        m_underlayBlurTarget = target;
        scheduleUnderlayBlur(target);
    });
}

PlayerPage::~PlayerPage()
{
    if (m_ppVolumeAppFilterInstalled && QCoreApplication::instance()) {
        QCoreApplication::instance()->removeEventFilter(this);
        m_ppVolumeAppFilterInstalled = false;
    }
}

void PlayerPage::setUnderlaySnapshot(const QPixmap &snapshot, const QSize &targetSize)
{
    if (snapshot.isNull())
        return;

    m_underlaySnapshot = snapshot;
    QSize target = targetSize;
    if (target.width() < 1)
        target = size();
    if (target.width() < 1)
        target = snapshot.size();
    m_underlayBlurTarget = target;
    scheduleUnderlayBlur(target);
}

void PlayerPage::setOpenTransitionActive(bool active)
{
    if (m_openTransitionActive == active)
        return;
    m_openTransitionActive = active;
    update();
}

void PlayerPage::refreshUnderlayBackdrop(QWidget *source, const QSize &targetSize)
{
    if (!source)
        return;

    const QPixmap shot = grabBackdropSource(source, targetSize);
    if (shot.isNull())
        return;
    setUnderlaySnapshot(shot, targetSize);
}

void PlayerPage::scheduleUnderlayBlur(const QSize &target)
{
    if (m_underlaySnapshot.isNull() || target.isEmpty())
        return;

    ++m_underlayBlurGen;
    const int gen = m_underlayBlurGen;
    const QPixmap shot = m_underlaySnapshot;
    const QSize blurTarget = target;

    QtConcurrent::run([shot, blurTarget]() {
        return makeBlurredBackdrop(shot, blurTarget);
    }).then(this, [this, gen](QPixmap blurred) {
        if (gen != m_underlayBlurGen || blurred.isNull())
            return;
        m_underlayBlurPixmap = std::move(blurred);
        update();
    });
}

void PlayerPage::scheduleCoverBlur(const QPixmap &source, const QSize &target)
{
    if (source.isNull() || target.isEmpty())
        return;

    ++m_coverBlurGen;
    const int gen = m_coverBlurGen;
    const QPixmap srcCopy = source;

    QtConcurrent::run([srcCopy, target]() {
        return makeBlurredBackdrop(srcCopy, target);
    }).then(this, [this, gen](QPixmap blurred) {
        if (gen != m_coverBlurGen || blurred.isNull())
            return;
        m_bgBlurPixmap = std::move(blurred);
        update();
    });
}

void PlayerPage::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const bool dark = Theme::ThemeManager::instance().isDarkMode();

    // 不透明垫底，避免模糊图边缘 alpha 透出下层
    QColor opaqueBase(20, 18, 24);
    if (m_coverMainColor.isValid())
        opaqueBase = QColor(m_coverMainColor.red(), m_coverMainColor.green(), m_coverMainColor.blue());
    else if (!dark)
        opaqueBase = QColor(241, 243, 246);
    p.fillRect(rect(), opaqueBase);

    // 滑入动画期间跳过重型模糊底图，避免与位移动画争抢主线程
    const bool lightBackdrop = m_openTransitionActive && m_underlayBlurPixmap.isNull();

    if (!lightBackdrop && !m_underlayBlurPixmap.isNull()) {
        if (m_underlayBlurPixmap.size() == rect().size())
            p.drawPixmap(rect(), m_underlayBlurPixmap);
        else
            p.drawPixmap(rect(), m_underlayBlurPixmap.scaled(rect().size(), Qt::IgnoreAspectRatio,
                                                             Qt::FastTransformation));
    }

    if (!lightBackdrop && !m_bgBlurPixmap.isNull())
        drawCoverBlurBackground(p, m_bgBlurPixmap, rect());
    else if (m_coverMainColor.isValid()) {
        QColor fill = m_coverMainColor;
        if (dark)
            fill = fill.darker(160);
        else
            fill = fill.lighter(140);
        p.fillRect(rect(), fill);
    } else {
        GlassPaint::paintMainWindowDeepBackdrop(p, rect(), dark);
    }

    // SPlayer .full-player background-color: #00000060
    p.fillRect(rect(), QColor(0, 0, 0, kBackdropOverlayAlpha));

    // 顶/底 80px 常驻暗角（不随播控隐藏，非独立模糊图层）
    paintChromeEdgeGradients(p, rect());

    QWidget::paintEvent(event);
}

int PlayerPage::coverSideLength() const
{
    int panelW = 0;
    if (m_leftPanel && m_leftPanel->width() >= 120)
        panelW = m_leftPanel->width();
    else if (width() > 0)
        panelW = qMax(280, width() * kPlayerStyleRatio / 100);
    else
        panelW = 360;

    const int pageH = height() > 120 ? height() : 720;
    const int byPanel = int(panelW * 0.70);
    const int byVh = int(pageH * 0.50);
    return qBound(200, qMin(byPanel, byVh), 480);
}

void PlayerPage::refreshCoverLayout()
{
    layoutPlayerPageChrome();
    applyMetaTextElide();
    if (!m_coverBackdropSource.isNull()) {
        const int base = coverSideLength();
        if (m_coverFrame)
            m_coverFrame->setFixedSize(base, base);
        m_coverRoundedBase = makeRoundedCoverPixmap(m_coverBackdropSource, base);
        applyCoverVisualScale(m_coverVisualScale);
    } else {
        applyCoverVisualScale(m_coverVisualScale);
    }
}

QColor PlayerPage::idleIconColor() const
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QColor fb = dark ? QColor(255, 255, 255, 210) : QColor(33, 37, 41, 210);
    return blendCoverTint(m_coverMainColor, fb, 70);
}

QColor PlayerPage::accentIconColor() const
{
    return blendCoverTint(m_coverMainColor, kPpIconAccent, 82);
}

void PlayerPage::refreshTintedPalette()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QColor titleBase = dark ? QColor(252, 248, 255) : QColor(33, 37, 41);
    const QColor subBase = dark ? QColor(245, 240, 255, 178) : QColor(33, 37, 41, 178);
    const QColor muteBase = dark ? QColor(245, 240, 255, 130) : QColor(33, 37, 41, 130);

    const QColor titleC = blendCoverTint(m_coverMainColor, titleBase, dark ? 78 : 55);
    const QColor subC = blendCoverTint(m_coverMainColor, subBase, dark ? 62 : 48);
    const QColor muteC = blendCoverTint(m_coverMainColor, muteBase, dark ? 50 : 40);
    m_clrTitle = titleC.name(QColor::HexRgb);
    m_clrArtist = subC.name(QColor::HexArgb);
    m_clrAlbum = muteC.name(QColor::HexArgb);
    // SPlayer DefaultLyric：默认继承浅色；当前行 .on opacity 1，其余 .is-lrc opacity 0.3
    Q_UNUSED(dark);
    m_clrLyricDim = QStringLiteral("rgba(255,255,255,77)");
    m_clrLyricHi = QStringLiteral("#FFFFFF");
    m_clrLyricHiTrans = QStringLiteral("rgba(255,255,255,153)");
    m_clrLyricHiBg = QStringLiteral("rgba(255,255,255,36)");
    updateMetaIcons();

    if (m_engine && !m_lyrics.isEmpty()) {
        m_currentLyricLine = -1;
        const qint64 pos = m_engine->position();
        updateLyricCountdown(pos);
        updateLyricHighlight(pos);
    } else {
        updateLyricCountdown(m_engine ? m_engine->position() : 0);
    }
}

void PlayerPage::applyLyricLineStyle(QLabel *textLabel, QLabel *transLabel, bool isCurrent) const
{
    if (!textLabel)
        return;

    int pageH = height();
    if (pageH < 480 && m_lyricsScroll && m_lyricsScroll->viewport())
        pageH = qMax(pageH, m_lyricsScroll->viewport()->height());
    const int mainPx = lyricMainFontPx(pageH);
    const int tranPx = qMax(14, mainPx - 12);
    const QString chrome = QStringLiteral("background: transparent; padding: 0; margin: 0; border: none;");

    textLabel->setStyleSheet(QString::fromUtf8("color: %1; font-size: %2px; font-weight: %3; %4")
                               .arg(isCurrent ? m_clrLyricHi : m_clrLyricDim)
                               .arg(mainPx)
                               .arg(isCurrent ? 700 : 400)
                               .arg(chrome));
    QFont mainFont = textLabel->font();
    mainFont.setPixelSize(mainPx);
    mainFont.setWeight(isCurrent ? QFont::Bold : QFont::Normal);
    textLabel->setFont(mainFont);

    if (transLabel) {
        transLabel->setStyleSheet(QString::fromUtf8("color: %1; font-size: %2px; font-weight: 400; %3")
                                      .arg(isCurrent ? m_clrLyricHiTrans : m_clrLyricDim)
                                      .arg(tranPx)
                                      .arg(chrome));
        QFont tranFont = transLabel->font();
        tranFont.setPixelSize(tranPx);
        tranFont.setWeight(QFont::Normal);
        transLabel->setFont(tranFont);
    }
}

static QWidget *findLyricLineByIndex(const QVector<QWidget *> &lines, int index)
{
    if (index < 0)
        return nullptr;
    for (auto *lineWidget : lines) {
        if (lineWidget && lineWidget->property("lyricIndex").toInt() == index)
            return lineWidget;
    }
    return nullptr;
}

void PlayerPage::scheduleLyricFocusBlurUpdate()
{
    if (!m_lyricBlurTimer) {
        m_lyricBlurTimer = new QTimer(this);
        m_lyricBlurTimer->setSingleShot(true);
        m_lyricBlurTimer->setInterval(16);
        connect(m_lyricBlurTimer, &QTimer::timeout, this, &PlayerPage::applyLyricViewportFocusBlur);
    }
    if (!m_lyricBlurTimer->isActive())
        m_lyricBlurTimer->start();
}

void PlayerPage::applyLyricViewportFocusBlur()
{
    if (!m_lyricsScroll || m_lyricLineWidgets.isEmpty())
        return;

    QWidget *vp = m_lyricsScroll->viewport();
    if (!vp || vp->height() <= 0)
        return;

    const int scrollY = m_lyricsScroll->verticalScrollBar()->value();
    const int viewH = vp->height();
    const int focusY = scrollY + int(viewH * kLyricScrollCenterOffset);

    auto blurForDistance = [](int distPx) -> qreal {
        if (distPx <= kLyricFocusBandPx)
            return 0.0;
        return qMin(kLyricFocusBlurMax, qreal(distPx - kLyricFocusBandPx) / kLyricFocusBlurRampPx);
    };

    for (auto *lineWidget : m_lyricLineWidgets) {
        if (!lineWidget)
            continue;
        if (lineWidget->property("lyricIndex").toInt() < 0)
            continue;

        const int lineTop = lineWidget->y();
        const int lineH = lineWidget->height();
        const int lineCenterY = lineTop + lineH / 2;

        if (lineTop + lineH < scrollY - 120 || lineTop > scrollY + viewH + 120) {
            lineWidget->setGraphicsEffect(nullptr);
            continue;
        }

        int signedDist = lineCenterY - focusY;
        if (signedDist < 0)
            signedDist = qMin(0, signedDist + kLyricFocusTopEasePx);
        else
            signedDist = qMax(0, signedDist - kLyricFocusBottomEasePx);
        const qreal radius = blurForDistance(qAbs(signedDist));
        if (radius <= 0.05) {
            lineWidget->setGraphicsEffect(nullptr);
            continue;
        }

        auto *effect = qobject_cast<QGraphicsBlurEffect *>(lineWidget->graphicsEffect());
        if (!effect) {
            effect = new QGraphicsBlurEffect(lineWidget);
            effect->setBlurHints(QGraphicsBlurEffect::PerformanceHint);
            lineWidget->setGraphicsEffect(effect);
        }
        if (!qFuzzyCompare(effect->blurRadius(), radius))
            effect->setBlurRadius(radius);
    }
}

void PlayerPage::syncLyricLinesVisual(int activeLine, int previousLine)
{
    const int textW = lyricTextAreaWidth();

    for (auto *lineWidget : m_lyricLineWidgets) {
        if (!lineWidget)
            continue;
        const int idx = lineWidget->property("lyricIndex").toInt();
        if (idx < 0)
            continue;

        const bool on = (idx == activeLine);

        auto *textLabel = lineWidget->findChild<QLabel *>(QStringLiteral("lyricText"));
        auto *transLabel = lineWidget->findChild<QLabel *>(QStringLiteral("lyricTranslation"));
        applyLyricLineStyle(textLabel, transLabel, on);

        if (textW > 0 && (on || idx == previousLine))
            relayoutSingleLyricLine(lineWidget, textW);
    }

    if (m_lyricsContainer)
        m_lyricsContainer->adjustSize();

    scheduleLyricFocusBlurUpdate();
}

void PlayerPage::scrollLyricsToActiveLine(int line)
{
    if (m_lyricUserScrolling || !m_lyricsScroll || !m_lyricsContainer || line < 0)
        return;

    QWidget *lineWidget = findLyricLineByIndex(m_lyricLineWidgets, line);
    if (!lineWidget)
        return;

    m_lyricsContainer->adjustSize();

    const int y = lineWidget->mapTo(m_lyricsContainer, QPoint(0, 0)).y();
    const int viewH = m_lyricsScroll->viewport()->height();
    const int lineH = lineWidget->height();
    int target = int(y - (viewH - lineH) * kLyricScrollCenterOffset);
    auto *scrollBar = m_lyricsScroll->verticalScrollBar();
    target = qBound(scrollBar->minimum(), target, scrollBar->maximum());

    if (qAbs(scrollBar->value() - target) < 2)
        return;

    if (m_scrollAnim) {
        m_scrollAnim->stop();
        delete m_scrollAnim;
        m_scrollAnim = nullptr;
    }

    m_scrollAnim = new QPropertyAnimation(scrollBar, "value", this);
    m_scrollAnim->setDuration(kLyricScrollAnimMs);
    m_scrollAnim->setStartValue(scrollBar->value());
    m_scrollAnim->setEndValue(target);
    m_scrollAnim->setEasingCurve(splayerLyricScrollCurve());
    connect(m_scrollAnim, &QPropertyAnimation::finished, this, [this]() {
        m_scrollAnim = nullptr;
        scheduleLyricFocusBlurUpdate();
    });
    m_scrollAnim->start(QAbstractAnimation::DeleteWhenStopped);
    scheduleLyricFocusBlurUpdate();
}

int PlayerPage::lyricTextAreaWidth() const
{
    if (!m_lyricsScroll || !m_lyricsLayout)
        return 0;
    QWidget *vp = m_lyricsScroll->viewport();
    const int vpW = vp ? vp->width() : 0;
    if (vpW <= 0)
        return 0;
    const QMargins outer = m_lyricsLayout->contentsMargins();
    constexpr int kLineHorizontalPad = 32;
    return qMax(80, vpW - outer.left() - outer.right() - kLineHorizontalPad);
}

void PlayerPage::updateLyricWrapWidth()
{
    if (!m_lyricsScroll || !m_lyricsContainer || !m_lyricsLayout)
        return;

    QWidget *vp = m_lyricsScroll->viewport();
    const int vpW = vp ? vp->width() : 0;
    if (vpW <= 0)
        return;

    m_lyricsContainer->setMinimumWidth(vpW);
    m_lyricsContainer->setMaximumWidth(vpW);

    const int textW = lyricTextAreaWidth();
    if (textW <= 0)
        return;

    for (auto *lineWidget : m_lyricLineWidgets) {
        if (!lineWidget)
            continue;
        lineWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        relayoutSingleLyricLine(lineWidget, textW);
    }

    m_lyricsContainer->updateGeometry();
    m_lyricsContainer->adjustSize();
    scheduleLyricFocusBlurUpdate();

    if (m_currentLyricLine >= 0 && !m_lyricUserScrolling) {
        const int line = m_currentLyricLine;
        QTimer::singleShot(0, this, [this, line]() { scrollLyricsToActiveLine(line); });
    }
}

void PlayerPage::pauseLyricAutoScroll()
{
    m_lyricUserScrolling = true;
    if (!m_lyricScrollResumeTimer)
        m_lyricScrollResumeTimer = new QTimer(this);
    m_lyricScrollResumeTimer->setSingleShot(true);
    m_lyricScrollResumeTimer->disconnect(this);
    connect(m_lyricScrollResumeTimer, &QTimer::timeout, this, [this]() {
        m_lyricUserScrolling = false;
        scrollLyricsToActiveLine(m_currentLyricLine);
    });
    m_lyricScrollResumeTimer->start(kLyricUserScrollPauseMs);
}

void PlayerPage::updateMetaIcons()
{
    constexpr int px = 16;
    if (m_artistMetaIcon) {
        const QPixmap artistPm = Icons::renderNamed("Artist", px, QColor(m_clrArtist));
        m_artistMetaIcon->setPixmap(artistPm);
        m_artistMetaIcon->setFixedSize(px, px);
    }
    if (m_albumMetaIcon) {
        const QPixmap albumPm = Icons::renderNamed("Album", px, QColor(m_clrAlbum));
        m_albumMetaIcon->setPixmap(albumPm);
        m_albumMetaIcon->setFixedSize(px, px);
    }
}

void PlayerPage::updateQualityBadgeStyle()
{
    if (!m_qualityBadge || m_lastQuality.tier == AudioQuality::Tier::Unknown)
        return;
    m_qualityBadge->setStyleSheet(QStringLiteral(
        "QLabel#playerQualityBadge { background: transparent; border: none; padding: 0; margin: 0; }"));
}

void PlayerPage::refreshAudioQuality()
{
    scheduleAudioQualityProbe();
}

void PlayerPage::applyAudioQualityBadge(const AudioQuality::ProbeResult &result)
{
    if (!m_qualityBadge || !m_qualityRow)
        return;

    if (result.tier == AudioQuality::Tier::Unknown) {
        hideAudioQualityBadge();
        return;
    }

    m_lastQuality = AudioQuality::ensureVisibleTier(result);
    const QColor fg = QColor(m_clrTitle);
    const QByteArray iconUtf8 = AudioQuality::tierIconName(m_lastQuality.tier).toUtf8();
    constexpr int kBadgeH = 20;

    QPixmap pm = Icons::renderResourceHeight(Icons::resourcePath(iconUtf8.constData()), kBadgeH, fg);
    if (pm.isNull())
        pm = Icons::renderResourceHeight(Icons::resourcePath("HQ"), kBadgeH, fg);

    m_qualityBadge->setPixmap(pm);
    m_qualityBadge->setText({});
    m_qualityBadge->setToolTip(AudioQuality::tierTooltip(m_lastQuality.tier, m_lastQuality));
    m_qualityBadge->setFixedSize(qMax(28, pm.width()), qMax(18, pm.height()));

    updateQualityBadgeStyle();
    m_qualityBadge->show();
    m_qualityRow->show();
    m_qualityRow->setFixedHeight(qMax(22, m_qualityBadge->height() + 2));
    relayoutLeftInfoColumn();
}

void PlayerPage::hideAudioQualityBadge()
{
    if (!m_qualityBadge || !m_qualityRow)
        return;
    m_lastQuality = {};
    m_qualityBadge->clear();
    m_qualityBadge->hide();
    m_qualityRow->hide();
    relayoutLeftInfoColumn();
}

void PlayerPage::refineAudioQualityFromEngine()
{
    if (!m_engine)
        return;
    // 文件头探测（本地/缓存）优先级更高，避免 FLAC 被播放器元数据回退成 HQ。
    if (m_hasFileProbedQuality)
        return;
    const int bps = AudioQuality::normalizeBitrateBps(m_engine->audioBitRateBps());
    if (bps <= 0)
        return;
    AudioQuality::ProbeResult r;
    r.bitrateBps = bps;
    r.tier = AudioQuality::tierFromBitrateBps(bps);
    r = AudioQuality::ensureVisibleTier(r);
    if (m_qualityFromPlayerMeta && m_lastQuality.tier == r.tier && m_lastQuality.bitrateBps == r.bitrateBps)
        return;
    m_qualityFromPlayerMeta = true;
    applyAudioQualityBadge(r);
}

void PlayerPage::scheduleAudioQualityProbe()
{
    if (!m_qualityBadge)
        return;

    if (m_qualityReply) {
        m_qualityReply->abort();
        m_qualityReply = nullptr;
    }

    ++m_qualityProbeGen;
    const int probeGen = m_qualityProbeGen;
    m_qualityFromPlayerMeta = false;
    m_hasFileProbedQuality = false;

    const MusicInfo info = m_engine ? m_engine->currentMusic() : MusicInfo{};
    const int musicId = m_musicId != 0 ? m_musicId : info.id;

    if (musicId == 0 && !info.isLocalFile()) {
        hideAudioQualityBadge();
        return;
    }

    AudioQuality::ProbeResult initial =
        AudioQuality::guessInitialTier(info.isLocalFile(), info.localPath);
    if (musicId > 0) {
        const QString cached = MusicDownloader::cachedAudioFilePath(musicId);
        if (!cached.isEmpty() && QFile::exists(cached))
            initial = AudioQuality::probeFile(cached);
    }

    if (AudioQuality::isDefinitiveProbe(initial)) {
        m_fileProbedQuality = initial;
        m_hasFileProbedQuality = true;
        applyAudioQualityBadge(initial);
        return;
    }

    if (info.isLocalFile() && !info.localPath.isEmpty()) {
        applyAudioQualityBadge(initial);
        return;
    }

    // 远程文件：如果是“后缀兜底”的初始档位（可能只有 HQ/MQ、没有任何数值），
    // 先展示出来避免闪烁，但仍会继续走 HTTP HEAD + Range 探测以升级到更准确的 Hi-Res。
    if (initial.tier != AudioQuality::Tier::Unknown)
        applyAudioQualityBadge(initial);
    else
        hideAudioQualityBadge();

    if (musicId <= 0)
        return;

    const QUrl url(QString::fromUtf8("%1/api/music/file/%2").arg(Theme::kApiBase).arg(musicId));
    if (!m_qualityNam)
        return;

    auto finishProbe = [this](AudioQuality::ProbeResult result) {
        if (result.tier == AudioQuality::Tier::Unknown)
            return;
        applyAudioQualityBadge(result);
    };

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *headReply = m_qualityNam->head(req);
    m_qualityReply = headReply;
    QPointer<QNetworkReply> headPtr(headReply);
    connect(headReply, &QNetworkReply::finished, this,
            [this, probeGen, musicId, url, finishProbe, headPtr]() {
        if (!headPtr) {
            return;
        }

        // 只有当当前成员仍指向本次 headReply 时才清理，避免误清理新请求。
        if (m_qualityReply == headPtr)
            m_qualityReply = nullptr;

        if (probeGen != m_qualityProbeGen || musicId != m_musicId) {
            headPtr->deleteLater();
            return;
        }

        AudioQuality::ProbeResult result;
        QString ctLower;
        if (headPtr->error() == QNetworkReply::NoError) {
            const QString ct = headPtr->header(QNetworkRequest::ContentTypeHeader).toString();
            ctLower = ct.toLower();
            const qint64 len = headPtr->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            result = AudioQuality::probeHttpHint(ct, len, trackDurationSec());
        }
        headPtr->deleteLater();

        const bool losslessCt =
            ctLower.contains(QLatin1String("flac")) || ctLower.contains(QLatin1String("wav"));
        if (result.tier != AudioQuality::Tier::Unknown && !losslessCt) {
            finishProbe(result);
            return;
        }

        if (!m_qualityNam) {
            if (result.tier != AudioQuality::Tier::Unknown)
                finishProbe(result);
            return;
        }

        QNetworkRequest getReq(url);
        getReq.setRawHeader("Range", "bytes=0-16383");
        QNetworkReply *rangeReply = m_qualityNam->get(getReq);
        QPointer<QNetworkReply> rangePtr(rangeReply);
        connect(rangeReply, &QNetworkReply::finished, this,
                [this, probeGen, musicId, rangePtr, finishProbe, result]() {
                    if (!rangePtr) {
                        return;
                    }
                    if (probeGen != m_qualityProbeGen || musicId != m_musicId) {
                        rangePtr->deleteLater();
                        return;
                    }
                    AudioQuality::ProbeResult r2 = result;
                    if (rangePtr->error() == QNetworkReply::NoError)
                        r2 = AudioQuality::probeBuffer(rangePtr->readAll());
                    if (r2.tier == AudioQuality::Tier::Unknown && result.tier != AudioQuality::Tier::Unknown)
                        r2 = result;
                    else if (result.tier != AudioQuality::Tier::Unknown
                             && r2.tier != AudioQuality::Tier::Unknown
                             && int(r2.tier) < int(result.tier))
                        r2.tier = result.tier;
                    rangePtr->deleteLater();
                    // Range 已读出容器头（采样率/位深）时，以解析结果为准，勿让 QMediaPlayer 的
                    // AudioBitRate（常低于 2Mbps 或仅为解码比特率）在 durationChanged 里把 Hi-Res 盖成 SQ。
                    if (r2.sampleRateHz > 0 || r2.bitsPerSample > 0) {
                        m_fileProbedQuality = r2;
                        m_hasFileProbedQuality = true;
                    }
                    finishProbe(r2);
                });
    });
}

void PlayerPage::applyPlayerPageStyle()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    refreshTintedPalette();

    const QString backFg = m_clrTitle;

    const int backBgA = dark ? 18 : 22;
    const int backBdA = dark ? 32 : 40;
    const int backHiA = dark ? 42 : 55;
    const int backHiBdA = dark ? 62 : 75;

    setStyleSheet(QString::fromUtf8(
                      "#playerPage { background: transparent; }"

                      "#playerBackBtn { "
                      "  background: rgba(230,57,80,%5); color: %1; font-size: 20px; "
                      "  border: 1px solid rgba(230,57,80,%6); border-radius: 22px; }"
                      "#playerBackBtn:hover { "
                      "  background: rgba(230,57,80,%7); color: %1; border-color: rgba(230,57,80,%8); }"

                      "#playerLeftPanel, #playerRightPanel { background: transparent; }"

                      "#playerCoverFrame { "
                      "  background: transparent; border: none; }"
                      "#playerCoverImage { "
                      "  background: transparent; border: none; }"

                      "#playerSongTitleLabel { "
                      "  color: %2; font-size: 26px; font-weight: 700; "
                      "  background: transparent; }"

                      "#playerMetaIcon { background: transparent; }"

                      "#playerArtistLabel { "
                      "  color: %3; font-size: 16px; font-weight: 400; "
                      "  background: transparent; }"

                      "#playerAlbumLabel { "
                      "  color: %4; font-size: 16px; font-weight: 400; "
                      "  background: transparent; }"

                      "#playerVideoRenderBtn, #playerVideoDownloadBtn { "
                      "  background: rgba(230,57,80,%5); color: %1; font-size: 13px; font-weight: 600; "
                      "  border: 1px solid rgba(230,57,80,%6); border-radius: 18px; padding: 8px 18px; }"
                      "#playerVideoRenderBtn:hover, #playerVideoDownloadBtn:hover { "
                      "  background: rgba(230,57,80,%7); border-color: rgba(230,57,80,%8); }"
                      "#playerVideoRenderBtn:disabled { "
                      "  color: rgba(255,255,255,0.35); background: rgba(120,120,120,0.25); border-color: rgba(255,255,255,0.08); }"

                      "#playerVideoStatusLbl { "
                      "  color: %3; font-size: 12px; background: transparent; "
                      "  qproperty-alignment: 'AlignCenter'; }"

                      "#lyricsScroll { "
                      "  background: transparent; border: none; }"
                      "#lyricsScroll > QWidget { background: transparent; }"

                      "#lyricsScroll QScrollBar:vertical { width: 0px; max-width: 0px; background: transparent; }"
                      "#lyricsScroll QScrollBar::handle:vertical { width: 0px; min-height: 0; background: transparent; }"
                      "#lyricsScroll QScrollBar::add-line:vertical, #lyricsScroll QScrollBar::sub-line:vertical { height: 0; width: 0; }"
                      "#lyricsScroll QScrollBar::add-page:vertical, #lyricsScroll QScrollBar::sub-page:vertical { background: transparent; }"

                      "#playerContentHost, #playerLeftPanel, #playerRightPanel { "
                      "  background: transparent; }"
                      "#playerControlBar { background: transparent; }"
                      "#ppCurTime, #ppDurTime { "
                      "  color: %3; font-size: 12px; background: transparent; }"

                      "#ppDesktopLrcBtn { "
                      "  background: transparent; border: none; border-radius: 12px; "
                      "  color: rgba(255,255,255,215); padding: 0; }"
                      "#ppDesktopLrcBtn:hover { "
                      "  background: rgba(255,255,255,26); color: #FFFFFF; }"
                      "#ppDesktopLrcBtn:pressed { "
                      "  background: rgba(255,255,255,14); }"
                      "#ppDesktopLrcBtn:checked { "
                      "  background: rgba(230,57,80,48); color: #FFF5F7; }")
                      .arg(backFg)
                      .arg(m_clrTitle)
                      .arg(m_clrArtist)
                      .arg(m_clrAlbum)
                      .arg(backBgA)
                      .arg(backBdA)
                      .arg(backHiA)
                      .arg(backHiBdA));

    applyMetaLabelFonts();
    applyMetaTextElide();
}

void PlayerPage::setupPlayerControl()
{
    m_controlBar = new QWidget(this);
    m_controlBar->setObjectName(QStringLiteral("playerControlBar"));
    m_controlBar->setMouseTracking(true);

    auto *barLay = new QHBoxLayout(m_controlBar);
    barLay->setContentsMargins(0, 0, 0, 0);
    barLay->setSpacing(0);

    m_ppLeftTools = new QWidget(m_controlBar);
    m_ppLeftTools->setObjectName(QStringLiteral("ppLeftTools"));
    auto *leftLay = new QHBoxLayout(m_ppLeftTools);
    leftLay->setContentsMargins(kPpSidePad, 0, kPpSidePad, 0);
    leftLay->setSpacing(0);
    leftLay->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    m_backBtn = new PlayerPageInkButton(m_ppLeftTools);
    m_backBtn->setObjectName(QStringLiteral("playerBackBtn"));
    m_backBtn->setFixedSize(kPpMenuBtn, kPpMenuBtn);
    m_backBtn->setIconSize(QSize(kPpSideIcon, kPpSideIcon));
    m_backBtn->setProperty("ppInk", int(PpInk::Back));
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setToolTip(I18n::instance().tr("back"));
    connect(m_backBtn, &QPushButton::clicked, this, [this]() { emit backRequested(); });

    m_ppHeartBtn = new PlayerPageInkButton(m_ppLeftTools);
    m_ppHeartBtn->setFixedSize(kPpMenuBtn, kPpMenuBtn);
    m_ppHeartBtn->setIconSize(QSize(kPpSideIcon, kPpSideIcon));
    m_ppHeartBtn->setProperty("ppInk", int(PpInk::Favorite));
    m_ppHeartBtn->setProperty("ppHeartOn", false);
    m_ppHeartBtn->setCursor(Qt::PointingHandCursor);
    m_ppHeartBtn->setToolTip(I18n::instance().tr("addToFavorites"));
    connect(m_ppHeartBtn, &QPushButton::clicked, this, [this]() {
        if (m_musicId > 0)
            emit favoriteClicked(m_musicId);
    });

    m_ppAddToPlaylistBtn = new PlayerPageInkButton(m_ppLeftTools);
    m_ppAddToPlaylistBtn->setFixedSize(kPpMenuBtn, kPpMenuBtn);
    m_ppAddToPlaylistBtn->setIconSize(QSize(kPpSideIcon, kPpSideIcon));
    m_ppAddToPlaylistBtn->setProperty("ppInk", int(PpInk::PlaylistAdd));
    m_ppAddToPlaylistBtn->setCursor(Qt::PointingHandCursor);
    m_ppAddToPlaylistBtn->setToolTip(I18n::instance().tr("addToPlaylist"));
    m_ppAddToPlaylistBtn->setEnabled(false);
    connect(m_ppAddToPlaylistBtn, &QPushButton::clicked, this, [this]() {
        if (m_musicId > 0)
            emit addToPlaylistClicked(m_musicId);
    });

    leftLay->addWidget(m_backBtn);
    leftLay->addWidget(m_ppHeartBtn);
    leftLay->addWidget(m_ppAddToPlaylistBtn);

    auto *center = new QWidget(m_controlBar);
    center->setObjectName(QStringLiteral("playerControlCenter"));
    auto *centerLay = new QVBoxLayout(center);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(4);
    centerLay->setAlignment(Qt::AlignCenter);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(12);
    btnRow->setAlignment(Qt::AlignCenter);

    m_ppPrevBtn = new PlayerPageInkButton(center);
    m_ppPrevBtn->setObjectName(QStringLiteral("ppPrevBtn"));
    m_ppPrevBtn->setFixedSize(kPpCtrlBtn, kPpCtrlBtn);
    m_ppPrevBtn->setIconSize(QSize(kPpTransportIcon, kPpTransportIcon));
    m_ppPrevBtn->setProperty("ppInk", int(PpInk::Prev));
    m_ppPrevBtn->setCursor(Qt::PointingHandCursor);
    m_ppPrevBtn->setToolTip(I18n::instance().tr("previous"));
    connect(m_ppPrevBtn, &QPushButton::clicked, this, &PlayerPage::previousClicked);

    m_ppPlayBtn = new PlayerPageInkButton(center);
    m_ppPlayBtn->setObjectName(QStringLiteral("ppPlayBtn"));
    m_ppPlayBtn->setFixedSize(kPpPlayBtn, kPpPlayBtn);
    m_ppPlayBtn->setIconSize(QSize(kPpPlayIcon, kPpPlayIcon));
    m_ppPlayBtn->setProperty("ppInk", int(PpInk::PlayMain));
    m_ppPlayBtn->setProperty("ppPlaying", false);
    m_ppPlayBtn->setCursor(Qt::PointingHandCursor);
    m_ppPlayBtn->setToolTip(I18n::instance().tr("play"));

    m_ppNextBtn = new PlayerPageInkButton(center);
    m_ppNextBtn->setObjectName(QStringLiteral("ppNextBtn"));
    m_ppNextBtn->setFixedSize(kPpCtrlBtn, kPpCtrlBtn);
    m_ppNextBtn->setIconSize(QSize(kPpTransportIcon, kPpTransportIcon));
    m_ppNextBtn->setProperty("ppInk", int(PpInk::Next));
    m_ppNextBtn->setCursor(Qt::PointingHandCursor);
    m_ppNextBtn->setToolTip(I18n::instance().tr("next"));
    connect(m_ppNextBtn, &QPushButton::clicked, this, &PlayerPage::nextClicked);

    m_ppPlayModeBtn = new PlayerPageInkButton(center);
    m_ppPlayModeBtn->setFixedSize(kPpCtrlBtn, kPpCtrlBtn);
    m_ppPlayModeBtn->setIconSize(QSize(kPpModeIcon, kPpModeIcon));
    m_ppPlayModeBtn->setProperty("ppInk", int(PpInk::PlayMode));
    m_ppPlayModeBtn->setProperty("ppPlayMode", 0);
    m_ppPlayModeBtn->setCursor(Qt::PointingHandCursor);
    m_ppPlayModeBtn->setToolTip(I18n::instance().tr("playModeList"));
    connect(m_ppPlayModeBtn, &QPushButton::clicked, this, [this]() {
        PlaylistManager::instance().togglePlayMode();
    });

    btnRow->addWidget(m_ppPlayModeBtn);
    btnRow->addWidget(m_ppPrevBtn);
    btnRow->addWidget(m_ppPlayBtn);
    btnRow->addWidget(m_ppNextBtn);
    centerLay->addLayout(btnRow);

    auto *sliderRow = new QHBoxLayout();
    sliderRow->setSpacing(8);
    sliderRow->setAlignment(Qt::AlignVCenter);

    m_ppCurTime = new QLabel(QStringLiteral("0:00"), center);
    m_ppCurTime->setObjectName(QStringLiteral("ppCurTime"));
    m_ppCurTime->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_ppCurTime->setFixedWidth(40);

    m_ppProgress = new PlayerProgressSlider(center);
    m_ppProgress->setObjectName(QStringLiteral("ppProgress"));
    m_ppProgress->setRange(0, 1000);
    m_ppProgress->setValue(0);
    m_ppProgress->setFixedHeight(16);
    m_ppProgress->setMaximumWidth(kPpProgressMaxW);
    m_ppProgress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_ppDurTime = new QLabel(QStringLiteral("0:00"), center);
    m_ppDurTime->setObjectName(QStringLiteral("ppDurTime"));
    m_ppDurTime->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_ppDurTime->setFixedWidth(40);

    sliderRow->addWidget(m_ppCurTime);
    sliderRow->addWidget(m_ppProgress, 1);
    sliderRow->addWidget(m_ppDurTime);
    centerLay->addLayout(sliderRow);

    m_ppRightTools = new QWidget(m_controlBar);
    m_ppRightTools->setObjectName(QStringLiteral("ppRightTools"));
    auto *rightLay = new QHBoxLayout(m_ppRightTools);
    rightLay->setContentsMargins(kPpSidePad, 0, kPpSidePad, 0);
    rightLay->setSpacing(12);
    rightLay->setAlignment(Qt::AlignVCenter | Qt::AlignRight);

    m_ppPlaylistBtn = new PlayerPageInkButton(m_ppRightTools);
    m_ppPlaylistBtn->setFixedSize(kPpCtrlBtn, kPpCtrlBtn);
    m_ppPlaylistBtn->setIconSize(QSize(kPpSideIcon, kPpSideIcon));
    m_ppPlaylistBtn->setProperty("ppInk", int(PpInk::Playlist));
    m_ppPlaylistBtn->setCursor(Qt::PointingHandCursor);
    m_ppPlaylistBtn->setToolTip(I18n::instance().tr("playlist"));
    connect(m_ppPlaylistBtn, &QPushButton::clicked, this, &PlayerPage::playlistClicked);

    auto *volWrapper = new QWidget(m_ppRightTools);
    volWrapper->setObjectName(QStringLiteral("ppVolumeWrapper"));
    auto *volLay = new QHBoxLayout(volWrapper);
    volLay->setContentsMargins(0, 0, 0, 0);
    volLay->setSpacing(0);

    m_ppVolumeBtn = new PlayerPageInkButton(volWrapper);
    m_ppVolumeBtn->setFixedSize(kPpCtrlBtn, kPpCtrlBtn);
    m_ppVolumeBtn->setIconSize(QSize(kPpSideIcon, kPpSideIcon));
    m_ppVolumeBtn->setProperty("ppInk", int(PpInk::Volume));
    m_ppVolumeBtn->setProperty("ppVol", 3);
    m_ppVolumeBtn->setCursor(Qt::PointingHandCursor);
    m_ppVolumeBtn->setToolTip(I18n::instance().tr("volume"));
    volLay->addWidget(m_ppVolumeBtn);

    m_ppDesktopLrcBtn = new PlayerPageInkButton(m_ppRightTools);
    m_ppDesktopLrcBtn->setObjectName(QStringLiteral("ppDesktopLrcBtn"));
    m_ppDesktopLrcBtn->setFixedSize(kPpCtrlBtn, kPpCtrlBtn);
    m_ppDesktopLrcBtn->setIconSize(QSize(kPpSideIcon, kPpSideIcon));
    m_ppDesktopLrcBtn->setProperty("ppInk", int(PpInk::DesktopLyric));
    m_ppDesktopLrcBtn->setCheckable(true);
    m_ppDesktopLrcBtn->setCursor(Qt::PointingHandCursor);
    m_ppDesktopLrcBtn->setToolTip(I18n::instance().tr("desktopLyrics"));
    {
        QSettings s;
        m_ppDesktopLrcBtn->setChecked(s.value(QStringLiteral("desktopLyrics"), false).toBool());
    }
    connect(m_ppDesktopLrcBtn, &QPushButton::toggled, this, [this](bool on) {
        QSettings s;
        s.setValue(QStringLiteral("desktopLyrics"), on);
        emit desktopLyricsToggled(on);
        m_ppDesktopLrcBtn->update();
    });

    rightLay->addWidget(m_ppDesktopLrcBtn);
    rightLay->addWidget(volWrapper);
    rightLay->addWidget(m_ppPlaylistBtn);

    QWidget *volHost = window();
    if (!volHost)
        volHost = this;
    m_ppVolumePanel = new QWidget(volHost);
    m_ppVolumePanel->setObjectName(QStringLiteral("ppVolumePanel"));
    m_ppVolumePanel->setFixedSize(44, 168);
    m_ppVolumePanel->setFocusPolicy(Qt::NoFocus);
    m_ppVolumePanel->hide();
    m_ppVolumePanel->installEventFilter(this);
    volWrapper->installEventFilter(this);

    auto *vpLay = new QVBoxLayout(m_ppVolumePanel);
    vpLay->setContentsMargins(10, 12, 10, 12);
    vpLay->setSpacing(8);
    m_ppVolumeSlider = new QSlider(Qt::Vertical, m_ppVolumePanel);
    m_ppVolumeSlider->setRange(0, 100);
    vpLay->addWidget(m_ppVolumeSlider, 1, Qt::AlignHCenter);
    m_ppVolumeLabel = new QLabel(QStringLiteral("80%"), m_ppVolumePanel);
    m_ppVolumeLabel->setAlignment(Qt::AlignCenter);
    vpLay->addWidget(m_ppVolumeLabel);

    m_ppVolumeOpacityFx = new QGraphicsOpacityEffect(m_ppVolumePanel);
    m_ppVolumeOpacityFx->setOpacity(0.0);
    m_ppVolumePanel->setGraphicsEffect(m_ppVolumeOpacityFx);
    m_ppVolumeOpAnim = new QPropertyAnimation(m_ppVolumeOpacityFx, "opacity", this);
    m_ppVolumePosAnim = new QPropertyAnimation(m_ppVolumePanel, "pos", this);
    connect(m_ppVolumeOpAnim, &QPropertyAnimation::finished, this, [this]() {
        if (!m_ppVolumePanelClosing || !m_ppVolumePanel)
            return;
        m_ppVolumePanel->hide();
        m_ppVolumePanelClosing = false;
        if (m_ppVolumeAppFilterInstalled && QCoreApplication::instance()) {
            QCoreApplication::instance()->removeEventFilter(this);
            m_ppVolumeAppFilterInstalled = false;
        }
    });
    m_ppVolumeLeaveTimer = new QTimer(this);
    m_ppVolumeLeaveTimer->setSingleShot(true);
    m_ppVolumeLeaveTimer->setInterval(kVolumeLeaveAutoHideMs);
    connect(m_ppVolumeLeaveTimer, &QTimer::timeout, this, [this]() {
        if (!m_ppVolumePanel || !m_ppVolumePanel->isVisible() || m_ppVolumePanelClosing)
            return;
        if (!volumePanelHotRectGlobal().contains(QCursor::pos()))
            hideVolumePanel();
    });

    barLay->addWidget(m_ppLeftTools, 1);
    barLay->addWidget(center, 1);
    barLay->addWidget(m_ppRightTools, 1);

    m_ppControlOpacity = new QGraphicsOpacityEffect(m_controlBar);
    m_ppControlOpacity->setOpacity(0.0);
    m_controlBar->setGraphicsEffect(m_ppControlOpacity);
    m_ppControlOpAnim = new QPropertyAnimation(m_ppControlOpacity, "opacity", this);
    m_ppControlOpAnim->setDuration(300);
    m_ppControlOpAnim->setEasingCurve(QEasingCurve::OutCubic);

    updatePlayModeBtn(PlaylistManager::instance().playMode());
    if (m_engine)
        applyCoverVisualScale(m_engine->isActuallyPlaying() ? kCoverScalePlaying : kCoverScalePaused);
    if (width() <= 700)
        setControlSidesVisible(true);
}

void PlayerPage::connectPlayerControlEngine()
{
    if (!m_engine)
        return;

    QSettings volSettings;
    const int initialVol = qBound(0, volSettings.value(kSettingsKeyVolume, 80).toInt(), 100);
    setVolumePercentSynced(initialVol);

    connect(m_ppPlayBtn, &QPushButton::clicked, this, [this]() {
        if (m_engine->isActuallyPlaying())
            m_engine->fadeOut();
        else
            m_engine->fadeIn();
    });
    connect(m_engine, &PlayerEngine::stateChanged, this, &PlayerPage::updatePlayControlState);
    updatePlayControlState();

    connect(m_engine, &PlayerEngine::positionChanged, this, [this](qint64 pos) {
        if (m_ppCurTime)
            m_ppCurTime->setText(formatPlaybackTime(pos));
        if (!m_ppProgress || m_ppProgress->isSliderDown())
            return;
        const qint64 dur = m_engine->duration();
        if (dur > 0)
            m_ppProgress->setValue(static_cast<int>(pos * 1000 / dur));
    });
    connect(m_engine, &PlayerEngine::durationChanged, this, [this](qint64 dur) {
        if (m_ppDurTime)
            m_ppDurTime->setText(formatPlaybackTime(dur));
        if (dur > 0)
            refineAudioQualityFromEngine();
    });
    connect(m_engine, &PlayerEngine::stateChanged, this, [this](PlayerEngine::PlaybackState st) {
        if (st == PlayerEngine::Playing && m_musicId > 0)
            QTimer::singleShot(300, this, &PlayerPage::refreshAudioQuality);
    });
    connect(m_ppProgress, &QSlider::sliderReleased, this, [this]() {
        if (!m_engine || m_engine->duration() <= 0)
            return;
        const qint64 position = static_cast<qint64>(m_ppProgress->value()) * m_engine->duration() / 1000;
        m_engine->setPosition(position);
    });
    connect(m_ppVolumeBtn, &QPushButton::clicked, this, [this]() {
        if (m_ppVolumePanel && m_ppVolumePanel->isVisible() && !m_ppVolumePanelClosing)
            hideVolumePanel();
        else
            showVolumePanel();
    });
    connect(m_ppVolumeSlider, &QSlider::valueChanged, this, [this](int v) {
        setVolumePercentSynced(v);
        emit volumePercentChanged(v);
    });
    connect(m_ppVolumeSlider, &QSlider::sliderReleased, this, [this]() {
        if (!m_ppVolumeSlider)
            return;
        QSettings s;
        s.setValue(kSettingsKeyVolume, m_ppVolumeSlider->value());
    });
}

void PlayerPage::applyCoverVisualScale(qreal scale)
{
    m_coverVisualScale = qBound(kCoverScalePaused, scale, kCoverScalePlaying);
    if (!m_coverImage)
        return;
    const int base = coverSideLength();
    if (m_coverFrame)
        m_coverFrame->setFixedSize(base, base);
    const int imgSide = qMax(1, int(base * m_coverVisualScale + 0.5));
    if (!m_coverRoundedBase.isNull()) {
        m_coverImage->setPixmap(
            m_coverRoundedBase.scaled(imgSide, imgSide, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    m_coverImage->setFixedSize(imgSide, imgSide);
}

void PlayerPage::updateCoverPlayScale(bool playing)
{
    if (m_coverScalePlaying == playing && m_coverScaleAnim && m_coverScaleAnim->state() == QAbstractAnimation::Running)
        return;
    m_coverScalePlaying = playing;

    const qreal target = playing ? kCoverScalePlaying : kCoverScalePaused;
    if (m_coverScaleAnim) {
        m_coverScaleAnim->stop();
        delete m_coverScaleAnim;
        m_coverScaleAnim = nullptr;
    }

    m_coverScaleAnim = new QVariantAnimation(this);
    m_coverScaleAnim->setDuration(kCoverScaleAnimMs);
    m_coverScaleAnim->setStartValue(m_coverVisualScale);
    m_coverScaleAnim->setEndValue(target);
    QEasingCurve ec(QEasingCurve::OutBack);
    ec.setOvershoot(1.15);
    m_coverScaleAnim->setEasingCurve(ec);
    connect(m_coverScaleAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        applyCoverVisualScale(v.toReal());
    });
    connect(m_coverScaleAnim, &QVariantAnimation::finished, this, [this]() {
        m_coverScaleAnim = nullptr;
    });
    m_coverScaleAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void PlayerPage::updatePlayControlState()
{
    if (!m_ppPlayBtn || !m_engine)
        return;
    const bool playing = m_engine->isActuallyPlaying();
    if (m_ppPlayBtn->property("ppPlaying").toBool() != playing) {
        m_ppPlayBtn->setProperty("ppPlaying", playing);
        m_ppPlayBtn->update();
        updateCoverPlayScale(playing);
    }
    m_ppPlayBtn->setToolTip(I18n::instance().tr(playing ? "pause" : "play"));
}

void PlayerPage::layoutPlayerPageChrome()
{
    const int menuH = m_menuBar ? kPlayerMenuH : 0;
    const int controlH = m_controlBar ? kPlayerControlH : 0;
    if (m_menuBar) {
        m_menuBar->setGeometry(0, 0, width(), menuH);
        m_menuBar->raise();
        m_menuBar->show();
    }
    if (m_contentHost) {
        const int contentH = qMax(0, height() - menuH - controlH);
        m_contentHost->setGeometry(0, menuH, width(), contentH);
        m_contentHost->show();
    }
    if (m_controlBar) {
        m_controlBar->setFixedHeight(controlH);
        m_controlBar->setGeometry(0, qMax(0, height() - controlH), width(), controlH);
        m_controlBar->raise();
        m_controlBar->show();
    }
    if (m_menuBar)
        m_menuBar->raise();
}

void PlayerPage::updateCoverBackdrop(const QPixmap &source)
{
    if (source.isNull()) {
        ++m_coverBlurGen;
        m_bgBlurPixmap = QPixmap();
        update();
        return;
    }
    m_coverMainColor = pixmapAverageColor(source);
    m_coverSecondColor = QColor(m_coverMainColor.red(), m_coverMainColor.green(), m_coverMainColor.blue(), 32);
    const QSize pageSize = size().isEmpty() ? QSize(1280, 720) : size();
    refreshTintedPalette();
    applyPlayerPageStyle();
    applyMetaTextElide();
    updatePlayModeBtn(PlaylistManager::instance().playMode());
    update();
    scheduleCoverBlur(source, pageSize);
}

void PlayerPage::setControlSidesVisible(bool visible)
{
    if (m_controlSidesVisible == visible)
        return;
    m_controlSidesVisible = visible;
    const qreal target = (visible || width() <= 700) ? 1.0 : 0.0;

    if (m_ppControlOpAnim)
        m_ppControlOpAnim->stop();
    if (m_ppMenuOpAnim)
        m_ppMenuOpAnim->stop();
    if (m_chromeFadeAnim) {
        m_chromeFadeAnim->stop();
        delete m_chromeFadeAnim;
        m_chromeFadeAnim = nullptr;
    }

    m_chromeFadeAnim = new QVariantAnimation(this);
    m_chromeFadeAnim->setDuration(300);
    const qreal startOp = m_ppControlOpacity ? m_ppControlOpacity->opacity() : 0.0;
    m_chromeFadeAnim->setStartValue(startOp);
    m_chromeFadeAnim->setEndValue(target);
    m_chromeFadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_chromeFadeAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        const qreal o = v.toReal();
        if (m_ppMenuOpacity)
            m_ppMenuOpacity->setOpacity(o);
        if (m_ppControlOpacity)
            m_ppControlOpacity->setOpacity(o);
        const bool passMouse = (o < 0.05);
        if (m_menuBar)
            m_menuBar->setAttribute(Qt::WA_TransparentForMouseEvents, passMouse);
        if (m_controlBar)
            m_controlBar->setAttribute(Qt::WA_TransparentForMouseEvents, passMouse);
    });
    connect(m_chromeFadeAnim, &QVariantAnimation::finished, this, [this]() {
        m_chromeFadeAnim = nullptr;
    });
    m_chromeFadeAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void PlayerPage::hidePlayerChrome()
{
    if (width() <= 700)
        return;
    if (m_controlHideTimer)
        m_controlHideTimer->stop();
    setControlSidesVisible(false);
}

void PlayerPage::scheduleChromeLeaveCheck()
{
    if (width() <= 700)
        return;
    QPointer<PlayerPage> page(this);
    QTimer::singleShot(0, this, [page]() {
        if (!page || !page->isVisible())
            return;
        const QPoint globalPos = QCursor::pos();
        QWidget *win = page->window();
        if (!win || !win->geometry().contains(globalPos)) {
            page->hidePlayerChrome();
            return;
        }
        const QRect pageGlobal(page->mapToGlobal(QPoint(0, 0)), page->size());
        if (!pageGlobal.contains(globalPos))
            page->hidePlayerChrome();
    });
}

void PlayerPage::bumpControlShowTimer()
{
    if (width() <= 700) {
        setControlSidesVisible(true);
        return;
    }
    setControlSidesVisible(true);
    if (m_controlHideTimer)
        m_controlHideTimer->start();
}

void PlayerPage::updatePlayModeBtn(const QString &mode)
{
    if (!m_ppPlayModeBtn)
        return;
    if (mode == QStringLiteral("single")) {
        m_ppPlayModeBtn->setProperty("ppPlayMode", 1);
        m_ppPlayModeBtn->setToolTip(I18n::instance().tr("playModeSingle"));
    } else if (mode == QStringLiteral("random")) {
        m_ppPlayModeBtn->setProperty("ppPlayMode", 2);
        m_ppPlayModeBtn->setToolTip(I18n::instance().tr("playModeRandom"));
    } else {
        m_ppPlayModeBtn->setProperty("ppPlayMode", 0);
        m_ppPlayModeBtn->setToolTip(I18n::instance().tr("playModeList"));
    }
    m_ppPlayModeBtn->update();
}

void PlayerPage::setFavoriteStatus(bool isFavorited)
{
    if (!m_ppHeartBtn)
        return;
    m_ppHeartBtn->setProperty("ppHeartOn", isFavorited);
    m_ppHeartBtn->setToolTip(isFavorited ? I18n::instance().tr("removeFromFavorites")
                                         : I18n::instance().tr("addToFavorites"));
    m_ppHeartBtn->update();
}

void PlayerPage::setDesktopLyricsChecked(bool checked)
{
    if (!m_ppDesktopLrcBtn)
        return;
    const QSignalBlocker blocker(m_ppDesktopLrcBtn);
    m_ppDesktopLrcBtn->setChecked(checked);
    m_ppDesktopLrcBtn->update();
}

void PlayerPage::setVolumePercentSynced(int percent)
{
    if (!m_engine)
        return;
    percent = qBound(0, percent, 100);
    m_engine->setVolume(static_cast<float>(percent / 100.0));
    if (m_ppVolumeSlider) {
        const QSignalBlocker blocker(m_ppVolumeSlider);
        m_ppVolumeSlider->setValue(percent);
    }
    if (m_ppVolumeLabel)
        m_ppVolumeLabel->setText(QStringLiteral("%1%").arg(percent));
    updateVolumeIcon(percent);
}

void PlayerPage::updateVolumeIcon(int percent)
{
    if (!m_ppVolumeBtn)
        return;
    int band = 3;
    if (percent == 0)
        band = 0;
    else if (percent < 40)
        band = 1;
    else if (percent < 70)
        band = 2;
    m_ppVolumeBtn->setProperty("ppVol", band);
    m_ppVolumeBtn->update();
}

QRect PlayerPage::volumePanelHotRectGlobal() const
{
    if (!m_ppVolumeBtn || !m_ppVolumePanel)
        return {};
    const QRect btnGlobal(m_ppVolumeBtn->mapToGlobal(QPoint(0, 0)), m_ppVolumeBtn->size());
    const QRect panelGlobal(m_ppVolumePanel->mapToGlobal(QPoint(0, 0)), m_ppVolumePanel->size());
    return btnGlobal.united(panelGlobal);
}

void PlayerPage::showVolumePanel()
{
    if (!m_ppVolumePanel || !m_ppVolumeBtn || !m_ppVolumeOpacityFx || !m_ppVolumeOpAnim || !m_ppVolumePosAnim)
        return;
    m_ppVolumePanelClosing = false;
    m_ppVolumeOpAnim->stop();
    m_ppVolumePosAnim->stop();
    if (m_ppVolumeLeaveTimer)
        m_ppVolumeLeaveTimer->stop();

    QWidget *host = m_ppVolumePanel->parentWidget();
    if (!host)
        host = window();
    if (!host)
        host = this;
    const QPoint ref = m_ppVolumeBtn->mapTo(host, QPoint(0, 0));
    const int x = ref.x() + (m_ppVolumeBtn->width() - m_ppVolumePanel->width()) / 2;
    const int y = ref.y() - m_ppVolumePanel->height() + 8 - kVolumePanelExtraLiftPx;
    const QPoint finalPos(x, y);
    const QPoint startPos(x, y + 10);

    m_ppVolumePanel->move(startPos);
    m_ppVolumeOpacityFx->setOpacity(0.0);
    m_ppVolumePanel->show();
    m_ppVolumePanel->raise();

    m_ppVolumeOpAnim->setDuration(200);
    m_ppVolumeOpAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_ppVolumeOpAnim->setStartValue(0.0);
    m_ppVolumeOpAnim->setEndValue(1.0);

    m_ppVolumePosAnim->setDuration(200);
    m_ppVolumePosAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_ppVolumePosAnim->setStartValue(startPos);
    m_ppVolumePosAnim->setEndValue(finalPos);
    m_ppVolumeOpAnim->start();
    m_ppVolumePosAnim->start();

    if (!m_ppVolumeAppFilterInstalled && QCoreApplication::instance()) {
        QCoreApplication::instance()->installEventFilter(this);
        m_ppVolumeAppFilterInstalled = true;
    }
}

void PlayerPage::hideVolumePanel()
{
    if (!m_ppVolumePanel || !m_ppVolumeOpacityFx || !m_ppVolumeOpAnim || !m_ppVolumePanel->isVisible())
        return;
    if (m_ppVolumeLeaveTimer)
        m_ppVolumeLeaveTimer->stop();
    m_ppVolumeOpAnim->stop();
    if (m_ppVolumePosAnim)
        m_ppVolumePosAnim->stop();
    m_ppVolumePanelClosing = true;
    m_ppVolumeOpAnim->setDuration(160);
    m_ppVolumeOpAnim->setEasingCurve(QEasingCurve::InCubic);
    m_ppVolumeOpAnim->setStartValue(m_ppVolumeOpacityFx->opacity());
    m_ppVolumeOpAnim->setEndValue(0.0);
    m_ppVolumeOpAnim->start();
}

bool PlayerPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress && m_ppVolumePanel && m_ppVolumePanel->isVisible()
        && !m_ppVolumePanelClosing) {
        auto *me = static_cast<QMouseEvent *>(event);
        const QPoint g = me->globalPosition().toPoint();
        if (!volumePanelHotRectGlobal().contains(g))
            hideVolumePanel();
    }

    auto *target = qobject_cast<QWidget *>(watched);
    const bool onPageTree = (watched == this) || (target && isAncestorOf(target));

    if (onPageTree) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *e = static_cast<QMouseEvent *>(event);
            if (e->button() == Qt::LeftButton
                && isInPlayerPageTopDragRegion(this, e->globalPosition().toPoint())
                && !blocksPlayerPageWindowDrag(target)) {
                startFramelessWindowMove(this);
                return true;
            }
            break;
        }
        case QEvent::MouseButtonDblClick: {
            auto *e = static_cast<QMouseEvent *>(event);
            if (e->button() == Qt::LeftButton
                && isInPlayerPageTopDragRegion(this, e->globalPosition().toPoint())
                && !blocksPlayerPageWindowDrag(target)) {
                toggleFramelessWindowMaximize(this);
                return true;
            }
            break;
        }
        default:
            break;
        }
    }

    if (onPageTree) {
        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::Enter:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::Wheel:
            bumpControlShowTimer();
            break;
        case QEvent::Leave:
            scheduleChromeLeaveCheck();
            break;
        default:
            break;
        }
    }

    if (m_lyricsScroll) {
        QWidget *lyricVp = m_lyricsScroll->viewport();
        QScrollBar *lyricBar = m_lyricsScroll->verticalScrollBar();
        if (watched == lyricVp || watched == lyricBar) {
            if (event->type() == QEvent::Wheel) {
                pauseLyricAutoScroll();
            }
            if (watched == lyricVp && event->type() == QEvent::Resize) {
                updateLyricWrapWidth();
            }
        }
    }

    if (watched->objectName() == QStringLiteral("ppVolumeWrapper")) {
        if (event->type() == QEvent::Enter) {
            if (m_ppVolumeLeaveTimer)
                m_ppVolumeLeaveTimer->stop();
            showVolumePanel();
        } else if (event->type() == QEvent::Leave) {
            if (m_ppVolumePanel && m_ppVolumePanel->isVisible()) {
                if (!volumePanelHotRectGlobal().contains(QCursor::pos())) {
                    if (m_ppVolumeLeaveTimer)
                        m_ppVolumeLeaveTimer->start();
                } else if (m_ppVolumeLeaveTimer) {
                    m_ppVolumeLeaveTimer->stop();
                }
            }
        }
    } else if (watched == m_ppVolumePanel) {
        if (event->type() == QEvent::Enter) {
            if (m_ppVolumeLeaveTimer)
                m_ppVolumeLeaveTimer->stop();
        } else if (event->type() == QEvent::Leave) {
            if (!volumePanelHotRectGlobal().contains(QCursor::pos())) {
                if (m_ppVolumeLeaveTimer)
                    m_ppVolumeLeaveTimer->start();
            } else if (m_ppVolumeLeaveTimer) {
                m_ppVolumeLeaveTimer->stop();
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void PlayerPage::wheelEvent(QWheelEvent *event)
{
    bumpControlShowTimer();
    if (m_controlBar && m_controlBar->geometry().contains(event->position().toPoint()) && m_engine) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            double v = m_engine->volume();
            v += (delta > 0 ? 0.05 : -0.05);
            v = qBound(0.0, v, 1.0);
            const int pct = qBound(0, static_cast<int>(qRound(v * 100.0)), 100);
            setVolumePercentSynced(pct);
            emit volumePercentChanged(pct);
            event->accept();
            return;
        }
    }
    QWidget::wheelEvent(event);
}

void PlayerPage::applyMetaLabelFonts()
{
    if (!m_titleLabel || !m_artistLabel || !m_albumLabel)
        return;

    QFont titleFont = m_titleLabel->font();
    titleFont.setPixelSize(26);
    titleFont.setWeight(QFont::Bold);
    m_titleLabel->setFont(titleFont);

    QFont artistFont = m_artistLabel->font();
    artistFont.setPixelSize(16);
    artistFont.setWeight(QFont::Normal);
    m_artistLabel->setFont(artistFont);
    if (m_albumLabel)
        m_albumLabel->setFont(artistFont);
}

void PlayerPage::setupUi()
{
    setObjectName("playerPage");

    m_menuBar = new QWidget(this);
    m_menuBar->setObjectName(QStringLiteral("playerMenuBar"));
    m_menuBar->setFixedHeight(kPlayerMenuH);
    m_menuBar->setMouseTracking(true);
    auto *menuLay = new QHBoxLayout(m_menuBar);
    menuLay->setContentsMargins(kPpMenuPad, 0, kPpMenuPad, 0);
    menuLay->addStretch(1);

    m_ppMenuOpacity = new QGraphicsOpacityEffect(m_menuBar);
    m_ppMenuOpacity->setOpacity(0.0);
    m_menuBar->setGraphicsEffect(m_ppMenuOpacity);
    m_ppMenuOpAnim = new QPropertyAnimation(m_ppMenuOpacity, "opacity", this);
    m_ppMenuOpAnim->setDuration(300);
    m_ppMenuOpAnim->setEasingCurve(QEasingCurve::OutCubic);

    auto *rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);

    m_contentHost = new QWidget(this);
    m_contentHost->setObjectName(QStringLiteral("playerContentHost"));
    auto *hostLay = new QVBoxLayout(m_contentHost);
    hostLay->setContentsMargins(0, 0, 0, 0);
    hostLay->setSpacing(0);

    auto *contentRow = new QHBoxLayout();
    contentRow->setSpacing(0);
    contentRow->setContentsMargins(0, 0, 0, 0);

    m_leftPanel = new QWidget(m_contentHost);
    m_leftPanel->setObjectName(QStringLiteral("playerLeftPanel"));
    m_leftPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_leftPanel->setMinimumWidth(280);

    auto *leftOuter = new QVBoxLayout(m_leftPanel);
    leftOuter->setContentsMargins(0, 0, 24, 0);
    leftOuter->setSpacing(0);
    leftOuter->setAlignment(Qt::AlignCenter);

    m_leftInfoColumn = new QWidget(m_leftPanel);
    m_leftInfoColumn->setObjectName(QStringLiteral("playerLeftInfoColumn"));
    m_leftInfoColumn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    auto *infoLay = new QVBoxLayout(m_leftInfoColumn);
    infoLay->setContentsMargins(0, 0, 0, 0);
    infoLay->setSpacing(24);
    infoLay->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    m_coverFrame = new QWidget(m_leftInfoColumn);
    m_coverFrame->setObjectName(QStringLiteral("playerCoverFrame"));
    m_coverFrame->setFixedSize(320, 320);
    m_coverFrame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto *coverLay = new QGridLayout(m_coverFrame);
    coverLay->setContentsMargins(0, 0, 0, 0);
    coverLay->setSpacing(0);

    m_coverImage = new QLabel(m_coverFrame);
    m_coverImage->setObjectName(QStringLiteral("playerCoverImage"));
    m_coverImage->setScaledContents(false);
    m_coverImage->setAlignment(Qt::AlignCenter);
    m_coverImage->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    coverLay->addWidget(m_coverImage, 0, 0, Qt::AlignCenter);
    infoLay->addWidget(m_coverFrame, 0, Qt::AlignHCenter);

    m_metaPanel = new QWidget(m_leftInfoColumn);
    m_metaPanel->setObjectName(QStringLiteral("playerMetaPanel"));
    m_metaPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *metaLay = new QVBoxLayout(m_metaPanel);
    metaLay->setContentsMargins(0, 0, 0, 0);
    metaLay->setSpacing(12);
    metaLay->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    m_titleLabel = new QLabel(I18n::instance().tr("unknown"), m_metaPanel);
    m_titleLabel->setObjectName("playerSongTitleLabel");
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setWordWrap(false);
    m_titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_qualityRow = new QWidget(m_metaPanel);
    m_qualityRow->setObjectName(QStringLiteral("playerQualityRow"));
    auto *qualityLay = new QHBoxLayout(m_qualityRow);
    qualityLay->setContentsMargins(4, 0, 0, 0);
    qualityLay->setSpacing(0);
    m_qualityBadge = new QLabel(m_qualityRow);
    m_qualityBadge->setObjectName(QStringLiteral("playerQualityBadge"));
    m_qualityBadge->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_qualityBadge->hide();
    qualityLay->addWidget(m_qualityBadge, 0, Qt::AlignLeft);
    qualityLay->addStretch();
    m_qualityRow->hide();

    m_artistRow = new QWidget(m_metaPanel);
    auto *artistLay = new QHBoxLayout(m_artistRow);
    artistLay->setContentsMargins(0, 0, 0, 0);
    artistLay->setSpacing(6);
    m_artistMetaIcon = new QLabel(m_artistRow);
    m_artistMetaIcon->setObjectName(QStringLiteral("playerMetaIcon"));
    m_artistLabel = new QLabel(I18n::instance().tr("unknownArtist"), m_artistRow);
    m_artistLabel->setObjectName("playerArtistLabel");
    m_artistLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_artistLabel->setWordWrap(false);
    m_artistLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    artistLay->addWidget(m_artistMetaIcon, 0, Qt::AlignTop);
    artistLay->addWidget(m_artistLabel, 1);

    m_albumRow = new QWidget(m_metaPanel);
    auto *albumLay = new QHBoxLayout(m_albumRow);
    albumLay->setContentsMargins(0, 0, 0, 0);
    albumLay->setSpacing(6);
    m_albumMetaIcon = new QLabel(m_albumRow);
    m_albumMetaIcon->setObjectName(QStringLiteral("playerMetaIcon"));
    m_albumLabel = new QLabel(m_albumRow);
    m_albumLabel->setObjectName("playerAlbumLabel");
    m_albumLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_albumLabel->setWordWrap(false);
    m_albumLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    albumLay->addWidget(m_albumMetaIcon, 0, Qt::AlignTop);
    albumLay->addWidget(m_albumLabel, 1);

    updateMetaIcons();

    m_fullMetaTitle = m_titleLabel->text();
    m_fullMetaArtist = m_artistLabel->text();
    m_fullMetaAlbum.clear();

    metaLay->addWidget(m_titleLabel);
    metaLay->addWidget(m_qualityRow);
    metaLay->addWidget(m_artistRow);
    metaLay->addWidget(m_albumRow);
    infoLay->addWidget(m_metaPanel, 0, Qt::AlignLeft);

    m_videoStatusLbl = new QLabel(m_leftInfoColumn);
    m_videoStatusLbl->setObjectName(QStringLiteral("playerVideoStatusLbl"));
    m_videoStatusLbl->setWordWrap(true);
    m_videoStatusLbl->hide();

    m_videoRenderBtn = new QPushButton(I18n::instance().tr(QStringLiteral("videoRenderButton")), m_leftInfoColumn);
    m_videoRenderBtn->setObjectName(QStringLiteral("playerVideoRenderBtn"));
    m_videoRenderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_videoRenderBtn, &QPushButton::clicked, this, &PlayerPage::openVideoRenderDialog);

    m_videoDownloadBtn = new QPushButton(I18n::instance().tr(QStringLiteral("videoRenderDownload")), m_leftInfoColumn);
    m_videoDownloadBtn->setObjectName(QStringLiteral("playerVideoDownloadBtn"));
    m_videoDownloadBtn->setCursor(Qt::PointingHandCursor);
    m_videoDownloadBtn->hide();
    connect(m_videoDownloadBtn, &QPushButton::clicked, this, &PlayerPage::downloadRenderedVideo);

    m_videoRenderBtn->hide();
    m_videoDownloadBtn->hide();

    m_videoPollTimer = new QTimer(this);
    m_videoPollTimer->setInterval(8000);
    connect(m_videoPollTimer, &QTimer::timeout, this, &PlayerPage::pollVideoRenderStatus);

    infoLay->addWidget(m_videoStatusLbl, 0, Qt::AlignHCenter);
    infoLay->addWidget(m_videoRenderBtn, 0, Qt::AlignHCenter);
    infoLay->addWidget(m_videoDownloadBtn, 0, Qt::AlignHCenter);

    leftOuter->addStretch(1);
    leftOuter->addWidget(m_leftInfoColumn, 0, Qt::AlignCenter);
    leftOuter->addStretch(1);

    m_rightPanel = new QWidget(m_contentHost);
    m_rightPanel->setObjectName(QStringLiteral("playerRightPanel"));
    m_rightPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_rightPanel->setMinimumWidth(320);

    auto *lyricsCol = new QVBoxLayout(m_rightPanel);
    lyricsCol->setContentsMargins(kLyricPadLeft, 0, kLyricPadRight, 26);
    lyricsCol->setSpacing(0);

    m_lyricsScroll = new QScrollArea(m_rightPanel);
    m_lyricsScroll->setObjectName("lyricsScroll");
    m_lyricsScroll->setWidgetResizable(true);
    m_lyricsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_lyricsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_lyricsScroll->setFrameShape(QFrame::NoFrame);

    m_lyricsContainer = new QWidget();
    m_lyricsContainer->setObjectName(QStringLiteral("lyricsContainer"));
    m_lyricsContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_lyricsLayout = new QVBoxLayout(m_lyricsContainer);
    m_lyricsLayout->setAlignment(Qt::AlignTop);
    m_lyricsLayout->setSpacing(12);
    m_lyricsLayout->setContentsMargins(0, 0, 16, 0);

    m_lyricsScroll->setWidget(m_lyricsContainer);
    nekoPolishScrollAreaViewport(m_lyricsScroll);
    if (auto *vp = m_lyricsScroll->viewport())
        vp->installEventFilter(this);
    if (auto *scrollBar = m_lyricsScroll->verticalScrollBar()) {
        scrollBar->installEventFilter(this);
        connect(scrollBar, &QScrollBar::sliderPressed, this, [this]() { pauseLyricAutoScroll(); });
        connect(scrollBar, &QScrollBar::sliderMoved, this, [this]() { pauseLyricAutoScroll(); });
        connect(scrollBar, &QScrollBar::valueChanged, this, [this]() { scheduleLyricFocusBlurUpdate(); });
    }
    lyricsCol->addWidget(m_lyricsScroll, 1);

    contentRow->addWidget(m_leftPanel, kPlayerStyleRatio);
    contentRow->addWidget(m_rightPanel, 100 - kPlayerStyleRatio);

    hostLay->addLayout(contentRow, 1);
    rootLay->addWidget(m_contentHost, 1);

    setupPlayerControl();
    connectPlayerControlEngine();
    layoutPlayerPageChrome();

    applyPlayerPageStyle();
    applyMetaTextElide();
    relayoutLeftInfoColumn();
}

void PlayerPage::setMusicInfo(int id, const QString &title, const QString &artist,
                              const QString &album, const QString &coverUrl)
{
    const QString t = title.isEmpty() ? I18n::instance().tr("unknown") : title;
    const QString a = artist.isEmpty() ? I18n::instance().tr("unknownArtist") : artist;
    const int prevId = m_musicId;
    const QString prevCoverUrl = m_coverUrl;
    m_musicId = id;
    if (m_ppAddToPlaylistBtn)
        m_ppAddToPlaylistBtn->setEnabled(id > 0);
    if (prevId != id)
        resetVideoRenderState();
    m_trackDurationSec = qMax(1, trackDurationSec());
    m_fullMetaTitle = t;
    m_fullMetaArtist = a;
    m_fullMetaAlbum = album;
    m_titleIsPlaceholder = title.isEmpty();
    m_artistIsPlaceholder = artist.isEmpty();
    applyMetaTextElide();
    scheduleAudioQualityProbe();

    disconnect(m_coverConn);
    m_coverConn = {};

    if (m_musicId < 0) {
        m_coverUrl = coverUrl;
        const QString fu = CoverCache::resolveCoverUrl(coverUrl);
        if (fu.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
            QPixmap px;
            if (px.load(QUrl(fu).toLocalFile()))
                applyCoverPixmap(px);
            else
                applyCoverUnknownLarge();
        } else {
            applyCoverUnknownLarge();
        }
        updateVideoRenderUi();
        return;
    }

    if (m_musicId <= 0) {
        m_coverImage->clear();
        m_coverRoundedBase = QPixmap();
        updateVideoRenderUi();
        return;
    }
    // 同一首歌且封面 URL 未变：不重复走 CoverCache / 网络
    if (prevId == id && coverUrl == prevCoverUrl)
        return;

    m_coverUrl = coverUrl;
    loadCover(coverUrl);
    updateVideoRenderUi();
}

void PlayerPage::retranslate()
{
    if (m_titleIsPlaceholder)
        m_fullMetaTitle = I18n::instance().tr("unknown");
    if (m_artistIsPlaceholder)
        m_fullMetaArtist = I18n::instance().tr("unknownArtist");
    applyMetaTextElide();

    // Update no lyrics message if visible
    auto *noLyricsText = findChild<QLabel *>("noLyricsText");
    if (noLyricsText) {
        noLyricsText->setText(I18n::instance().tr("noLyrics"));
    }

    if (m_videoRenderBtn)
        m_videoRenderBtn->setText(I18n::instance().tr(QStringLiteral("videoRenderButton")));
    if (m_videoDownloadBtn)
        m_videoDownloadBtn->setText(I18n::instance().tr(QStringLiteral("videoRenderDownload")));
    if (m_ppAddToPlaylistBtn)
        m_ppAddToPlaylistBtn->setToolTip(I18n::instance().tr("addToPlaylist"));
    updateVideoRenderUi();
}

static int labelLineHeight(const QLabel *label, const QString &text, int width)
{
    if (!label)
        return 0;
    label->ensurePolished();
    const QFontMetrics fm(label->font());
    const QRect bound = fm.boundingRect(0, 0, width, 512, Qt::TextSingleLine | Qt::AlignLeft, text);
    return qMax(fm.height(), bound.height());
}

void PlayerPage::applyMetaTextElide()
{
    if (!m_titleLabel || !m_artistLabel || !m_albumLabel)
        return;

    int w = coverSideLength();
    if (m_leftPanel && m_leftPanel->width() > w + 40) {
        const int panelW = m_leftPanel->width();
        const int maxMeta = qMin(int(panelW * 0.7), int(qMax(height(), 480) * 0.55));
        w = qBound(w, maxMeta, panelW - 8);
    }

    m_titleLabel->ensurePolished();
    m_artistLabel->ensurePolished();
    m_albumLabel->ensurePolished();

    const QFontMetrics fmTitle(m_titleLabel->font());
    const QFontMetrics fmArtist(m_artistLabel->font());
    const QFontMetrics fmAlbum(m_albumLabel->font());

    const QString elidedTitle = fmTitle.elidedText(m_fullMetaTitle, Qt::ElideRight, w);
    const QString elidedArtist = fmArtist.elidedText(m_fullMetaArtist, Qt::ElideRight, w);
    const QString elidedAlbum = fmAlbum.elidedText(m_fullMetaAlbum, Qt::ElideRight, w);

    m_titleLabel->setText(elidedTitle);
    m_artistLabel->setText(elidedArtist);
    m_albumLabel->setText(elidedAlbum);

    const int titleH = labelLineHeight(m_titleLabel, elidedTitle, w);
    const int artistH = labelLineHeight(m_artistLabel, elidedArtist, w);
    const int albumH = m_fullMetaAlbum.isEmpty() ? 0 : labelLineHeight(m_albumLabel, elidedAlbum, w);

    m_titleLabel->setFixedSize(w, titleH);
    m_artistLabel->setFixedSize(qMax(0, w - 26), artistH);
    if (albumH > 0) {
        m_albumLabel->setFixedSize(qMax(0, w - 26), albumH);
        m_albumRow->show();
    } else {
        m_albumRow->hide();
    }
    if (m_artistRow)
        m_artistRow->setFixedHeight(artistH);

    m_titleLabel->setToolTip(m_fullMetaTitle != elidedTitle ? m_fullMetaTitle : QString());
    m_artistLabel->setToolTip(m_fullMetaArtist != elidedArtist ? m_fullMetaArtist : QString());
    m_albumLabel->setToolTip(m_fullMetaAlbum != elidedAlbum ? m_fullMetaAlbum : QString());

    relayoutLeftInfoColumn();
}

void PlayerPage::relayoutLeftInfoColumn()
{
    if (!m_metaPanel || !m_leftInfoColumn)
        return;

    const int titleH = m_titleLabel ? m_titleLabel->height() : 0;
    const int qualityH =
        (m_qualityRow && m_qualityRow->isVisible()) ? m_qualityRow->sizeHint().height() : 0;
    const int artistH = m_artistRow ? m_artistRow->height() : 0;
    const int albumH = (m_albumRow && m_albumRow->isVisible()) ? m_albumRow->height() : 0;
    const int coverW = coverSideLength();
    const int metaW = m_titleLabel ? m_titleLabel->width() : coverW;
    const int metaH = titleH + qualityH + artistH + albumH + 24;
    m_metaPanel->setFixedSize(metaW, metaH);

    m_leftInfoColumn->setFixedWidth(qMax(coverW, metaW));
    if (m_coverFrame)
        m_coverFrame->setFixedSize(coverW, coverW);
    m_leftInfoColumn->adjustSize();
}

void PlayerPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutPlayerPageChrome();
    if (!m_underlaySnapshot.isNull())
        m_underlayResizeTimer->start();
    if (isVisible())
        refreshCoverLayout();

    if (m_lyricsScroll && m_lyricsContainer) {
        if (auto *top = m_lyricsContainer->findChild<QWidget *>(QStringLiteral("lyricPadTop")))
            top->setFixedHeight(kLyricTopPad);
        if (auto *bot = m_lyricsContainer->findChild<QWidget *>(QStringLiteral("lyricPadBottom")))
            bot->setFixedHeight(qMax(m_lyricsScroll->viewport()->height(), 120));
        updateLyricWrapWidth();
    }
}

void PlayerPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    layoutPlayerPageChrome();
    bumpControlShowTimer();
    QTimer::singleShot(0, this, &PlayerPage::refreshCoverLayout);

    // 与主窗口滑入动画同时进行内容淡入会叠加掉帧，仅保留滑入
    if (m_openTransitionActive || !m_contentHost)
        return;
    auto *opacity = new QGraphicsOpacityEffect(m_contentHost);
    opacity->setOpacity(0.0);
    m_contentHost->setGraphicsEffect(opacity);

    auto *fadeIn = new QPropertyAnimation(opacity, "opacity", m_contentHost);
    fadeIn->setDuration(350);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutCubic);
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);

    connect(fadeIn, &QPropertyAnimation::finished, m_contentHost, [this]() {
        if (m_contentHost)
            m_contentHost->setGraphicsEffect(nullptr);
    });
}

void PlayerPage::applyCoverPixmap(const QPixmap &sourcePixmap)
{
    if (sourcePixmap.isNull())
        return;
    const int base = coverSideLength();
    if (m_coverFrame)
        m_coverFrame->setFixedSize(base, base);
    m_coverRoundedBase = makeRoundedCoverPixmap(sourcePixmap, base);
    applyCoverVisualScale(m_coverVisualScale);
    m_coverBackdropSource = sourcePixmap;
    updateCoverBackdrop(sourcePixmap);
}

void PlayerPage::applyCoverUnknownLarge()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const int base = coverSideLength();
    if (m_coverFrame)
        m_coverFrame->setFixedSize(base, base);
    QPixmap pm(base, base);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(0, 0, base, base, kPlayerCoverRadius, kPlayerCoverRadius);
    p.fillPath(path, dark ? QColor(52, 44, 72) : QColor(236, 232, 248));
    p.setPen(dark ? QColor(230, 57, 80, 220) : QColor(111, 66, 193, 200));
    QFont f = p.font();
    f.setPixelSize(qBound(28, base / 6, 56));
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, I18n::instance().tr(QStringLiteral("unknown")));
    p.end();
    m_coverRoundedBase = pm;
    applyCoverVisualScale(m_coverVisualScale);
    m_coverBackdropSource = QPixmap();
    m_bgBlurPixmap = QPixmap();
    update();
}

void PlayerPage::loadCover(const QString &url)
{
    if (m_musicId <= 0)
        return;

    const QString cacheKey = QString::number(m_musicId);
    QString fetchUrl = CoverCache::resolveCoverUrl(url);
    if (fetchUrl.isEmpty()) {
        fetchUrl = QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(m_musicId);
    }

    CoverCache *cc = CoverCache::instance();
    if (QPixmap cached = cc->get(cacheKey); !cached.isNull()) {
        applyCoverPixmap(cached);
        return;
    }

    if (m_coverImage)
        m_coverImage->clear();
    m_coverRoundedBase = QPixmap();

    disconnect(m_coverConn);
    const int expectId = m_musicId;
    m_coverConn = connect(cc, &CoverCache::coverLoaded, this,
                            [this, cacheKey, expectId](const QString &id, const QPixmap &pix) {
                                if (id != cacheKey)
                                    return;
                                if (m_musicId != expectId)
                                    return;
                                if (pix.isNull())
                                    return;
                                applyCoverPixmap(pix);
                            });
    cc->fetchCover(cacheKey, fetchUrl);
}

void PlayerPage::loadLyricsForTrack(const MusicInfo &info)
{
    ++m_lyricsFetchGeneration;
    const int lyricsGen = m_lyricsFetchGeneration;
    m_currentLyricLine = -1;

    if (info.isLocalFile()) {
        const int cacheKey = info.id;
        if (m_lyricsCache.contains(cacheKey)) {
            m_lyrics = m_lyricsCache.value(cacheKey);
            rebuildLyricLabels();
            return;
        }

        m_lyrics.clear();
        rebuildLyricLabels();

        QString raw = EmbeddedLyrics::readEmbeddedLyricsText(info.localPath);
        if (raw.trimmed().isEmpty()) {
            const QFileInfo fi(info.localPath);
            const QString lrcPath = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
                + QLatin1String(".lrc");
            QFile f(lrcPath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QByteArray bytes = f.readAll();
                f.close();
                raw = QString::fromUtf8(bytes);
            }
        }

        if (!raw.trimmed().isEmpty())
            applyLyricsRawText(raw);
        rebuildLyricLabels();

        if (!m_lyrics.isEmpty()) {
            constexpr int kMax = 64;
            if (m_lyricsCache.size() >= kMax && !m_lyricsCache.contains(cacheKey))
                m_lyricsCache.remove(m_lyricsCache.constBegin().key());
            m_lyricsCache.insert(cacheKey, m_lyrics);
        }
        return;
    }

    const int musicId = info.id;
    if (musicId <= 0) {
        m_lyrics.clear();
        rebuildLyricLabels();
        return;
    }

    if (m_lyricsCache.contains(musicId)) {
        m_lyrics = m_lyricsCache.value(musicId);
        rebuildLyricLabels();
        return;
    }

    m_lyrics.clear();
    rebuildLyricLabels();

    QNetworkAccessManager *nam = new QNetworkAccessManager(this);
    QString url = QString::fromUtf8("%1/api/music/lyrics/%2")
        .arg(QString::fromUtf8(Theme::kApiBase))
        .arg(musicId);
    QNetworkReply *reply = nam->get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, musicId, lyricsGen]() {
        const auto cleanup = [&]() {
            reply->deleteLater();
            nam->deleteLater();
        };
        if (lyricsGen != m_lyricsFetchGeneration) {
            cleanup();
            return;
        }
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray response = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(response);
            QJsonObject obj = doc.object();
            bool success = obj.value("success").toBool();
            if (success) {
                QString lrc = obj.value("data").toString();
                if (!lrc.isEmpty()) {
                    parseLrc(lrc);
                    rebuildLyricLabels();
                    if (!m_lyrics.isEmpty()) {
                        constexpr int kMax = 64;
                        if (m_lyricsCache.size() >= kMax && !m_lyricsCache.contains(musicId))
                            m_lyricsCache.remove(m_lyricsCache.constBegin().key());
                        m_lyricsCache.insert(musicId, m_lyrics);
                    }
                } else {
                    qDebug() << "歌词API返回空歌词内容，musicId:" << musicId << "，协议:"
                             << httpProtocolLabel(reply);
                }
            } else {
                qDebug() << "歌词API返回失败，musicId:" << musicId << "，协议:" << httpProtocolLabel(reply)
                         << "，message:" << obj.value("message").toString();
            }
        } else {
            qDebug() << "歌词API请求失败，musicId:" << musicId << "，协议:" << httpProtocolLabel(reply)
                     << "，error:" << reply->errorString();
        }
        cleanup();
    });
}

void PlayerPage::applyLyricsRawText(const QString &raw)
{
    QString t = raw;
    if (!t.isEmpty() && t.front() == QChar(0xFEFF))
        t.remove(0, 1);
    parseLrc(t);
    if (m_lyrics.isEmpty() && !t.trimmed().isEmpty()) {
        static const QRegularExpression hasTime(R"(\[\d+:\d)");
        if (!t.contains(hasTime))
            m_lyrics.append({0, t.trimmed(), QString()});
    }
}

void PlayerPage::parseLrc(const QString &lrc)
{
    m_lyrics.clear();
    // 支持 [m:s]、小数 1～5 位（与后端一致）、一行多时间轴；小数按 F/10^L 秒四舍五入为毫秒
    static const QRegularExpression timeRe(R"(\[(\d+):(\d{1,2})(?:\.(\d{1,5}))?\])");
    static const QRegularExpression transRe(R"(^\{["'](.+)["']\}$)");

    auto subsecondToMs = [](const QString &frac) -> int {
        if (frac.isEmpty())
            return 0;
        bool ok = false;
        const int F = frac.toInt(&ok);
        if (!ok || F < 0)
            return 0;
        const int L = frac.length();
        if (L < 1 || L > 5)
            return 0;
        static const qint64 kPow10[] = {1, 10, 100, 1000, 10000, 100000};
        const qint64 denom = kPow10[L];
        return static_cast<int>((qint64{F} * 1000 + denom / 2) / denom);
    };

    const QStringList lines = lrc.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty())
            continue;

        QRegularExpressionMatchIterator tagIt = timeRe.globalMatch(line);
        if (!tagIt.hasNext())
            continue;

        QString plain = line;
        plain.remove(timeRe);
        plain = plain.trimmed();

        QString translation;
        if (i + 1 < lines.size()) {
            const QString next = lines[i + 1].trimmed();
            const auto tMatch = transRe.match(next);
            if (tMatch.hasMatch())
                translation = tMatch.captured(1);
        }

        tagIt = timeRe.globalMatch(line);
        while (tagIt.hasNext()) {
            const QRegularExpressionMatch match = tagIt.next();
            const int min = match.captured(1).toInt();
            const int sec = match.captured(2).toInt();
            const int ms = subsecondToMs(match.captured(3));
            const qint64 timeMs = (static_cast<qint64>(min) * 60 + sec) * 1000 + ms;
            m_lyrics.append({timeMs, plain, translation});
        }
    }

    std::sort(m_lyrics.begin(), m_lyrics.end(),
              [](const LyricLine &a, const LyricLine &b) { return a.time < b.time; });
}

QColor PlayerPage::lyricCountdownDotColor() const
{
    if (m_coverMainColor.isValid())
        return m_coverMainColor;
    return QColor(230, 57, 80);
}

void PlayerPage::updateLyricCountdown(qint64 positionMs)
{
    auto *cd = static_cast<LyricCountdownWidget *>(m_lyricIntroCountdown);
    if (!cd)
        return;
    cd->setDotColor(lyricCountdownDotColor());
    const bool show = cd->shouldShow(positionMs);
    cd->setVisible(show);
    if (show)
        cd->updatePosition(positionMs);
}

void PlayerPage::rebuildLyricLabels()
{
    m_lyricIntroCountdown = nullptr;
    m_lyricLineWidgets.clear();
    QLayoutItem *item;
    while ((item = m_lyricsLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    if (m_lyrics.isEmpty()) {
        auto *noData = new QLabel(QString::fromUtf8("\xe2\x99\xaa"), m_lyricsContainer);
        noData->setAlignment(Qt::AlignCenter);
        noData->setObjectName("noLyricsIcon");
        noData->setStyleSheet(QString::fromUtf8(
            "color: %1; font-size: 56px; background: transparent; margin-top: 60px;"
        ).arg(m_clrLyricDim));
        m_lyricsLayout->addWidget(noData);

        auto *noDataLabel = new QLabel(I18n::instance().tr("noLyrics"), m_lyricsContainer);
        noDataLabel->setAlignment(Qt::AlignCenter);
        noDataLabel->setObjectName("noLyricsText");
        noDataLabel->setStyleSheet(QString::fromUtf8(
            "color: %1; font-size: 14px; background: transparent;"
        ).arg(m_clrLyricDim));
        m_lyricsLayout->addWidget(noDataLabel);
        m_lyricsLayout->addStretch();
        emitDesktopLyricsPayload();
        emitBarLyricUpdate(-1);
        return;
    }

    auto *topPad = new QWidget(m_lyricsContainer);
    topPad->setObjectName(QStringLiteral("lyricPadTop"));
    topPad->setFixedHeight(kLyricTopPad);
    topPad->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_lyricsLayout->addWidget(topPad);

    if (!m_lyrics.isEmpty() && m_lyrics.first().time >= kLyricIntroCountdownMs) {
        auto *countdown = new LyricCountdownWidget(m_lyricsContainer);
        countdown->setObjectName(QStringLiteral("lyricIntroCountdown"));
        countdown->configure(0, m_lyrics.first().time, lyricCountdownDotColor());
        m_lyricsLayout->addWidget(countdown);
        m_lyricIntroCountdown = countdown;
    }

    for (int i = 0; i < m_lyrics.size(); ++i) {
        auto *lineWidget = new QWidget(m_lyricsContainer);
        lineWidget->setObjectName(QString("lyricWidget_%1").arg(i));
        lineWidget->setProperty("lyricIndex", i);
        lineWidget->setAttribute(Qt::WA_TranslucentBackground);
        lineWidget->setAutoFillBackground(false);
        lineWidget->setStyleSheet(QStringLiteral("background: transparent;"));
        lineWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

        auto *outerLay = new QVBoxLayout(lineWidget);
        outerLay->setContentsMargins(0, 0, 0, 0);
        outerLay->setSpacing(0);

        auto *inner = new QWidget(lineWidget);
        inner->setObjectName(QStringLiteral("lyricLineInner"));
        inner->setAttribute(Qt::WA_TranslucentBackground);
        inner->setAutoFillBackground(false);
        inner->setStyleSheet(QStringLiteral("background: transparent;"));

        auto *lineLayout = new QVBoxLayout(inner);
        lineLayout->setContentsMargins(16, 10, 16, 10);
        lineLayout->setSpacing(0);
        outerLay->addWidget(inner);

        auto *textLabel = new QLabel(m_lyrics[i].text, inner);
        textLabel->setObjectName(QStringLiteral("lyricText"));
        textLabel->setProperty("lyricIndex", i);
        configureLyricLabel(textLabel);
        lineLayout->addWidget(textLabel, 0, Qt::AlignTop);

        QLabel *transLabel = nullptr;
        if (!m_lyrics[i].translation.isEmpty()) {
            transLabel = new QLabel(m_lyrics[i].translation, inner);
            transLabel->setObjectName(QStringLiteral("lyricTranslation"));
            transLabel->setProperty("lyricIndex", i);
            configureLyricLabel(transLabel);
            lineLayout->addWidget(transLabel, 0, Qt::AlignTop);
        }
        applyLyricLineStyle(textLabel, transLabel, false);

        m_lyricsLayout->addWidget(lineWidget);
        m_lyricLineWidgets.append(lineWidget);
    }

    auto *botPad = new QWidget(m_lyricsContainer);
    botPad->setObjectName(QStringLiteral("lyricPadBottom"));
    botPad->setFixedHeight(200);
    botPad->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_lyricsLayout->addWidget(botPad);
    emitDesktopLyricsPayload();
    emitBarLyricUpdate(-1);

    QTimer::singleShot(0, this, [this]() {
        updateLyricWrapWidth();
        m_currentLyricLine = -1;
        if (m_engine && !m_lyrics.isEmpty()) {
            const qint64 pos = m_engine->position();
            updateLyricCountdown(pos);
            updateLyricHighlight(pos);
        } else {
            scheduleLyricFocusBlurUpdate();
        }
    });
}

void PlayerPage::emitBarLyricUpdate(int lineIndex)
{
    QString text;
    if (lineIndex >= 0 && lineIndex < m_lyrics.size()) {
        const LyricLine &line = m_lyrics.at(lineIndex);
        text = line.text;
        const QString trans = line.translation.trimmed();
        if (!trans.isEmpty())
            text += QStringLiteral("（ %1 ）").arg(trans);
    }
    emit barLyricLineChanged(text, lineIndex, !m_lyrics.isEmpty());
}

QString PlayerPage::serializeLyricsForDesktop() const
{
    if (m_lyrics.isEmpty())
        return {};

    QString out;
    for (const LyricLine &line : m_lyrics) {
        const qint64 t = line.time;
        const int min = static_cast<int>(t / 60000);
        const int sec = static_cast<int>((t / 1000) % 60);
        const int cs = static_cast<int>((t % 1000) / 10);
        const QString timeTag = QStringLiteral("[%1:%2.%3]")
                                    .arg(min)
                                    .arg(sec, 2, 10, QChar('0'))
                                    .arg(cs, 2, 10, QChar('0'));
        out += timeTag + line.text + QLatin1Char('\n');
        const QString trans = line.translation.trimmed();
        if (!trans.isEmpty())
            out += timeTag + QLatin1Char('=') + trans + QLatin1Char('\n');
    }
    return out;
}

void PlayerPage::emitDesktopLyricsPayload()
{
    emit lyricsPayloadReady(serializeLyricsForDesktop());
}

void PlayerPage::updateLyricHighlight(qint64 positionMs)
{
    updateLyricCountdown(positionMs);

    if (m_lyrics.isEmpty()) {
        if (m_currentLyricLine != -1) {
            m_currentLyricLine = -1;
            emitBarLyricUpdate(-1);
        }
        return;
    }

    int line = -1;
    for (int i = m_lyrics.size() - 1; i >= 0; --i) {
        if (positionMs >= m_lyrics.at(i).time) {
            line = i;
            break;
        }
    }

    if (line == m_currentLyricLine)
        return;
    const int previousLine = m_currentLyricLine;
    m_currentLyricLine = line;
    emitBarLyricUpdate(line);

    syncLyricLinesVisual(line, previousLine);
    QTimer::singleShot(0, this, [this, line]() { scrollLyricsToActiveLine(line); });
}

int PlayerPage::trackDurationSec() const
{
    const qint64 ms = m_engine ? m_engine->duration() : 0;
    if (ms > 0)
        return static_cast<int>(ms / 1000);
    return qMax(1, m_trackDurationSec);
}

void PlayerPage::resetVideoRenderState()
{
    m_videoJobId.clear();
    m_videoJobStatus.clear();
    m_videoJobError.clear();
    m_videoRemainingToday = -1;
    m_videoRenderBusy = false;
    if (m_videoPollTimer)
        m_videoPollTimer->stop();
    updateVideoRenderUi();
}

void PlayerPage::updateVideoRenderUi()
{
    const bool online = m_musicId > 0;
    if (m_videoRenderBtn) {
        m_videoRenderBtn->setVisible(online);
        m_videoRenderBtn->setEnabled(online && !m_videoRenderBusy
                                     && m_videoJobStatus != QStringLiteral("pending")
                                     && m_videoJobStatus != QStringLiteral("processing"));
        if (m_videoRenderBusy)
            m_videoRenderBtn->setText(I18n::instance().tr(QStringLiteral("videoRenderButtonBusy")));
        else
            m_videoRenderBtn->setText(I18n::instance().tr(QStringLiteral("videoRenderButton")));
    }
    if (m_videoDownloadBtn)
        m_videoDownloadBtn->setVisible(online && m_videoJobStatus == QStringLiteral("done"));
    if (!m_videoStatusLbl)
        return;
    if (!online || m_videoJobStatus.isEmpty()) {
        m_videoStatusLbl->hide();
        return;
    }
    QString text;
    if (m_videoJobStatus == QStringLiteral("pending") || m_videoJobStatus == QStringLiteral("processing")) {
        text = I18n::instance().tr(QStringLiteral("videoRenderStatusProcessing"));
        if (m_videoRemainingToday >= 0)
            text += QLatin1Char('\n')
                    + I18n::instance().tr(QStringLiteral("videoRenderRemainingToday"))
                          .arg(m_videoRemainingToday);
    } else if (m_videoJobStatus == QStringLiteral("done")) {
        text = I18n::instance().tr(QStringLiteral("videoRenderStatusDone"));
    } else if (m_videoJobStatus == QStringLiteral("failed")) {
        const QString err = m_videoJobError.isEmpty() ? QStringLiteral("—") : m_videoJobError;
        text = I18n::instance().tr(QStringLiteral("videoRenderFailed")).arg(err);
    }
    if (text.isEmpty()) {
        m_videoStatusLbl->hide();
        return;
    }
    m_videoStatusLbl->setText(text);
    m_videoStatusLbl->show();
}

void PlayerPage::pollVideoRenderStatus()
{
    if (!m_apiClient || m_videoJobId.isEmpty() || !UserManager::instance().isLoggedIn())
        return;
    m_apiClient->fetchVideoRenderStatus(m_videoJobId, [this](bool ok, const QVariantMap &data) {
        if (!ok)
            return;
        m_videoJobStatus = data.value(QStringLiteral("status")).toString();
        m_videoJobError = data.value(QStringLiteral("error")).toString();
        if (m_videoJobStatus == QStringLiteral("done") || m_videoJobStatus == QStringLiteral("failed")) {
            if (m_videoPollTimer)
                m_videoPollTimer->stop();
        }
        updateVideoRenderUi();
    });
}

void PlayerPage::openVideoRenderDialog()
{
    if (m_musicId <= 0) {
        Toast::show(window(), I18n::instance().tr(QStringLiteral("videoRenderLocalUnsupported")), Toast::Info);
        return;
    }
    if (!UserManager::instance().isLoggedIn()) {
        Toast::show(window(), I18n::instance().tr(QStringLiteral("pleaseLoginFirst")), Toast::Error);
        return;
    }
    if (!m_apiClient)
        return;
    if (m_videoJobStatus == QStringLiteral("failed"))
        resetVideoRenderState();

    VideoRenderDialog dlg(m_apiClient, m_musicId, m_fullMetaTitle, m_fullMetaArtist, trackDurationSec(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_videoRenderBusy = true;
    updateVideoRenderUi();
    m_apiClient->createVideoRenderJob(m_musicId, dlg.startSec(), dlg.watermarked(),
                                      [this](bool ok, const QString &message, const QVariantMap &data) {
                                          m_videoRenderBusy = false;
                                          if (!ok) {
                                              Toast::show(window(),
                                                          message.isEmpty()
                                                              ? I18n::instance().tr(QStringLiteral(
                                                                    "videoRenderCreateFailed"))
                                                              : message,
                                                          Toast::Error);
                                              updateVideoRenderUi();
                                              return;
                                          }
                                          m_videoJobId = data.value(QStringLiteral("jobId")).toString();
                                          m_videoJobStatus =
                                              data.value(QStringLiteral("status")).toString();
                                          if (m_videoJobStatus.isEmpty())
                                              m_videoJobStatus = QStringLiteral("pending");
                                          if (data.contains(QStringLiteral("remainingToday")))
                                              m_videoRemainingToday =
                                                  data.value(QStringLiteral("remainingToday")).toInt();
                                          Toast::show(window(),
                                                      I18n::instance().tr(QStringLiteral("videoRenderSubmitted")),
                                                      Toast::Success, 4500);
                                          updateVideoRenderUi();
                                          if (m_videoPollTimer && !m_videoJobId.isEmpty())
                                              m_videoPollTimer->start();
                                          pollVideoRenderStatus();
                                      });
}

void PlayerPage::downloadRenderedVideo()
{
    if (m_videoJobId.isEmpty() || !m_apiClient)
        return;
    QString base = m_fullMetaTitle;
    base.replace(QRegularExpression(QStringLiteral(R"([/\\?%*:|"<>])")), QStringLiteral("_"));
    if (base.isEmpty())
        base = QStringLiteral("clip");
    const QString defaultName = base + QStringLiteral(".mp4");
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString path = QFileDialog::getSaveFileName(
        this, I18n::instance().tr(QStringLiteral("videoRenderDownload")), dir + QLatin1Char('/') + defaultName,
        QStringLiteral("MP4 (*.mp4)"));
    if (path.isEmpty())
        return;

    m_apiClient->downloadVideoRenderFile(m_videoJobId, path, [this, path](bool ok, const QString &err) {
        if (ok) {
            Toast::show(window(), I18n::instance().tr(QStringLiteral("videoRenderDownloadDone")).arg(path),
                        Toast::Success, 5000);
        } else {
            Toast::show(window(),
                        I18n::instance().tr(QStringLiteral("videoRenderFailed"))
                            .arg(err.isEmpty() ? I18n::instance().tr(QStringLiteral("downloadFailed")) : err),
                        Toast::Error);
        }
    });
}
