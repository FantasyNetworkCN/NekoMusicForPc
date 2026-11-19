#include "liquidglass.h"
#include "liquidglassvulkan.h"
#include "core/appearancesettings.h"
#include "theme/theme.h"
#include "theme/thememanager.h"

#include <QPainter>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QListView>
#include <QOpenGLWidget>
#include <QPixmap>
#include <QRegion>
#include <QPointer>
#include <QShowEvent>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace LiquidGlass {
namespace {

// 背景和输出保持设备像素比例，独立捕获区域不需要整窗重采样。
constexpr int kMaxTextureSide = 4096;

class GlassComboPopupView final : public QListView
{
public:
    explicit GlassComboPopupView(QComboBox *combo)
        : QListView(combo), m_combo(combo)
    {
        setFrameShape(QFrame::NoFrame);
        setAutoFillBackground(false);
        setAttribute(Qt::WA_TranslucentBackground);
        viewport()->setAttribute(Qt::WA_TranslucentBackground);
        connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged,
                this, [this]() { applyTheme(); });
        connect(&AppearanceSettings::instance(),
                &AppearanceSettings::liquidGlassProgressChanged, this,
                [this](qreal) {
                    applyTheme();
                    viewport()->update();
                });
        applyTheme();
    }

protected:
    QSize sizeHint() const override
    {
        QSize hint = QListView::sizeHint();
        int desiredWidth = m_combo ? m_combo->width() : hint.width();
        if (model()) {
            for (int row = 0; row < model()->rowCount(); ++row) {
                const QString text = model()->data(model()->index(row, 0)).toString();
                desiredWidth = qMax(desiredWidth, fontMetrics().horizontalAdvance(text) + 42);
            }
        }
        hint.setWidth(qMax(desiredWidth, 120));
        return hint;
    }

    void showEvent(QShowEvent *event) override
    {
        QListView::showEvent(event);
        const int desiredWidth = sizeHint().width();
        setMinimumWidth(desiredWidth);
        if (width() < desiredWidth)
            resize(desiredWidth, height());
    }

    void paintEvent(QPaintEvent *event) override
    {
        const qreal progress = AppearanceSettings::instance().liquidGlassProgress();
        QWidget *host = m_combo ? m_combo->window() : nullptr;
        if (progress > 0) {
            QPixmap source = host
                ? host->property("nekoLiquidBackdrop").value<QPixmap>() : QPixmap();
            if (source.isNull())
                source = host
                    ? host->property("nekoFrostedBackdrop").value<QPixmap>() : QPixmap();
            if (source.isNull()) {
                for (QWidget *candidate : QApplication::topLevelWidgets()) {
                    source = candidate->property("nekoLiquidBackdrop").value<QPixmap>();
                    if (source.isNull())
                        source = candidate->property("nekoFrostedBackdrop").value<QPixmap>();
                    if (!source.isNull()) {
                        host = candidate;
                        break;
                    }
                }
            }
            QPainter painter(viewport());
            painter.setRenderHint(QPainter::Antialiasing);
            painter.fillRect(viewport()->rect(),
                             Theme::ThemeManager::instance().isDarkMode()
                                 ? QColor(25, 36, 55, 244)
                                 : QColor(250, 252, 255, 246));
            if (host && !source.isNull()) {
                Backdrop backdrop;
                const QPoint origin = host->mapFromGlobal(viewport()->mapToGlobal(QPoint()));
                backdrop.setImage(source.toImage(), host->rect());
                const bool dark = Theme::ThemeManager::instance().isDarkMode();
                painter.save();
                painter.setOpacity(progress);
                m_surface.paint(painter, viewport()->rect(), backdrop,
                                QRectF(origin, viewport()->size()), 14, dark);
                painter.restore();
            }
        }
        QListView::paintEvent(event);
    }

private:
    void applyTheme()
    {
        const bool dark = Theme::ThemeManager::instance().isDarkMode();
        const bool glass = AppearanceSettings::instance().liquidGlassProgress() > 0;
        if (m_themeReady && m_lastDark == dark && m_lastGlass == glass)
            return;
        m_themeReady = true;
        m_lastDark = dark;
        m_lastGlass = glass;
        const QString text = dark ? QStringLiteral("#EAF0FA") : QStringLiteral("#182B45");
        const QString selection = dark ? QStringLiteral("rgba(90, 186, 225, 100)")
                                       : QStringLiteral("rgba(22, 119, 184, 55)");
        const QString background = dark ? QStringLiteral("#19243A")
                                        : QStringLiteral("#F7FAFF");
        const QString border = dark ? QStringLiteral("rgba(190, 215, 248, 55)")
                                    : QStringLiteral("rgba(66, 105, 148, 55)");
        setStyleSheet(QStringLiteral(
            "QListView { background: %3; border: 1px solid %4; border-radius: 14px; "
            "padding: 6px; color: %1; }"
            "QListView::item { padding: 7px 10px; border-radius: 6px; min-height: 22px; }"
            "QListView::item:selected { background: %2; color: %1; }"
            "QListView::item:hover { background: %2; }")
                .arg(text, selection,
                     glass ? QStringLiteral("transparent") : background,
                     glass ? QStringLiteral("transparent") : border));
    }

