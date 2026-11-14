/**
 * @file desktoplrc.cpp
 * @brief 桌面歌词显示实现
 */

#include "desktoplrc.h"
#include "core/i18n.h"
#include "desktoplrcplatform.h"

#include <QAction>
#include <QColor>
#include <QContextMenuEvent>
#include <QCursor>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>
#include <QToolTip>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>

#if defined(NEKO_HAS_LAYERSHELL_QT)
#include <LayerShellQt/Window>
#endif

namespace {

constexpr int kDefaultDesktopLyricsW = 380;
constexpr int kDefaultDesktopLyricsH = 80;
constexpr int kDesktopLyricsGeomVersion = 7;
constexpr int kMinDesktopLyricsW = 260;
constexpr int kMinDesktopLyricsH = 64;
constexpr int kControlSize = 24;
constexpr int kControlGap = 4;
constexpr int kControlPadding = 5;
constexpr int kControlTop = 6;
constexpr int kSnapDistance = 18;

QColor lyricAccent()
{
    return QColor(255, 198, 92);
}

QString formatClock(qint64 ms)
{
    ms = qMax<qint64>(0, ms);
    const qint64 totalSec = ms / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSec / 60)
        .arg(totalSec % 60, 2, 10, QLatin1Char('0'));
}

bool isReasonableDesktopLyricsSize(int w, int h, const QScreen *screen)
{
    if (w < 200 || h < 40)
        return false;
    if (!screen)
        return w <= 1400 && h <= 400;
    const QRect area = screen->availableGeometry();
    return w <= area.width() * 3 / 4 && h <= area.height() / 3;
}

QSize normalizedDesktopLyricsSize(int w, int h, const QScreen *screen)
{
    if (isReasonableDesktopLyricsSize(w, h, screen))
        return QSize(w, h);
    return QSize(kDefaultDesktopLyricsW, kDefaultDesktopLyricsH);
}

/** 全屏 bug 会把 1920×1080 写入配置；撤销代码后仍会读出，必须校验并迁移 */
void migrateDesktopLyricsGeometry(QSettings &settings)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    const int w = settings.value(QStringLiteral("desktopLyricsW"), kDefaultDesktopLyricsW).toInt();
    const int h = settings.value(QStringLiteral("desktopLyricsH"), kDefaultDesktopLyricsH).toInt();
    const int version = settings.value(QStringLiteral("desktopLyricsGeomVersion"), 0).toInt();

    if (version >= kDesktopLyricsGeomVersion && isReasonableDesktopLyricsSize(w, h, screen))
        return;

    if (!isReasonableDesktopLyricsSize(w, h, screen) || h < kMinDesktopLyricsH) {
        settings.remove(QStringLiteral("desktopLyricsX"));
        settings.remove(QStringLiteral("desktopLyricsY"));
        settings.setValue(QStringLiteral("desktopLyricsW"), kDefaultDesktopLyricsW);
        settings.setValue(QStringLiteral("desktopLyricsH"), kDefaultDesktopLyricsH);
    }
    settings.setValue(QStringLiteral("desktopLyricsGeomVersion"), kDesktopLyricsGeomVersion);
}

QWindow *desktopLrcWindowHandle(QWidget *widget)
{
    if (!widget)
        return nullptr;
    if (QWindow *handle = widget->windowHandle())
        return handle;
    widget->createWinId();
    return widget->windowHandle();
}

} // namespace

DesktopLrc::DesktopLrc(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    // 悬浮歌词：显示时不抢主窗口焦点，更像桌面挂件
    setAttribute(Qt::WA_ShowWithoutActivating, true);

    m_layerShellActive = useLayerShellPath();
#if defined(NEKO_DESKTOP_LRC_WAYLAND_INPUT)
    m_layerShellFullscreen = m_layerShellActive;
#else
    m_layerShellFullscreen = false;
#endif
    desktopLrcConfigureWindow(this, m_layerShellActive);

    setWindowTitle(QStringLiteral("NekoMusic — %1")
                 .arg(I18n::instance().tr(QStringLiteral("desktopLyrics"))));
    setMouseTracking(true);
    resize(kDefaultDesktopLyricsW, kDefaultDesktopLyricsH);

    QSettings settings;
    m_positionLocked = settings.value(QStringLiteral("desktopLyricsLocked"), false).toBool();
    const int fontSize = qBound(9, settings.value(QStringLiteral("desktopLyricsFontSize"), 12).toInt(), 30);
    m_font = QFont(QStringLiteral("Microsoft YaHei"), fontSize, QFont::Bold);

    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &DesktopLrc::updateLyricDisplay);

    m_stayOnTopTimer = new QTimer(this);
    m_stayOnTopTimer->setInterval(350);
    connect(m_stayOnTopTimer, &QTimer::timeout, this, &DesktopLrc::refreshStayOnTop);

    m_hoverAnimation = new QVariantAnimation(this);
    m_hoverAnimation->setDuration(180);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_hoverProgress = value.toReal();
        update();
    });

    m_lineAnimation = new QVariantAnimation(this);
    m_lineAnimation->setDuration(260);
    m_lineAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_lineAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_lineProgress = value.toReal();
        update();
    });

    if (!m_layerShellActive)
        restoreGeometry();
    applyFallbackText();
}

bool DesktopLrc::useLayerShellPath() const
{
#if defined(NEKO_HAS_LAYERSHELL_QT)
    return desktopLrcIsKdePlasmaSession() && desktopLrcIsWaylandSession();
#else
    Q_UNUSED(this);
    return false;
#endif
}

#if defined(NEKO_HAS_LAYERSHELL_QT)
void DesktopLrc::ensureLayerShellConfigured()
{
    if (m_layerShellConfigured)
        return;

    // 必须在首次 show/map 之前绑定 layer-shell，否则会与 xdg_toplevel 冲突导致 Wayland 崩溃
    if (LayerShellQt::Window *layer = LayerShellQt::Window::get(desktopLrcWindowHandle(this))) {
        using LSWindow = LayerShellQt::Window;
        layer->setLayer(LSWindow::LayerOverlay);
        layer->setKeyboardInteractivity(LSWindow::KeyboardInteractivityNone);
        layer->setActivateOnShow(false);
        layer->setCloseOnDismissed(false);
        layer->setScope(QStringLiteral("nekomusic-desktop-lrc"));
        layer->setWantsToBeOnActiveScreen(false);
        if (m_layerShellFullscreen) {
            layer->setAnchors(LSWindow::Anchors(LSWindow::AnchorTop | LSWindow::AnchorBottom
                                                  | LSWindow::AnchorLeft | LSWindow::AnchorRight));
            layer->setMargins(QMargins());
        } else {
            layer->setAnchors(LSWindow::Anchors(LSWindow::AnchorTop | LSWindow::AnchorLeft));
            layer->setDesiredSize(size());
        }
    }

    m_layerShellConfigured = true;
}

