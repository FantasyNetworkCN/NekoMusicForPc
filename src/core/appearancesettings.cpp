#include "appearancesettings.h"

#include <QSettings>
#include <QSignalBlocker>
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
    connect(m_liquidGlassTransition, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_liquidGlassProgress = value.toReal();
        emit liquidGlassProgressChanged(m_liquidGlassProgress);
    });
}

void AppearanceSettings::setLiquidGlassEnabled(bool enabled)
{
    if (m_liquidGlassEnabled == enabled)
        return;
    const qreal start = m_liquidGlassProgress;
    const qreal target = enabled ? 1 : 0;
    m_liquidGlassTransition->stop();
    m_liquidGlassEnabled = enabled;
    QSettings().setValue(QLatin1String(kLiquidGlass), enabled);
    emit liquidGlassChanged(enabled);
    if (start == target) {
        emit liquidGlassProgressChanged(target);
        return;
    }
    {
        // 改参数会重新计算当前帧。阻止尚未重置时间轴时的中间值跳过退场。
        const QSignalBlocker blocker(m_liquidGlassTransition);
        m_liquidGlassTransition->setCurrentTime(0);
        m_liquidGlassTransition->setDuration(qMax(120, qRound((enabled ? 260 : 320) * qAbs(target - start))));
        m_liquidGlassTransition->setEasingCurve(QEasingCurve::InOutCubic);
        m_liquidGlassTransition->setStartValue(start);
        m_liquidGlassTransition->setEndValue(target);
    }
    m_liquidGlassTransition->start();
}