    QPointer<QComboBox> m_combo;
    Surface m_surface;
    bool m_themeReady = false;
    bool m_lastDark = false;
    bool m_lastGlass = false;
};

QRectF textureRect(const QRect &source, const QRect &bounds, const QSize &imageSize)
{
    const qreal sx = qreal(imageSize.width()) / bounds.width();
    const qreal sy = qreal(imageSize.height()) / bounds.height();
    return {(source.x() - bounds.x()) * sx, (source.y() - bounds.y()) * sy,
            source.width() * sx, source.height() * sy};
}

Optics opticsFor(qreal radius, bool dark)
{
    return {float(radius), 22.0f, 19.0f, 2.15f, 0.42f,
            {dark ? 0.05f : 0.96f, dark ? 0.09f : 0.98f, dark ? 0.15f : 1.0f,
             dark ? 0.38f : 0.46f},
            0.17f, dark ? 0.62f : 0.82f};
}

QRgb sampleRgb(const QImage &image, float x, float y)
{
    x = std::clamp(x, 0.0f, float(image.width() - 1));
    y = std::clamp(y, 0.0f, float(image.height() - 1));
    const int x0 = int(x), y0 = int(y);
    const int x1 = qMin(x0 + 1, image.width() - 1), y1 = qMin(y0 + 1, image.height() - 1);
    const float fx = x - x0, fy = y - y0;
    const auto pixel = [&](int column, int row) {
        const auto *rgba = image.constScanLine(row) + column * 4;
        return qRgb(rgba[0], rgba[1], rgba[2]);
    };
    const QRgb a = pixel(x0, y0), b = pixel(x1, y0), c = pixel(x0, y1), d = pixel(x1, y1);
    const auto channel = [&](int shift) {
        const auto value = [shift](QRgb p) { return float((p >> shift) & 255); };
        return qRound((value(a) * (1 - fx) + value(b) * fx) * (1 - fy)
                      + (value(c) * (1 - fx) + value(d) * fx) * fy);
    };
    return qRgb(channel(16), channel(8), channel(0));
}

QImage renderFallback(const QImage &image, const QRect &bounds, const QRectF &source,
                      const QSize &size, const Optics &optics)
{
    QImage result(size, QImage::Format_ARGB32_Premultiplied);
    if (result.isNull())
        return {};
    const float sx = float(image.width()) / bounds.width();
    const float sy = float(image.height()) / bounds.height();
    const float stepX = float(source.width()) / size.width();
    const float stepY = float(source.height()) / size.height();
    const float halfWidth = float(source.width() / 2), halfHeight = float(source.height() / 2);
    const float radius = qMin(optics.radius, qMin(halfWidth, halfHeight));
    const float bevel = qMax(0.5f, qMin(optics.bezel, qMin(halfWidth, halfHeight)));
    const float alphaWidth = qMax(stepX, stepY);
    const float tint = optics.tint[3];
    for (int y = 0; y < size.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
        const float posY = (y + 0.5f) * stepY;
        for (int x = 0; x < size.width(); ++x) {
            const float posX = (x + 0.5f) * stepX;
            const float qx = std::abs(posX - halfWidth) - halfWidth + radius;
            const float qy = std::abs(posY - halfHeight) - halfHeight + radius;
            const float ax = qMax(0.0f, qx), ay = qMax(0.0f, qy);
            const float length = std::sqrt(ax * ax + ay * ay);
            const float distance = length + qMin(0.0f, qMax(qx, qy)) - radius;
            const float coverage = std::clamp(0.5f - distance / alphaWidth, 0.0f, 1.0f);
            if (coverage <= 0.0f) {
                row[x] = 0;
                continue;
            }
            const float nx = std::copysign(length > 0 ? ax / length : (qx > qy ? 1.0f : 0.0f), posX - halfWidth);
            const float ny = std::copysign(length > 0 ? ay / length : (qy >= qx ? 1.0f : 0.0f), posY - halfHeight);
            const float h = std::clamp(-distance / bevel, 0.0f, 1.0f);
            const float bend = optics.bend * (1.0f - std::sqrt(h * (2.0f - h)));
            const QRgb sampled = sampleRgb(image, (float(source.x() - bounds.x()) + posX - nx * bend) * sx - 0.5f,
                                           (float(source.y() - bounds.y()) + posY - ny * bend) * sy - 0.5f);
            const float light = -0.58f * nx - 0.81f * ny;
            const float edge = std::clamp(1.0f + distance * 0.43f, 0.0f, 1.0f);
            const float gleam = edge * optics.highlight * (0.10f + 0.55f * qMax(0.0f, light));
            const auto shade = [&](float sample, float base) {
                const float color = sample * (1 - tint) + base * 255 * tint;
                return qRound(color * (1 - gleam) + 255 * gleam);
            };
            row[x] = qPremultiply(qRgba(shade(qRed(sampled), optics.tint[0]),
                                        shade(qGreen(sampled), optics.tint[1]),
                                        shade(qBlue(sampled), optics.tint[2]), qRound(coverage * 255)));
        }
    }
    return result;
}

} // namespace