void DesktopLrc::applyLayerShellFullscreen(QScreen *screen)
{
    if (!screen)
        return;

    LayerShellQt::Window *layer = LayerShellQt::Window::get(desktopLrcWindowHandle(this));
    if (!layer)
        return;

    layer->setWantsToBeOnActiveScreen(false);
    layer->setScreen(screen);

    if (m_layerShellFullscreen) {
        layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop
                                                        | LayerShellQt::Window::AnchorBottom
                                                        | LayerShellQt::Window::AnchorLeft
                                                        | LayerShellQt::Window::AnchorRight));
        layer->setMargins(QMargins());
        const QRect area = screen->availableGeometry();
        resize(area.size());
        layer->setDesiredSize(size());
        return;
    }

    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop
                                                    | LayerShellQt::Window::AnchorLeft));
    resize(m_panelW, m_panelH);
    layer->setDesiredSize(size());
}

void DesktopLrc::applyLayerShellGeometry()
{
    LayerShellQt::Window *layer = LayerShellQt::Window::get(desktopLrcWindowHandle(this));
    if (!layer)
        return;

    QSettings settings;
    migrateDesktopLyricsGeometry(settings);

    QScreen *screen = QGuiApplication::primaryScreen();
    if (settings.contains(QStringLiteral("desktopLyricsX"))
        && settings.contains(QStringLiteral("desktopLyricsY"))) {
        const int probeW = settings.value(QStringLiteral("desktopLyricsW"), kDefaultDesktopLyricsW).toInt();
        const int probeH = settings.value(QStringLiteral("desktopLyricsH"), kDefaultDesktopLyricsH).toInt();
        const QPoint saved(settings.value(QStringLiteral("desktopLyricsX")).toInt(),
                           settings.value(QStringLiteral("desktopLyricsY")).toInt());
        if (QScreen *savedScreen =
                QGuiApplication::screenAt(saved + QPoint(probeW / 2, probeH / 2)))
            screen = savedScreen;
    }
    if (!screen)
        screen = screenForWidget();

    const QSize sz = normalizedDesktopLyricsSize(
        settings.value(QStringLiteral("desktopLyricsW"), kDefaultDesktopLyricsW).toInt(),
        settings.value(QStringLiteral("desktopLyricsH"), kDefaultDesktopLyricsH).toInt(),
        screen);
    m_panelW = sz.width();
    m_panelH = sz.height();
    if (!screen)
        return;

    applyLayerShellFullscreen(screen);
    syncCachedScreenPosFromSettings(screen);
    m_cachedScreenPos = clampToScreen(m_cachedScreenPos, screen);
    if (m_layerShellFullscreen) {
        updateInputRegion();
    } else {
        setWindowScreenTopLeft(m_cachedScreenPos, screen);
    }
    update();
}

void DesktopLrc::syncCachedScreenPosFromSettings(QScreen *screen)
{
    if (!screen)
        screen = screenForWidget();
    if (!screen)
        return;

    QSettings settings;
    migrateDesktopLyricsGeometry(settings);
    const QRect area = screen->availableGeometry();

    if (settings.contains(QStringLiteral("desktopLyricsX"))
        && settings.contains(QStringLiteral("desktopLyricsY"))) {
        m_cachedScreenPos = QPoint(settings.value(QStringLiteral("desktopLyricsX")).toInt(),
                                   settings.value(QStringLiteral("desktopLyricsY")).toInt());
    } else {
        m_cachedScreenPos = QPoint((area.width() - m_panelW) / 2 + area.left(),
                                   area.bottom() - m_panelH - 48);
    }
}

void DesktopLrc::saveLayerShellGeometry()
{
    QSettings settings;
    settings.setValue(QStringLiteral("desktopLyricsX"), m_cachedScreenPos.x());
    settings.setValue(QStringLiteral("desktopLyricsY"), m_cachedScreenPos.y());
    settings.setValue(QStringLiteral("desktopLyricsW"), m_panelW);
    settings.setValue(QStringLiteral("desktopLyricsH"), m_panelH);
    settings.setValue(QStringLiteral("desktopLyricsGeomVersion"), kDesktopLyricsGeomVersion);
}
#else
void DesktopLrc::ensureLayerShellConfigured() {}
void DesktopLrc::applyLayerShellFullscreen(QScreen *) {}
void DesktopLrc::applyLayerShellGeometry() {}
void DesktopLrc::saveLayerShellGeometry() {}
#endif

void DesktopLrc::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_layerShellActive)
        desktopLrcKeepOnTop(this);
}

void DesktopLrc::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    startHoverAnimation(true);
    QWidget::enterEvent(event);
}

void DesktopLrc::leaveEvent(QEvent *event)
{
    m_hovered = false;
    m_hoveredAction = HoverAction::None;
    m_pressedAction = HoverAction::None;
    if (!m_dragging && !m_resizing && !m_seeking)
        startHoverAnimation(false);
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(event);
}

void DesktopLrc::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (isMinimized() || isMaximized()) {
            // Force block minimization and maximization, restore to normal geometry
            QTimer::singleShot(0, this, [this]() {
                showNormal();
                if (!m_layerShellActive) {
                    desktopLrcKeepOnTop(this);
                }
            });
            event->accept();
            return;
        }
    }
    QWidget::changeEvent(event);
}

void DesktopLrc::mouseDoubleClickEvent(QMouseEvent *event)
{
    const QRect panel = lyricsPanelLocalRect();
    if (event->button() == Qt::LeftButton
        && buttonAt(event->position().toPoint(), panel) == HoverAction::None
        && m_playbackAvailable) {
        emit playPauseRequested();
    }
    // 吞掉事件，阻止窗口管理器把双击解释成最大化。
    event->accept();
}

void DesktopLrc::refreshStayOnTop()
{
    if (!m_isVisible || m_layerShellActive || m_dragging)
        return;
    desktopLrcKeepOnTop(this);
}

