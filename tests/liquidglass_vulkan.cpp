#include "ui/liquidglassvulkan.h"

#include <QCoreApplication>
#include <QDir>
#include <QPainter>
#include <QDebug>

int main(int argc, char **argv)
{
    const QCoreApplication app(argc, argv);
    QImage backdrop(420, 248, QImage::Format_RGBA8888);
    backdrop.fill(QColor("#181c2b"));
    {
        QPainter painter(&backdrop);
        for (int x = 0; x < backdrop.width(); x += 10)
            painter.fillRect(x, 0, 3, backdrop.height(), QColor("#dbe6f5"));
        for (int y = 0; y < backdrop.height(); y += 20)
            painter.fillRect(0, y, backdrop.width(), 1, QColor("#6188c0"));
    }

    constexpr LiquidGlass::Optics material{
        22, 26, 26, 2.15f, 0.42f, {0.05f, 0.09f, 0.15f, 0.38f}, 0.17f, 0.62f
    };
    const QRect region(74, 48, 272, 152);
    const auto render = [&](const LiquidGlass::Optics &optics) {
        return LiquidGlass::renderOnVulkan(backdrop, backdrop.rect(), region, region.size(), optics);
    };
    const QImage glass = render(material);
    if (glass.isNull()) {
        qWarning() << "Vulkan Liquid Glass is unavailable";
        return qEnvironmentVariableIsSet("NEKO_TEST_REQUIRE_VULKAN") ? 1 : 77;
    }

    auto flatMaterial = material;
    flatMaterial.bend = flatMaterial.dispersion = 0;
    const QImage flat = render(flatMaterial);
    if (flat.isNull() || glass.pixelColor(0, 0).alpha() != 0
        || glass.pixelColor(128, 45).alpha() != 255)
        return 2;
    if (glass.pixelColor(127, 45).red() - glass.pixelColor(130, 45).red() < 70
        || glass.pixelColor(127, 45) != flat.pixelColor(127, 45)) {
        qCritical() << "Fine backdrop detail did not survive the glass center";
        return 3;
    }
    int displaced = 0;
    for (int x = 3; x < 18; ++x) {
        if (qAbs(glass.pixelColor(x, 45).red() - flat.pixelColor(x, 45).red()) > 35)
            ++displaced;
    }
    if (displaced < 4) {
        qCritical() << "Vulkan did not deflect the high-contrast lines at the rim";
        return 4;
    }

    LiquidGlass::VulkanControlRecipe control;
    control.track = QRectF(0, 0, 272, 80);
    control.thumb = QRectF(8, 8, 64, 64);
    control.trackRadius = 40;
    control.thumbRadius = 32;
    control.trackMaterial = {40, 6, 3.1f, 0.50f, 0.24f,
                             {0.35f, 0.55f, 0.82f, 0.14f}, 0.20f, 0.78f};
    control.thumbMaterial = {32, 5.5f, 4.8f, 0.85f, 0.18f,
                             {0.78f, 0.92f, 1.0f, 0.58f}, 0.17f, 0.92f};
    const auto controlImage = LiquidGlass::renderControlOnVulkan(
        backdrop, backdrop.rect(), region, QSize(272, 80), control);
    if (controlImage.isNull() || controlImage.pixelColor(0, 0).alpha() != 0
        || controlImage.pixelColor(136, 40).alpha() < 240
        || controlImage.pixelColor(24, 40).alpha() < 240)
        return 6;
    control.pressure = 1.0f;
    control.thumb = QRectF(6, 11, 68, 58);
    const auto pressedControl = LiquidGlass::renderControlOnVulkan(
        backdrop, backdrop.rect(), region, QSize(272, 80), control);
    if (pressedControl.isNull() || pressedControl == controlImage)
        return 7;

    const QString artifacts = qEnvironmentVariable("NEKO_UI_ARTIFACT_DIR");
    if (!artifacts.isEmpty()) {
        QDir().mkpath(artifacts);
        QImage comparison(backdrop.width() * 2, backdrop.height(), QImage::Format_RGBA8888);
        QPainter painter(&comparison);
        painter.drawImage(0, 0, backdrop);
        painter.drawImage(backdrop.width(), 0, backdrop);
        painter.drawImage(region.topLeft(), flat);
        painter.drawImage(region.topLeft() + QPoint(backdrop.width(), 0), glass);
        painter.end();
        if (!comparison.save(QDir(artifacts).filePath(QStringLiteral("vulkan-refraction-comparison.png"))))
            return 5;
        controlImage.save(QDir(artifacts).filePath(QStringLiteral("vulkan-control-recipe.png")));
        pressedControl.save(QDir(artifacts).filePath(QStringLiteral("vulkan-control-pressed.png")));
    }
    return 0;
}