void Backdrop::setImage(const QImage &image, const QRect &bounds)
{
    m_bounds = bounds;
    m_image = bounds.isEmpty() ? QImage() : image.convertToFormat(QImage::Format_RGBA8888);
    m_image.setDevicePixelRatio(1.0);
}

bool Surface::paint(QPainter &painter, const QRectF &target, const Backdrop &backdrop,
                    const QRectF &sourceArea, qreal radius, bool dark)
{
    if (backdrop.isEmpty() || target.isEmpty() || sourceArea.isEmpty()
        || !QRectF(backdrop.m_bounds).intersects(sourceArea))
        return false;

    const qreal dpr = painter.device()->devicePixelRatioF();
    const qreal scale = qMin(dpr, qreal(kMaxTextureSide) / qMax(target.width(), target.height()));
    const qint64 key = backdrop.m_image.cacheKey();
    if (m_image.isNull() || m_sourceKey != key || m_backdropBounds != backdrop.m_bounds
        || m_sourceArea != sourceArea || m_size != target.size() || m_radius != radius
        || m_scale != scale || m_dark != dark) {
        const QSize pixels(qMax(1, qRound(target.width() * scale)),
                           qMax(1, qRound(target.height() * scale)));
        const Optics optics = opticsFor(radius, dark);
        m_image = renderOnVulkan(backdrop.m_image, backdrop.m_bounds, sourceArea, pixels, optics);
        if (m_image.isNull())
            m_image = renderFallback(backdrop.m_image, backdrop.m_bounds, sourceArea, pixels, optics);
        m_sourceKey = key;
        m_backdropBounds = backdrop.m_bounds;
        m_sourceArea = sourceArea;
        m_size = target.size();
        m_radius = radius;
        m_scale = scale;
        m_dark = dark;
    }
    if (m_image.isNull())
        return false;
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, m_image);
    painter.restore();
    return true;
}

bool ControlSurface::paint(QPainter &painter, const QRectF &target, const Backdrop &backdrop,
                           const QRectF &sourceArea, const ControlRecipe &recipe)
{
    if (backdrop.isEmpty() || target.isEmpty() || sourceArea.isEmpty()
        || !QRectF(backdrop.m_bounds).intersects(sourceArea))
        return false;

    const qreal dpr = painter.device()->devicePixelRatioF();
    const qreal scale = qMin(dpr, qreal(kMaxTextureSide) / qMax(target.width(), target.height()));
    const QSize pixels(qMax(1, qRound(target.width() * scale)),
                       qMax(1, qRound(target.height() * scale)));
    const qint64 key = backdrop.m_image.cacheKey();
    const bool recipeChanged = m_recipe.track != recipe.track || m_recipe.thumb != recipe.thumb
        || m_recipe.trackRadius != recipe.trackRadius || m_recipe.thumbRadius != recipe.thumbRadius
        || m_recipe.pressure != recipe.pressure || m_recipe.dark != recipe.dark;
    if (m_image.isNull() || m_sourceKey != key || m_backdropBounds != backdrop.m_bounds
        || m_sourceArea != sourceArea || m_size != target.size() || m_pixelSize != pixels
        || recipeChanged || m_recipe.checked != recipe.checked) {
        const qreal sx = pixels.width() / target.width();
        const qreal sy = pixels.height() / target.height();
        const auto scaledRect = [sx, sy](const QRectF &rect) {
            return QRectF(rect.x() * sx, rect.y() * sy, rect.width() * sx, rect.height() * sy);
        };
        LiquidGlass::VulkanControlRecipe vulkanRecipe;
        vulkanRecipe.track = scaledRect(recipe.track);
        vulkanRecipe.thumb = scaledRect(recipe.thumb);
        vulkanRecipe.trackRadius = float(recipe.trackRadius * qMin(sx, sy));
        vulkanRecipe.thumbRadius = float(recipe.thumbRadius * qMin(sx, sy));
        vulkanRecipe.pressure = float(recipe.pressure);
        vulkanRecipe.trackMaterial = opticsFor(recipe.trackRadius, recipe.dark);
        vulkanRecipe.thumbMaterial = opticsFor(recipe.thumbRadius, recipe.dark);
        const QColor accent(Theme::kLavender);
        vulkanRecipe.trackMaterial.tint[0] = float(accent.redF());
        vulkanRecipe.trackMaterial.tint[1] = float(accent.greenF());
        vulkanRecipe.trackMaterial.tint[2] = float(accent.blueF());
        vulkanRecipe.trackMaterial.tint[3] = recipe.checked ? 0.24f : 0.09f;

        m_image = renderControlOnVulkan(backdrop.m_image, backdrop.m_bounds,
                                        sourceArea, pixels, vulkanRecipe);
        m_sourceKey = key;
        m_backdropBounds = backdrop.m_bounds;
        m_sourceArea = sourceArea;
        m_size = target.size();
        m_pixelSize = pixels;
        m_recipe = recipe;
    }
    if (m_image.isNull()) {
        const qreal radius = qMin(recipe.trackRadius, target.height() / 2);
        m_fallbackTrack.paint(painter, QRectF(target.topLeft() + recipe.track.topLeft(),
                                               recipe.track.size()),
                              backdrop, sourceArea.translated(recipe.track.topLeft()),
                              radius, recipe.dark);
        QColor accent(Theme::kLavender);
        accent.setAlphaF(recipe.checked ? 0.18 : 0.04);
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawRoundedRect(QRectF(target.topLeft() + recipe.track.topLeft(),
                                       recipe.track.size()), radius, radius);
        painter.restore();
        m_fallbackThumb.paint(painter, QRectF(target.topLeft() + recipe.thumb.topLeft(),
                                               recipe.thumb.size()),
                              backdrop, sourceArea.translated(recipe.thumb.topLeft()),
                              recipe.thumbRadius, recipe.dark);
        return true;
    }
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, m_image);
    painter.restore();
    return true;
}