QScreen *DesktopLrc::screenForWidget() const
{
    auto *self = const_cast<DesktopLrc *>(this);
    if (QScreen *screen = m_dragScreen)
        return screen;
    if (QWindow *handle = desktopLrcWindowHandle(self)) {
        if (QScreen *screen = handle->screen())
            return screen;
    }
    if (QScreen *screen = QGuiApplication::screenAt(
            m_cachedScreenPos + QPoint(m_panelW / 2, m_panelH / 2)))
        return screen;
    return QGuiApplication::primaryScreen();
}

QPoint DesktopLrc::clampToScreen(const QPoint &topLeft, QScreen *screen) const
{
    if (!screen)
        return topLeft;
    const QRect area = screen->availableGeometry();
    return QPoint(qBound(area.left(), topLeft.x(), area.right() - m_panelW + 1),
                  qBound(area.top(), topLeft.y(), area.bottom() - m_panelH + 1));
}

QPoint DesktopLrc::snapToScreenEdges(const QPoint &topLeft, QScreen *screen) const
{
    if (!screen)
        return topLeft;
    if (QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier))
        return clampToScreen(topLeft, screen);

    const QRect area = screen->availableGeometry();
    QPoint snapped = clampToScreen(topLeft, screen);
    const int centerX = area.left() + (area.width() - m_panelW) / 2;

    if (qAbs(snapped.x() - area.left()) <= kSnapDistance)
        snapped.setX(area.left());
    else if (qAbs(snapped.x() + m_panelW - area.right() - 1) <= kSnapDistance)
        snapped.setX(area.right() - m_panelW + 1);
    else if (qAbs(snapped.x() - centerX) <= kSnapDistance)
        snapped.setX(centerX);

    if (qAbs(snapped.y() - area.top()) <= kSnapDistance)
        snapped.setY(area.top());
    else if (qAbs(snapped.y() + m_panelH - area.bottom() - 1) <= kSnapDistance)
        snapped.setY(area.bottom() - m_panelH + 1);

    return clampToScreen(snapped, screen);
}

void DesktopLrc::setPanelSize(const QSize &size)
{
    QScreen *screen = screenForWidget();
    int maxW = 1200;
    int maxH = 360;
    if (screen) {
        const QRect area = screen->availableGeometry();
        maxW = qMax(kMinDesktopLyricsW, qMin(maxW, area.width() * 3 / 4));
        maxH = qMax(kMinDesktopLyricsH, qMin(maxH, area.height() / 3));
    }

    const QSize bounded(qBound(kMinDesktopLyricsW, size.width(), maxW),
                        qBound(kMinDesktopLyricsH, size.height(), maxH));
    if (bounded == QSize(m_panelW, m_panelH))
        return;

    m_panelW = bounded.width();
    m_panelH = bounded.height();
    if (screen)
        m_cachedScreenPos = clampToScreen(m_cachedScreenPos, screen);

    if (m_layerShellFullscreen) {
        updateInputRegion();
        update();
        return;
    }

    resize(bounded);
#if defined(NEKO_HAS_LAYERSHELL_QT)
    if (m_layerShellActive) {
        if (LayerShellQt::Window *layer = LayerShellQt::Window::get(desktopLrcWindowHandle(this)))
            layer->setDesiredSize(bounded);
    }
#endif
    update();
}

void DesktopLrc::resetLayout()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    setPanelSize(QSize(kDefaultDesktopLyricsW, kDefaultDesktopLyricsH));
    if (screen) {
        const QRect area = screen->availableGeometry();
        setWindowScreenTopLeft(
            QPoint((area.width() - m_panelW) / 2 + area.left(),
                   area.bottom() - m_panelH - 48),
            screen);
    }
    if (m_layerShellActive)
        saveLayerShellGeometry();
    else
        saveGeometry();
}

void DesktopLrc::adjustFontSize(int delta, const QPoint &anchorGlobal)
{
    if (delta == 0)
        return;

    const int oldSize = m_font.pointSize();
    const int newSize = qBound(9, oldSize + delta, 30);
    if (newSize == oldSize)
        return;

    m_font.setPointSize(newSize);
    QSettings().setValue(QStringLiteral("desktopLyricsFontSize"), newSize);

    const int minimumHeight = qMax(kMinDesktopLyricsH, newSize * 3 + 24);
    if (m_panelH < minimumHeight)
        setPanelSize(QSize(qMax(m_panelW, 300), minimumHeight));

    if (m_layerShellActive)
        saveLayerShellGeometry();
    else
        saveGeometry();
    update();
    QToolTip::showText(anchorGlobal,
                       I18n::instance().tr(QStringLiteral("desktopLyricsSizeFmt")).arg(newSize),
                       this);
}

QRect DesktopLrc::lyricsPanelLocalRect() const
{
    if (!m_layerShellFullscreen)
        return rect();

    QScreen *screen = screenForWidget();
    if (!screen)
        return QRect(QPoint(), QSize(m_panelW, m_panelH));
    const QRect area = screen->availableGeometry();
    return QRect(m_cachedScreenPos - area.topLeft(), QSize(m_panelW, m_panelH));
}

void DesktopLrc::updateInputRegion()
{
    if (!m_layerShellActive || !m_layerShellFullscreen)
        return;
    if (QWindow *handle = desktopLrcWindowHandle(this))
        desktopLrcApplyWaylandInputRegion(handle, QRegion(lyricsPanelLocalRect()));
}

void DesktopLrc::startHoverAnimation(bool visible)
{
    if (!m_hoverAnimation)
        return;
    m_hoverAnimation->stop();
    if (!m_isVisible) {
        m_hoverProgress = 0.0;
        update();
        return;
    }
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(visible ? 1.0 : 0.0);
    m_hoverAnimation->start();
}

DesktopLrc::HoverAction DesktopLrc::buttonAt(const QPoint &localPos, const QRect &panel) const
{
    const QVector<HoverAction> actions = {
        HoverAction::Previous,
        HoverAction::PlayPause,
        HoverAction::Next,
        HoverAction::Lock,
        HoverAction::Close
    };
    const int totalWidth =
        actions.size() * kControlSize + (actions.size() - 1) * kControlGap;
    const int left = panel.right() - kControlPadding - totalWidth + 1;
    const int top = panel.top() + kControlTop;

    for (int i = 0; i < actions.size(); ++i) {
        const QRect rect(left + i * (kControlSize + kControlGap), top, kControlSize, kControlSize);
        if (rect.contains(localPos))
            return actions.at(i);
    }
    return HoverAction::None;
}

