#include "glasspaint.h"
#include "glasswidget.h"

#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QWidget>
#include <QVariant>
#include <QtMath>

#include <cmath>

namespace {

QPixmap noiseTile()
{
    static QPixmap cache;
    if (!cache.isNull())
        return cache;

    constexpr int n = 96;
    QImage img(n, n, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const qreal t = qSin(qreal(x) * 12.9898 + qreal(y) * 78.233) * 43758.5453;
            int v = int(t) & 255;
            v = (v - 128) / 8;
            const int g = qBound(0, 128 + v, 255);
            img.setPixelColor(x, y, QColor(g, g, g, 18));
        }
    }
    cache = QPixmap::fromImage(img);
    return cache;
}

void drawNoiseOverlay(QPainter &p, const QRect &r, qreal strength = 1.0)
{
    const QPixmap tile = noiseTile();
    if (tile.isNull())
        return;
    p.save();
    p.setOpacity(0.028 * strength);
    p.drawTiledPixmap(r, tile);
    p.restore();
}

static QSize backdropWorkSize(const QSize &target, int maxSide = 640)
{
    QSize w = target;
    if (w.width() < 1)
        w.setWidth(1280);
    if (w.height() < 1)
        w.setHeight(720);
    const int side = qMax(w.width(), w.height());
    if (side > maxSide)
        w.scale(maxSide, maxSide, Qt::KeepAspectRatio);
    return w;
}

static QPixmap blurPixmap(const QPixmap &src, const QSize &target, qreal blurRadius)
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
    if (cropped.size() != target)
        cropped = cropped.scaled(target, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    return cropped;
}

} // namespace

namespace GlassPaint {

QPixmap blurredPixmap(const QPixmap &source, const QSize &size, qreal radius)
{
    return blurPixmap(source, size, radius);
}

bool paintCachedBackdrop(QPainter &p, QWidget *widget, const QRect &r)
{
    if (!widget)
        return false;
    QWidget *host = widget->window();
    const QPixmap backdrop = host->property("nekoFrostedBackdrop").value<QPixmap>();
    if (backdrop.isNull() || host->size().isEmpty())
        return false;

    const QPoint origin = widget->mapTo(host, r.topLeft());
    const qreal sx = qreal(backdrop.width()) / host->width();
    const qreal sy = qreal(backdrop.height()) / host->height();
    const QRectF source(origin.x() * sx, origin.y() * sy, r.width() * sx, r.height() * sy);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawPixmap(QRectF(r), backdrop, source);
    return true;
}

QPixmap grabBlurredBackdrop(QWidget *host, const QList<QWidget *> &excludeWidgets, qreal blurRadius)
{
    if (!host)
        return {};

    QList<QWidget *> saved;
    for (QWidget *w : excludeWidgets) {
        if (!w || !w->isVisible())
            continue;
        saved.append(w);
        w->hide();
    }

    const QSize work = backdropWorkSize(host->size());
    QPixmap shot(work);
    shot.fill(Qt::transparent);
    {
        QPainter p(&shot);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.scale(qreal(work.width()) / qMax(1, host->width()),
                qreal(work.height()) / qMax(1, host->height()));
        host->render(&p);
    }

    for (QWidget *w : saved)
        w->show();

    if (shot.isNull())
        return {};
    return blurPixmap(shot, host->size(), blurRadius);
}

void paintMainWindowDeepBackdrop(QPainter &p, const QRect &r, bool darkMode)
{
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient bg(r.topLeft(), r.bottomLeft());
    if (darkMode) {
        bg.setColorAt(0.0, QColor(15, 23, 41));
        bg.setColorAt(1.0, QColor(9, 15, 28));
    } else {
        bg.setColorAt(0.0, QColor(237, 246, 253));
        bg.setColorAt(1.0, QColor(229, 237, 249));
    }
    p.fillRect(r, bg);

    // 静态环境光，不启动常驻动画，也不影响列表滚动。
    QRadialGradient cyan(QPointF(r.left() + r.width() * .16, r.top() + r.height() * .08),
                         r.width() * .7);
    cyan.setColorAt(0, darkMode ? QColor(36, 136, 163, 42) : QColor(124, 221, 241, 68));
    cyan.setColorAt(1, Qt::transparent);
    p.fillRect(r, cyan);
    QRadialGradient indigo(QPointF(r.right(), r.top() + r.height() * .7), r.width() * .65);
    indigo.setColorAt(0, darkMode ? QColor(94, 99, 187, 32) : QColor(160, 170, 239, 48));
    indigo.setColorAt(1, Qt::transparent);
    p.fillRect(r, indigo);
}

void paintMainWindowPagesImageBackdrop(QPainter &p, const QRect &r, const QPixmap &image, bool darkMode)
{
    if (!image.isNull()) {
        if (image.size() == r.size())
            p.drawPixmap(r.topLeft(), image);
        else
            p.drawPixmap(r, image);
    } else {
        paintMainWindowDeepBackdrop(p, r, darkMode);
        return;
    }
    if (darkMode)
        p.fillRect(r, QColor(0, 0, 0, 96));
    else
        p.fillRect(r, QColor(255, 255, 255, 72));
}

void paintMainWindowSolidBackdrop(QPainter &p, const QRect &r, const QColor &color)
{
    p.fillRect(r, color.isValid() ? color : QColor(24, 24, 24));
}

void paintBarGlass(QPainter &p, const QRect &r, BarKind kind, bool darkMode, bool photoShellBackdrop)
{
    Q_UNUSED(kind);
    Q_UNUSED(photoShellBackdrop);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(r).adjusted(.5, .5, -.5, -.5), 20, 20);
    p.setClipPath(shape);
    paintCachedBackdrop(p, dynamic_cast<QWidget *>(p.device()), r);
    QLinearGradient surface(r.topLeft(), r.bottomRight());
    surface.setColorAt(0, darkMode ? QColor(25, 38, 58, 170) : QColor(255, 255, 255, 175));
    surface.setColorAt(1, darkMode ? QColor(13, 23, 40, 195) : QColor(247, 251, 255, 125));
    p.fillPath(shape, surface);
    drawNoiseOverlay(p, r);
    p.setClipping(false);
    p.setPen(darkMode ? QColor(196, 223, 255, 28) : QColor(255, 255, 255, 215));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);
    p.restore();
}

void applyFrostedSurface(GlassWidget *glass, bool darkMode)
{
    if (!glass)
        return;
    glass->setBackdropCaptureEnabled(true);
    if (darkMode) {
        glass->setBaseColor(QColor(25, 38, 58, 170));
        glass->setBorderColor(QColor(196, 223, 255, 28));
    } else {
        glass->setBaseColor(QColor(255, 255, 255, 175));
        glass->setBorderColor(QColor(255, 255, 255, 215));
    }
    glass->setOpacity(1.0);
}

} // namespace GlassPaint