QImage capture(QWidget *host, const QRect &region, const QWidget *stopBefore)
{
    if (!host)
        return {};
    const QRect area = region.intersected(host->rect());
    if (area.isEmpty())
        return {};
    const qreal scale = qMin<qreal>(host->devicePixelRatioF(),
        qreal(kMaxTextureSide) / qMax(area.width(), area.height()));
    QImage image(QSize(qMax(1, qRound(area.width() * scale)),
                      qMax(1, qRound(area.height() * scale))), QImage::Format_RGB32);
    if (image.isNull())
        return {};
    image.fill(host->palette().color(QPalette::Window));
    QPainter painter(&image);
    painter.scale(qreal(image.width()) / area.width(), qreal(image.height()) / area.height());
    constexpr auto background = QWidget::DrawWindowBackground;
    const auto glLayers = host->findChildren<QOpenGLWidget *>(QString(), Qt::FindDirectChildrenOnly);
    if (!stopBefore && glLayers.isEmpty()) {
        host->render(&painter, QPoint(), QRegion(area), background | QWidget::DrawChildren);
    } else {
        host->render(&painter, QPoint(), QRegion(area), background);
        // QWidget 子对象按堆叠顺序排列；单独渲染下层，无需隐藏浮层。
        for (QObject *object : host->children()) {
            if (object == stopBefore)
                break;
            auto *child = qobject_cast<QWidget *>(object);
            if (!child || child->isWindow() || !child->isVisible())
                continue;
            const QRect overlap = child->geometry().intersected(area);
            if (overlap.isEmpty())
                continue;
            const QRect localArea = overlap.translated(-child->pos());
            const QPoint target = overlap.topLeft() - area.topLeft();
            if (auto *gl = qobject_cast<QOpenGLWidget *>(child)) {
                if (gl->isValid()) {
                    const QImage frame = gl->grabFramebuffer();
                    if (!frame.isNull()) {
                        painter.drawImage(QRect(target, overlap.size()), frame,
                                          textureRect(localArea, gl->rect(), frame.size()));
                    }
                }
            } else {
                child->render(&painter, target, QRegion(localArea), QWidget::DrawChildren);
            }
        }
    }
    return image;
}

const Backdrop &shellBackdrop(const QWidget *widget)
{
    static Backdrop backdrop;
    static qint64 key = 0;
    static QSize size;
    const QWidget *host = widget->window();
    const QPixmap source = host->property("nekoLiquidBackdrop").value<QPixmap>();
    if (source.cacheKey() != key || host->size() != size) {
        key = source.cacheKey();
        size = host->size();
        backdrop.setImage(source.toImage(), host->rect());
    }
    return backdrop;
}

void installComboBox(QComboBox *combo)
{
    if (!combo || combo->property("nekoLiquidComboInstalled").toBool())
        return;
    combo->setProperty("nekoLiquidComboInstalled", true);
    combo->setView(new GlassComboPopupView(combo));
}

} // namespace LiquidGlass
