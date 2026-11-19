#pragma once

#include <QImage>
#include <QRect>
#include <QRectF>

namespace LiquidGlass {

struct Optics {
    float radius;
    float bezel;
    float bend;
    float dispersion;
    float frost;
    float tint[4];
    float shadow;
    float highlight;
};

struct VulkanControlRecipe {
    QRectF track;
    QRectF thumb;
    float trackRadius = 0;
    float thumbRadius = 0;
    float pressure = 0;
    Optics trackMaterial{};
    Optics thumbMaterial{};
};

QImage renderOnVulkan(const QImage &image, const QRect &bounds, const QRectF &source,
                     const QSize &pixels, const Optics &optics);
QImage renderControlOnVulkan(const QImage &image, const QRect &bounds, const QRectF &source,
                            const QSize &pixels, const VulkanControlRecipe &recipe);

} // namespace LiquidGlass