QString DesktopLrc::hoverActionToolTip(HoverAction action) const
{
    I18n &i18n = I18n::instance();
    switch (action) {
    case HoverAction::Previous:
        return i18n.tr(QStringLiteral("previous"));
    case HoverAction::PlayPause:
        return i18n.tr(m_isPlaying ? QStringLiteral("pause") : QStringLiteral("play"));
    case HoverAction::Next:
        return i18n.tr(QStringLiteral("next"));
    case HoverAction::Lock:
        return i18n.tr(m_positionLocked ? QStringLiteral("desktopLyricsUnlockPosition")
                                       : QStringLiteral("desktopLyricsLockPosition"));
    case HoverAction::Close:
        return i18n.tr(QStringLiteral("close"));
    case HoverAction::None:
        break;
    }
    return QString();
}

void DesktopLrc::updateHoveredAction(const QPoint &localPos)
{
    if (!m_isVisible) {
        m_hoveredAction = HoverAction::None;
        return;
    }
    const HoverAction action = buttonAt(localPos, lyricsPanelLocalRect());
    if (action == m_hoveredAction)
        return;

    m_hoveredAction = action;
    if (action == HoverAction::None) {
        QToolTip::hideText();
    } else {
        const QRect panel = lyricsPanelLocalRect();
        const QVector<HoverAction> actions = {
            HoverAction::Previous,
            HoverAction::PlayPause,
            HoverAction::Next,
            HoverAction::Lock,
            HoverAction::Close
        };
        const int totalWidth =
            actions.size() * kControlSize + (actions.size() - 1) * kControlGap;
        const int left = panel.right() - kControlPadding - totalWidth + 1;
        const int top = panel.top() + kControlTop;
        const int index = actions.indexOf(action);
        const QRect rect(left + index * (kControlSize + kControlGap), top,
                         kControlSize, kControlSize);
        QToolTip::showText(mapToGlobal(rect.center()), hoverActionToolTip(action), this, rect, 2500);
    }
    update();
}

void DesktopLrc::togglePositionLocked()
{
    m_positionLocked = !m_positionLocked;
    QSettings().setValue(QStringLiteral("desktopLyricsLocked"), m_positionLocked);
    QToolTip::showText(QCursor::pos(), hoverActionToolTip(HoverAction::Lock), this);
    update();
}

void DesktopLrc::showContextMenu(const QPoint &globalPos)
{
    I18n &i18n = I18n::instance();
    QMenu menu(this);

    QAction *playPause = menu.addAction(
        i18n.tr(m_isPlaying ? QStringLiteral("pause") : QStringLiteral("play")));
    playPause->setEnabled(m_playbackAvailable);
    connect(playPause, &QAction::triggered, this, &DesktopLrc::playPauseRequested);

    QAction *previous = menu.addAction(i18n.tr(QStringLiteral("previous")));
    previous->setEnabled(m_playbackAvailable);
    connect(previous, &QAction::triggered, this, &DesktopLrc::previousRequested);

    QAction *next = menu.addAction(i18n.tr(QStringLiteral("next")));
    next->setEnabled(m_playbackAvailable);
    connect(next, &QAction::triggered, this, &DesktopLrc::nextRequested);

    menu.addSeparator();
    QAction *larger = menu.addAction(i18n.tr(QStringLiteral("desktopLyricsFontLarger")));
    connect(larger, &QAction::triggered, this, [this]() {
        adjustFontSize(1, QCursor::pos());
    });
    QAction *smaller = menu.addAction(i18n.tr(QStringLiteral("desktopLyricsFontSmaller")));
    connect(smaller, &QAction::triggered, this, [this]() {
        adjustFontSize(-1, QCursor::pos());
    });

    QAction *lock = menu.addAction(
        i18n.tr(m_positionLocked ? QStringLiteral("desktopLyricsUnlockPosition")
                                 : QStringLiteral("desktopLyricsLockPosition")));
    lock->setCheckable(true);
    lock->setChecked(m_positionLocked);
    connect(lock, &QAction::triggered, this, &DesktopLrc::togglePositionLocked);

    QAction *reset = menu.addAction(i18n.tr(QStringLiteral("desktopLyricsResetLayout")));
    connect(reset, &QAction::triggered, this, &DesktopLrc::resetLayout);

    menu.addSeparator();
    QAction *close = menu.addAction(i18n.tr(QStringLiteral("close")));
    connect(close, &QAction::triggered, this, &DesktopLrc::closeRequested);

    menu.exec(globalPos);
}

bool DesktopLrc::cursorInsidePanel() const
{
    return lyricsPanelLocalRect().contains(mapFromGlobal(QCursor::pos()));
}

void DesktopLrc::contextMenuEvent(QContextMenuEvent *event)
{
    if (!lyricsPanelLocalRect().contains(event->pos()))
        return;
    showContextMenu(event->globalPos());
    event->accept();
}

void DesktopLrc::setWindowScreenTopLeft(const QPoint &topLeft, QScreen *screen)
{
    if (!screen)
        screen = screenForWidget();
    if (!screen)
        return;

    m_cachedScreenPos = clampToScreen(topLeft, screen);

#if defined(NEKO_HAS_LAYERSHELL_QT)
    if (m_layerShellActive) {
        if (m_layerShellFullscreen) {
            updateInputRegion();
            update();
            return;
        }

        if (LayerShellQt::Window *layer = LayerShellQt::Window::get(desktopLrcWindowHandle(this))) {
            const QRect area = screen->availableGeometry();
            layer->setWantsToBeOnActiveScreen(false);
            layer->setScreen(screen);
            layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop
                                                           | LayerShellQt::Window::AnchorLeft));
            layer->setMargins(QMargins(m_cachedScreenPos.x() - area.left(),
                                       m_cachedScreenPos.y() - area.top(), 0, 0));
            return;
        }
    }
#endif

    const int left = m_cachedScreenPos.x();
    const int top = m_cachedScreenPos.y();
    move(left, top);
}

DesktopLrc::~DesktopLrc() = default;

