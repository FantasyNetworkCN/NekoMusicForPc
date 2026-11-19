#include "appearancesettings.h"

#include <QSettings>
#include <QVariantAnimation>

namespace {
constexpr auto kLiquidGlass = "appearance/liquidGlass";
}

AppearanceSettings &AppearanceSettings::instance()
{
    static AppearanceSettings settings;
    return settings;
}

AppearanceSettings::AppearanceSettings()
    : m_liquidGlassEnabled(QSettings().value(QLatin1String(kLiquidGlass), false).toBool()),
      m_liquidGlassProgress(m_liquidGlassEnabled ? 1 : 0),
      m_liquidGlassTransition(new QVariantAnimation(this))
{
    m_liquidGlassTransition->setEasingCurve(QEasingCurve::InOutCubic);
    connect(m_liquidGlassTransition, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_liquidGlassProgress = value.toReal();
        emit liquidGlassProgressChanged(m_liquidGlassProgress);
    });
}

void AppearanceSettings::setLiquidGlassEnabled(bool enabled)
{
    if (m_liquidGlassEnabled == enabled)
        return;
    m_liquidGlassEnabled = enabled;
    QSettings().setValue(QLatin1String(kLiquidGlass), enabled);
    emit liquidGlassChanged(enabled);
    m_liquidGlassTransition->stop();
    m_liquidGlassTransition->setDuration(220);
    m_liquidGlassTransition->setStartValue(m_liquidGlassProgress);
    m_liquidGlassTransition->setEndValue(enabled ? 1.0 : 0.0);
    m_liquidGlassTransition->start();
}
