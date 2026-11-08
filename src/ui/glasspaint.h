#pragma once

#include <QRect>

class QPainter;
class QColor;
class GlassWidget;
class QWidget;
class QPixmap;
class QSize;

#include <QList>

/** 主窗口背景与条带面板（纯 QPainter，无 OpenGL）。 */
namespace GlassPaint {

enum class BarKind {
    TitleBar,
    Sidebar,
    PlayerBar,
};

void paintMainWindowDeepBackdrop(QPainter &p, const QRect &r, bool darkMode);

/** 铺满窗口的图片背景 + 轻遮罩（保证列表/文字可读） */
void paintMainWindowPagesImageBackdrop(QPainter &p, const QRect &r, const QPixmap &image, bool darkMode);

/** 纯色整窗背景 */
void paintMainWindowSolidBackdrop(QPainter &p, const QRect &r, const QColor &color);

void paintBarGlass(QPainter &p, const QRect &r, BarKind kind, bool darkMode,
                   bool photoShellBackdrop = false);

/** 共享壳层缓存采样；不隐藏控件、不在绘制事件中递归抓取窗口。 */
bool paintCachedBackdrop(QPainter &p, QWidget *widget, const QRect &r);

/** 对图片进行降采样高斯柔化，仅在背景或窗口尺寸变化时调用。 */
QPixmap blurredPixmap(const QPixmap &source, const QSize &size, qreal radius = 36.0);

/** 统一的半透明雾面容器。 */
void applyFrostedSurface(GlassWidget *glass, bool darkMode);

/** 抓取 host 背后画面并高斯模糊（用于抽屉遮罩，非纯黑变暗） */
QPixmap grabBlurredBackdrop(QWidget *host, const QList<QWidget *> &excludeWidgets = {},
                            qreal blurRadius = 48.0);

} // namespace GlassPaint