void DesktopLrc::restoreGeometry()
{
    QSettings settings;
    migrateDesktopLyricsGeometry(settings);

    QScreen *primary = QGuiApplication::primaryScreen();
    const QSize sz = normalizedDesktopLyricsSize(
        settings.value(QStringLiteral("desktopLyricsW"), kDefaultDesktopLyricsW).toInt(),
        settings.value(QStringLiteral("desktopLyricsH"), kDefaultDesktopLyricsH).toInt(),
        primary);
    m_panelW = sz.width();
    m_panelH = sz.height();
    resize(sz);

    const bool hasPos = settings.contains(QStringLiteral("desktopLyricsX"))
        && settings.contains(QStringLiteral("desktopLyricsY"));

    auto placeDefault = [this]() {
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            const QRect g = screen->availableGeometry();
            setWindowScreenTopLeft(
                QPoint((g.width() - m_panelW) / 2 + g.x(), g.bottom() - m_panelH - 48), screen);
        }
    };

    if (!hasPos) {
        placeDefault();
        return;
    }

    const int x = settings.value(QStringLiteral("desktopLyricsX")).toInt();
    const int y = settings.value(QStringLiteral("desktopLyricsY")).toInt();
    setWindowScreenTopLeft(QPoint(x, y), primary);

    const QRect frame = QRect(m_cachedScreenPos, QSize(m_panelW, m_panelH));
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen && screen->availableGeometry().intersects(frame))
            return;
    }
    placeDefault();
}

void DesktopLrc::saveGeometry()
{
    QScreen *screen = m_dragScreen ? m_dragScreen : screenForWidget();
    if (!screen || !isReasonableDesktopLyricsSize(m_panelW, m_panelH, screen))
        return;

    QSettings settings;
    settings.setValue(QStringLiteral("desktopLyricsX"), m_cachedScreenPos.x());
    settings.setValue(QStringLiteral("desktopLyricsY"), m_cachedScreenPos.y());
    settings.setValue(QStringLiteral("desktopLyricsW"), m_panelW);
    settings.setValue(QStringLiteral("desktopLyricsH"), m_panelH);
    settings.setValue(QStringLiteral("desktopLyricsGeomVersion"), kDesktopLyricsGeomVersion);
}

void DesktopLrc::applyFallbackText(bool animate)
{
    if (!m_currentSongTitle.isEmpty() || !m_currentSongArtist.isEmpty()) {
        const QString title = m_currentSongTitle.isEmpty()
            ? I18n::instance().tr(QStringLiteral("unknown"))
            : m_currentSongTitle;
        const QString artist = m_currentSongArtist.isEmpty()
            ? I18n::instance().tr(QStringLiteral("unknown"))
            : m_currentSongArtist;
        setDisplayedLyric(QStringLiteral("%1 — %2").arg(title, artist),
                          I18n::instance().tr(QStringLiteral("noLyrics")),
                          animate);
    } else {
        setDisplayedLyric(I18n::instance().tr(QStringLiteral("noLyrics")), QString(), animate);
    }
}

void DesktopLrc::loadLrcText(const QString &lrcText)
{
    if (lrcText.trimmed().isEmpty()) {
        m_lyrics.clear();
        applyFallbackText(isVisible());
        return;
    }

    parseLyrics(lrcText);
    if (m_lyrics.isEmpty()) {
        applyFallbackText(isVisible());
    } else {
        refreshCurrentLyric(isVisible());
    }
}

void DesktopLrc::parseLyrics(const QString &lyricsText)
{
    m_lyrics.clear();

    const QStringList lines = lyricsText.split(QLatin1Char('\n'));
    static const QRegularExpression timeRegex(R"(\[(\d+):(\d{1,2})(?:\.(\d{1,5}))?\])");

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

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        QRegularExpressionMatchIterator matches = timeRegex.globalMatch(trimmed);
        if (!matches.hasNext())
            continue;

        QString lyricText = trimmed;
        lyricText.remove(timeRegex);
        lyricText = lyricText.trimmed();
        if (lyricText.isEmpty())
            continue;

        const bool isTranslation = lyricText.startsWith(QLatin1Char('='));
        if (isTranslation)
            lyricText = lyricText.mid(1).trimmed();
        if (lyricText.isEmpty())
            continue;

        matches = timeRegex.globalMatch(trimmed);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            const int minutes = match.captured(1).toInt();
            const int seconds = match.captured(2).toInt();
            const int ms = subsecondToMs(match.captured(3));
            const qint64 timeMs = static_cast<qint64>(minutes) * 60000 + seconds * 1000 + ms;

            if (isTranslation) {
                for (int i = m_lyrics.size() - 1; i >= 0; --i) {
                    if (m_lyrics[i].timeMs == timeMs) {
                        m_lyrics[i].translation = lyricText;
                        break;
                    }
                }
                continue;
            }

            bool merged = false;
            for (ParsedLyricLine &entry : m_lyrics) {
                if (entry.timeMs == timeMs) {
                    entry.text = lyricText;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                ParsedLyricLine entry;
                entry.timeMs = timeMs;
                entry.text = lyricText;
                m_lyrics.append(entry);
            }
        }
    }

    std::sort(m_lyrics.begin(), m_lyrics.end(), [](const ParsedLyricLine &a, const ParsedLyricLine &b) {
        return a.timeMs < b.timeMs;
    });
}

void DesktopLrc::updatePosition(qint64 position)
{
    m_currentPosition = position;
    refreshCurrentLyric(true);
    update();
}

void DesktopLrc::setCurrentSong(const QString &title, const QString &artist)
{
    m_currentSongTitle = title;
    m_currentSongArtist = artist;
    m_currentPosition = 0;
    m_durationMs = 0;
    m_lyrics.clear();
    applyFallbackText(isVisible());
}

void DesktopLrc::setDuration(qint64 durationMs)
{
    m_durationMs = qMax<qint64>(0, durationMs);
    update();
}

void DesktopLrc::setPlaybackState(bool playing)
{
    if (m_isPlaying == playing)
        return;
    m_isPlaying = playing;
    update();
}

void DesktopLrc::setPlaybackAvailable(bool available)
{
    if (m_playbackAvailable == available)
        return;
    m_playbackAvailable = available;
    update();
}

DesktopLrc::ParsedLyricLine DesktopLrc::getLyricEntryAtTime(qint64 timeMs) const
{
    ParsedLyricLine result;
    if (m_lyrics.isEmpty())
        return result;

    for (const ParsedLyricLine &line : m_lyrics) {
        if (line.timeMs <= timeMs)
            result = line;
        else
            break;
    }
    return result;
}

void DesktopLrc::setDisplayedLyric(const QString &text, const QString &translation, bool animate)
{
    if (text == m_currentLyrics && translation == m_currentTranslation)
        return;

    const bool canAnimate = animate && isVisible() && !m_currentLyrics.isEmpty();
    if (canAnimate) {
        m_previousLyrics = m_currentLyrics;
        m_previousTranslation = m_currentTranslation;
        m_lineProgress = 0.0;
        if (m_lineAnimation) {
            m_lineAnimation->stop();
            m_lineAnimation->setStartValue(0.0);
            m_lineAnimation->setEndValue(1.0);
            m_lineAnimation->start();
        }
    } else {
        m_previousLyrics.clear();
        m_previousTranslation.clear();
        m_lineProgress = 1.0;
        if (m_lineAnimation)
            m_lineAnimation->stop();
    }

    m_currentLyrics = text;
    m_currentTranslation = translation;
    update();
}

