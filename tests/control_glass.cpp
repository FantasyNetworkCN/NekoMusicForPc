#include "core/appearancesettings.h"
#include "theme/thememanager.h"
#include "ui/toggleswitch.h"

#include <QApplication>
#include <QComboBox>
#include <QAbstractItemView>
#include <QElapsedTimer>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

namespace {

int differingPixels(const QImage &a, const QImage &b)
{
    if (a.size() != b.size())
        return -1;
    int different = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixel(x, y) != b.pixel(x, y))
                ++different;
        }
    }
    return different;
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        QTest::qWait(10);
    return predicate();
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", (temporary.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (temporary.path() + "/data").toUtf8());
    qputenv("XDG_CACHE_HOME", (temporary.path() + "/cache").toUtf8());
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("NekoMusicControlGlassTest"));
    app.setOrganizationName(QStringLiteral("NekoMusicControlGlassTest"));
    app.setStyle(QStringLiteral("Fusion"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    Theme::ThemeManager::instance().setMode(Theme::Dark);

    QComboBox quality;
    quality.setFixedWidth(78);
    quality.addItems({QStringLiteral("标准"), QStringLiteral("HQ"),
                      QStringLiteral("SQ"), QStringLiteral("Hi-Res")});
    LiquidGlass::installComboBox(&quality);
    if (quality.view()->sizeHint().width()
        < quality.fontMetrics().horizontalAdvance(QStringLiteral("Hi-Res")) + 42)
        return 11;

    QImage backdrop(160, 90, QImage::Format_RGB32);
    backdrop.fill(QColor("#17233a"));
    {
        QPainter painter(&backdrop);
        for (int x = 0; x < backdrop.width(); x += 8)
            painter.fillRect(x, 0, 2, backdrop.height(), QColor("#dce9fa"));
        painter.fillRect(0, 44, backdrop.width(), 2, QColor("#62a6ea"));
    }

    QWidget host;
    host.resize(backdrop.size());
    host.setProperty("nekoLiquidBackdrop", QPixmap::fromImage(backdrop));
    ToggleSwitch toggle(&host);
    toggle.setGeometry(58, 32, 44, 24);
    host.show();
    QCoreApplication::processEvents();
    const QImage standard = toggle.grab().toImage().convertToFormat(QImage::Format_ARGB32);

    auto &appearance = AppearanceSettings::instance();
    appearance.setLiquidGlassEnabled(true);
    if (!waitFor([&]() { return appearance.liquidGlassProgress() >= 0.99; }, 1500))
        return 2;
    toggle.setChecked(true);
    if (!waitFor([&]() { return toggle.knob() >= 0.99; }, 1000))
        return 3;
    const QImage liquid = toggle.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    if (liquid.isNull() || differingPixels(standard, liquid) < 50)
        return 4;

    QTest::mousePress(&toggle, Qt::LeftButton, {}, QPoint(22, 12));
    if (!waitFor([&]() { return toggle.property("pressure").toReal() > 0.85; }, 500))
        return 5;
    const QImage pressed = toggle.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    if (differingPixels(liquid, pressed) < 10)
        return 6;
    QTest::mouseRelease(&toggle, Qt::LeftButton, {}, QPoint(-4, -4));

    if (toggle.knob() < 0.99)
        return 7;
    return 0;
}
