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

/** SPlayer 式扁平卡片/面板：禁用 backdrop 抓取，实心 surface */
void applyFlatSurface(GlassWidget *glass, bool darkMode);

/** 抓取 host 背后画面并高斯模糊（用于抽屉遮罩，非纯黑变暗） */
QPixmap grabBlurredBackdrop(QWidget *host, const QList<QWidget *> &excludeWidgets = {},
                            qreal blurRadius = 48.0);

} // namespace GlassPaint