void DesktopLrc::refreshCurrentLyric(bool animate)
{
    if (m_lyrics.isEmpty()) {
        applyFallbackText(animate);
        return;
    }

    const ParsedLyricLine line = getLyricEntryAtTime(m_currentPosition);
    if (line.text.isEmpty()) {
        applyFallbackText(animate);
        return;
    }
    setDisplayedLyric(line.text, line.translation.trimmed(), animate);
}

void DesktopLrc::updateLyricDisplay()
{
    if (!m_isVisible || !m_updateTimer->isActive())
        return;

    refreshCurrentLyric(true);
}

void DesktopLrc::showWindow()
{
    m_isVisible = true;

    if (m_layerShellActive) {
        ensureLayerShellConfigured();
        applyLayerShellGeometry();
        show();
        m_updateTimer->start(100);
        updateLyricDisplay();
        return;
    }

    restoreGeometry();
    show();
    if (isMinimized() || isMaximized())
        showNormal();
    desktopLrcKeepOnTop(this);
    m_updateTimer->start(100);
    m_stayOnTopTimer->start();
    updateLyricDisplay();
}

void DesktopLrc::hideWindow()
{
    m_isVisible = false;
    m_dragging = false;
    m_resizing = false;
    m_seeking = false;
    m_dragScreen = nullptr;
    m_seekPreviewMs = -1;
    m_hovered = false;
    m_hoverProgress = 0.0;
    m_hoveredAction = HoverAction::None;
    m_pressedAction = HoverAction::None;
    if (m_hoverAnimation)
        m_hoverAnimation->stop();
    QToolTip::hideText();
    m_updateTimer->stop();
    m_stayOnTopTimer->stop();
    setCursor(Qt::ArrowCursor);
    hide();
}

