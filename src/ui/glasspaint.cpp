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

float circleMap(float x)
{
    x = qBound(0.f, x, 1.f);
    return 1.f - std::sqrt(std::max(0.f, 1.f - x * x));
}

static QSize backdropWorkSize(const QSize &target, int maxSide = 900)
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
    QPixmap cover = src.scaled(work, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (cover.width() > work.width() || cover.height() > work.height()) {
        const int x = (cover.width() - work.width()) / 2;
        const int y = (cover.height() - work.height()) / 2;
        cover = cover.copy(x, y, work.width(), work.height());
    } else if (cover.size() != work) {
        cover = cover.scaled(work, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    QGraphicsScene scene;
    QGraphicsPixmapItem item(cover);
    QGraphicsBlurEffect effect;
    effect.setBlurRadius(blurRadius);
    effect.setBlurHints(QGraphicsBlurEffect::QualityHint);
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
        cropped = cropped.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return cropped;
}

} // namespace

namespace GlassPaint {

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

    const QPixmap shot = host->grab(host->rect());

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
        bg.setColorAt(0.0, QColor(24, 24, 24));
        bg.setColorAt(1.0, QColor(20, 20, 20));
    } else {
        bg.setColorAt(0.0, QColor(251, 251, 252));
        bg.setColorAt(1.0, QColor(241, 243, 246));
    }
    p.fillRect(r, bg);
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
    const QColor surface = photoShellBackdrop
        ? (darkMode ? QColor(22, 22, 28, 168) : QColor(255, 255, 255, 188))
        : (darkMode ? QColor(30, 30, 30) : QColor(255, 255, 255));
    const QColor border = darkMode ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 22);
    p.fillRect(r, surface);

    p.setPen(QPen(border, 1));
    if (kind == BarKind::Sidebar)
        p.drawLine(r.topRight(), r.bottomRight());
    else if (kind == BarKind::PlayerBar)
        p.drawLine(r.topLeft(), r.topRight());
    else
        p.drawLine(r.bottomLeft(), r.bottomRight());
}

void applyFlatSurface(GlassWidget *glass, bool darkMode)
{
    if (!glass)
        return;
    glass->setBackdropCaptureEnabled(false);
    if (darkMode) {
        glass->setBaseColor(QColor(36, 36, 36));
        glass->setBorderColor(QColor(255, 255, 255, 18));
    } else {
        glass->setBaseColor(QColor(255, 255, 255));
        glass->setBorderColor(QColor(0, 0, 0, 22));
    }
    glass->setOpacity(1.0);
}

} // namespace GlassPaint
