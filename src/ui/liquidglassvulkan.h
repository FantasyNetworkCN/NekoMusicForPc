#pragma once

#include <QImage>
#include <QRect>

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

QImage renderOnVulkan(const QImage &image, const QRect &bounds, const QRectF &source,
                     const QSize &pixels, const Optics &optics);

} // namespace LiquidGlass