void DesktopLrc::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const QRect panel = m_layerShellFullscreen ? lyricsPanelLocalRect() : rect();
    const qreal hover = m_hoverProgress;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(8, 10, 18, 34 + qRound(12 * hover)));
    painter.drawRoundedRect(QRectF(panel).adjusted(0.0, 2.0, 0.0, 2.0), 18, 18);

    QLinearGradient background(panel.topLeft(), panel.bottomRight());
    background.setColorAt(0.0, QColor(37, 31, 55, 202 + qRound(16 * hover)));
    background.setColorAt(0.55, QColor(24, 24, 38, 195 + qRound(14 * hover)));
    background.setColorAt(1.0, QColor(14, 17, 27, 190 + qRound(12 * hover)));
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(panel).adjusted(1.0, 1.0, -1.0, -1.0), 17, 17);

    QLinearGradient outline(panel.topLeft(), panel.bottomRight());
    outline.setColorAt(0.0, QColor(255, 255, 255, 28 + qRound(30 * hover)));
    outline.setColorAt(0.48, QColor(255, 255, 255, 15 + qRound(16 * hover)));
    outline.setColorAt(1.0, QColor(255, 206, 112, 20 + qRound(42 * hover)));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(outline, 1.0));
    painter.drawRoundedRect(QRectF(panel).adjusted(1.5, 1.5, -1.5, -1.5), 16.5, 16.5);

    const bool hasProgress = m_durationMs > 0;
    QRect progressTrack(panel.left() + 16, panel.bottom() - 8,
                        qMax(1, panel.width() - 32), 3);
    if (hasProgress) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 28 + qRound(22 * hover)));
        painter.drawRoundedRect(progressTrack, 1.5, 1.5);

        const qint64 shownPosition = m_seeking ? m_seekPreviewMs : m_currentPosition;
        const qreal ratio = qBound(0.0, qreal(shownPosition) / qreal(m_durationMs), 1.0);
        QRect fill = progressTrack;
        fill.setWidth(qRound(progressTrack.width() * ratio));
        if (fill.width() > 0) {
            QColor fillColor = lyricAccent();
            fillColor.setAlpha(178 + qRound(60 * hover));
            painter.setBrush(fillColor);
            painter.drawRoundedRect(fill, 1.5, 1.5);
        }
    }

    const int sidePadding = 20;
    const int topInset = 6 + qRound(3 * hover);
    const int bottomInset = hasProgress ? 11 : 6;
    const QRect textRect = panel.adjusted(sidePadding, topInset, -sidePadding, -bottomInset);

    auto drawPair = [&](const QString &mainText,
                        const QString &transText,
                        qreal opacity,
                        int yOffset) {
        if (mainText.isEmpty())
            return;

        painter.save();
        painter.setOpacity(opacity);
        painter.setFont(m_font);
        if (transText.isEmpty()) {
            painter.setPen(lyricAccent());
            painter.drawText(textRect.translated(0, yOffset),
                             Qt::AlignCenter | Qt::TextWordWrap, mainText);
        } else {
            const int splitY = textRect.top() + textRect.height() * 54 / 100;
            const QRect mainRect(textRect.left(), textRect.top() + yOffset,
                                 textRect.width(), qMax(1, splitY - textRect.top() - 2));
            const QRect transRect(textRect.left(), splitY + yOffset,
                                  textRect.width(), qMax(1, textRect.bottom() - splitY + 1));

            painter.setPen(lyricAccent());
            painter.drawText(mainRect, Qt::AlignCenter | Qt::TextWordWrap, mainText);

            painter.setPen(QColor(242, 244, 250, 178));
            painter.setFont(QFont(m_font.family(), qMax(8, m_font.pointSize() - 3),
                                  QFont::Normal));
            painter.drawText(transRect, Qt::AlignCenter | Qt::TextWordWrap, transText);
        }
        painter.restore();
    };

    QString mainText = m_currentLyrics;
    QString transText = m_currentTranslation;
    qreal lineProgress = m_lineProgress;
    if (m_seeking && m_durationMs > 0) {
        const ParsedLyricLine previewLine = getLyricEntryAtTime(m_seekPreviewMs);
        if (!previewLine.text.isEmpty()) {
            mainText = previewLine.text;
            transText = previewLine.translation.trimmed();
            lineProgress = 1.0;
        }
    }

    if (lineProgress < 0.999 && !m_previousLyrics.isEmpty()) {
        drawPair(m_previousLyrics, m_previousTranslation,
                 qBound(0.0, 1.0 - lineProgress, 1.0),
                 -qRound(8 * lineProgress));
    }
    drawPair(mainText, transText,
             lineProgress < 0.999 ? 0.25 + 0.75 * lineProgress : 1.0,
             qRound(7 * (1.0 - lineProgress)));

    if (m_seeking && m_durationMs > 0 && m_seekPreviewMs >= 0) {
        const QString seekText = QStringLiteral("%1 / %2")
                                     .arg(formatClock(m_seekPreviewMs), formatClock(m_durationMs));
        QFont seekFont(m_font.family(), qMax(8, qMin(10, m_font.pointSize() - 3)),
                       QFont::DemiBold);
        painter.setFont(seekFont);
        const QFontMetrics metrics(seekFont);
        const int pillW = metrics.horizontalAdvance(seekText) + 20;
        const QRect pill(panel.center().x() - pillW / 2, panel.top() + 7, pillW, 22);
        painter.setPen(QPen(QColor(255, 255, 255, 34), 1));
        painter.setBrush(QColor(10, 12, 20, 226));
        painter.drawRoundedRect(pill, 11, 11);
        painter.setPen(QColor(255, 226, 166));
        painter.drawText(pill, Qt::AlignCenter, seekText);
    }

    if (hover <= 0.001)
        return;

    const QVector<HoverAction> actions = {
        HoverAction::Previous,
        HoverAction::PlayPause,
        HoverAction::Next,
        HoverAction::Lock,
        HoverAction::Close
    };
    const int totalWidth =
        actions.size() * kControlSize + (actions.size() - 1) * kControlGap;
    const int left = panel.right() - kControlPadding - totalWidth + 1;
    const int top = panel.top() + kControlTop;

    painter.save();
    painter.setOpacity(hover);
    const QRect dock(left - 5, top - 4, totalWidth + 10, kControlSize + 8);
    painter.setPen(QPen(QColor(255, 255, 255, 30), 1));
    painter.setBrush(QColor(9, 11, 19, 210));
    painter.drawRoundedRect(dock, 15, 15);

    for (int i = 0; i < actions.size(); ++i) {
        const HoverAction action = actions.at(i);
        const QRect button(left + i * (kControlSize + kControlGap), top,
                           kControlSize, kControlSize);
        const bool enabled = action == HoverAction::Lock
            || action == HoverAction::Close || m_playbackAvailable;
        const bool active = action == HoverAction::Lock && m_positionLocked;

        if (action == m_pressedAction) {
            painter.setBrush(QColor(255, 255, 255, 54));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(button.adjusted(1, 1, -1, -1));
        } else if (action == m_hoveredAction && enabled) {
            painter.setBrush(QColor(255, 255, 255, 24));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(button.adjusted(1, 1, -1, -1));
        }

        QColor iconColor = enabled ? QColor(242, 244, 250, 225) : QColor(242, 244, 250, 70);
        if (action == HoverAction::PlayPause && enabled)
            iconColor = lyricAccent();
        if (active)
            iconColor = lyricAccent();
        if (action == HoverAction::Close && action == m_hoveredAction)
            iconColor = QColor(255, 132, 142);

        painter.setPen(QPen(iconColor, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QRectF icon = QRectF(button).adjusted(7, 7, -7, -7);
        const qreal cx = icon.center().x();
        const qreal cy = icon.center().y();

        if (action == HoverAction::Previous || action == HoverAction::Next) {
            const bool previous = action == HoverAction::Previous;
            QPainterPath triangle;
            if (previous) {
                triangle.moveTo(icon.right(), icon.top());
                triangle.lineTo(icon.right(), icon.bottom());
                triangle.lineTo(icon.left() + 3, cy);
            } else {
                triangle.moveTo(icon.left(), icon.top());
                triangle.lineTo(icon.left(), icon.bottom());
                triangle.lineTo(icon.right() - 3, cy);
            }
            triangle.closeSubpath();
            painter.setBrush(iconColor);
            painter.setPen(Qt::NoPen);
            painter.drawPath(triangle);
            painter.setPen(QPen(iconColor, 1.8, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(previous ? icon.left() + 1 : icon.right() - 1, icon.top()),
                             QPointF(previous ? icon.left() + 1 : icon.right() - 1, icon.bottom()));
        } else if (action == HoverAction::PlayPause) {
            painter.setBrush(iconColor);
            painter.setPen(Qt::NoPen);
            if (m_isPlaying) {
                painter.drawRoundedRect(QRectF(cx - 3.5, cy - 5, 2.7, 10), 1.2, 1.2);
                painter.drawRoundedRect(QRectF(cx + 0.8, cy - 5, 2.7, 10), 1.2, 1.2);
            } else {
                QPainterPath triangle;
                triangle.moveTo(cx - 3.2, cy - 5.2);
                triangle.lineTo(cx - 3.2, cy + 5.2);
                triangle.lineTo(cx + 5.1, cy);
                triangle.closeSubpath();
                painter.drawPath(triangle);
            }
        } else if (action == HoverAction::Lock) {
            painter.drawRoundedRect(QRectF(cx - 5, cy - 1, 10, 8), 2, 2);
            QPainterPath shackle;
            shackle.moveTo(cx - 3.6, cy - 1);
            shackle.lineTo(cx - 3.6, cy - 4.2);
            shackle.cubicTo(cx - 3.6, cy - 8.2, cx + 3.6, cy - 8.2, cx + 3.6, cy - 4.2);
            shackle.lineTo(cx + 3.6, cy - 1);
            painter.drawPath(shackle);
            painter.setPen(Qt::NoPen);
            painter.setBrush(iconColor);
            painter.drawEllipse(QPointF(cx, cy + 2.7), 1.2, 1.2);
        } else if (action == HoverAction::Close) {
            painter.drawLine(QPointF(cx - 4, cy - 4), QPointF(cx + 4, cy + 4));
            painter.drawLine(QPointF(cx + 4, cy - 4), QPointF(cx - 4, cy + 4));
        }
    }
    painter.restore();
}

void DesktopLrc::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    const QPoint local = event->position().toPoint();
    if (m_layerShellFullscreen && !lyricsPanelLocalRect().contains(local))
        return;

    const QRect panel = lyricsPanelLocalRect();
    const HoverAction action = buttonAt(local, panel);
    if (action != HoverAction::None) {
        const bool enabled = action == HoverAction::Lock
            || action == HoverAction::Close || m_playbackAvailable;
        if (enabled) {
            m_pressedAction = action;
            update();
        }
        event->accept();
        return;
    }

    if (!m_positionLocked) {
        const QRect resizeHandle(panel.right() - 17, panel.bottom() - 17, 17, 17);
        if (resizeHandle.contains(local)) {
            m_resizing = true;
            m_resizeStartGlobal = event->globalPosition().toPoint();
            m_resizeStartSize = QSize(m_panelW, m_panelH);
            setCursor(Qt::SizeFDiagCursor);
            if (m_updateTimer)
                m_updateTimer->stop();
            event->accept();
            return;
        }
    }

    QRect seekArea(panel.left() + 14, panel.bottom() - 13,
                   qMax(1, panel.width() - 28), 13);
    if (m_durationMs > 0 && seekArea.contains(local)) {
        m_seeking = true;
        const qreal ratio = qBound(0.0,
                                   qreal(local.x() - (panel.left() + 16))
                                       / qreal(qMax(1, panel.width() - 32)),
                                   1.0);
        m_seekPreviewMs = qRound64(ratio * m_durationMs);
        setCursor(Qt::PointingHandCursor);
        update();
        event->accept();
        return;
    }

    if (m_positionLocked) {
        event->accept();
        return;
    }

    m_dragging = true;
    m_lastDragGlobal = event->globalPosition().toPoint();
    m_dragScreen = QGuiApplication::screenAt(m_lastDragGlobal);
    if (!m_dragScreen)
        m_dragScreen = screenForWidget();
    setCursor(Qt::ClosedHandCursor);

    if (m_updateTimer)
        m_updateTimer->stop();

    if (!m_layerShellActive && desktopLrcIsWaylandSession()) {
        if (QWindow *handle = window() ? window()->windowHandle() : nullptr)
            handle->startSystemMove();
    }

    event->accept();
}

void DesktopLrc::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint global = event->globalPosition().toPoint();
    const QPoint local = event->position().toPoint();
    const QRect panel = lyricsPanelLocalRect();

    if (m_seeking) {
        const qreal ratio = qBound(0.0,
                                   qreal(local.x() - (panel.left() + 16))
                                       / qreal(qMax(1, panel.width() - 32)),
                                   1.0);
        m_seekPreviewMs = qRound64(ratio * m_durationMs);
        update();
        event->accept();
        return;
    }

    if (m_resizing) {
        const QPoint delta = global - m_resizeStartGlobal;
        setPanelSize(m_resizeStartSize + QSize(delta.x(), delta.y()));
        update();
        event->accept();
        return;
    }

    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint delta = global - m_lastDragGlobal;
        if (delta.isNull())
            return;
        m_lastDragGlobal = global;

#if defined(NEKO_HAS_LAYERSHELL_QT)
        if (m_layerShellActive) {
            setWindowScreenTopLeft(snapToScreenEdges(m_cachedScreenPos + delta, m_dragScreen),
                                   m_dragScreen);
            event->accept();
            return;
        }
#endif

        if (!desktopLrcIsWaylandSession()) {
            setWindowScreenTopLeft(snapToScreenEdges(m_cachedScreenPos + delta, m_dragScreen),
                                   m_dragScreen);
        }
        event->accept();
        return;
    }

    if (!m_hovered) {
        m_hovered = true;
        startHoverAnimation(true);
    }
    updateHoveredAction(local);
    if (m_hoveredAction != HoverAction::None) {
        setCursor(Qt::PointingHandCursor);
    } else if (!m_positionLocked
               && QRect(panel.right() - 17, panel.bottom() - 17, 17, 17).contains(local)) {
        setCursor(Qt::SizeFDiagCursor);
    } else if (m_durationMs > 0
               && QRect(panel.left() + 14, panel.bottom() - 13,
                        qMax(1, panel.width() - 28), 13).contains(local)) {
        setCursor(Qt::PointingHandCursor);
    } else if (!m_positionLocked) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void DesktopLrc::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const QPoint local = event->position().toPoint();
    const QRect panel = lyricsPanelLocalRect();
    const bool hadInteraction = m_pressedAction != HoverAction::None
        || m_dragging || m_resizing || m_seeking;
    if (!hadInteraction) {
        event->accept();
        return;
    }

    if (m_pressedAction != HoverAction::None) {
        const HoverAction action = m_pressedAction;
        m_pressedAction = HoverAction::None;
        if (buttonAt(local, panel) == action) {
            switch (action) {
            case HoverAction::Previous:
                emit previousRequested();
                break;
            case HoverAction::PlayPause:
                emit playPauseRequested();
                break;
            case HoverAction::Next:
                emit nextRequested();
                break;
            case HoverAction::Lock:
                togglePositionLocked();
                break;
            case HoverAction::Close:
                emit closeRequested();
                break;
            case HoverAction::None:
                break;
            }
        }
    }

    if (m_seeking) {
        const qint64 target = m_seekPreviewMs;
        m_seeking = false;
        m_seekPreviewMs = -1;
        if (target >= 0) {
            m_currentPosition = target;
            refreshCurrentLyric(true);
            emit seekRequested(target);
        }
    }

    if (m_resizing) {
        m_resizing = false;
        if (m_layerShellActive)
            saveLayerShellGeometry();
        else
            saveGeometry();
    } else if (m_dragging) {
        m_dragging = false;
        if (!m_layerShellActive && desktopLrcIsWaylandSession()) {
            m_cachedScreenPos = pos();
            m_dragScreen = QGuiApplication::screenAt(frameGeometry().center());
        }
        if (m_layerShellActive)
            saveLayerShellGeometry();
        else
            saveGeometry();
        m_dragScreen = nullptr;
    }

    if (m_updateTimer && m_isVisible)
        m_updateTimer->start(100);
    updateHoveredAction(local);
    if (m_isVisible && cursorInsidePanel())
        startHoverAnimation(true);
    else
        startHoverAnimation(false);
    update();
    event->accept();
}

void DesktopLrc::wheelEvent(QWheelEvent *event)
{
    int steps = event->angleDelta().y() / 120;
    if (steps == 0 && !event->pixelDelta().isNull()) {
        m_fontWheelAccumulator += event->pixelDelta().y();
        constexpr qreal kPixelStep = 28.0;
        steps = int(m_fontWheelAccumulator / kPixelStep);
        m_fontWheelAccumulator -= steps * kPixelStep;
    }
    if (steps != 0)
        adjustFontSize(steps, event->globalPosition().toPoint());
    event->accept();
}
