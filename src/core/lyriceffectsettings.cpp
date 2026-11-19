#include "lyriceffectsettings.h"

#include <QSettings>

#include <algorithm>

namespace {

constexpr auto kLegacyLyricsEnabled = "lyricEffects/enabled";
constexpr auto kFontScale = "lyricEffects/fontScale";
constexpr auto kLineSpacing = "lyricEffects/lineSpacing";
constexpr auto kAlignPosition = "lyricEffects/alignPosition";
constexpr auto kInactiveScale = "lyricEffects/inactiveScale";
constexpr auto kDistanceBlur = "lyricEffects/distanceBlur";
constexpr auto kBlurStrength = "lyricEffects/blurStrength";
constexpr auto kWordSweep = "lyricEffects/wordSweep";
constexpr auto kWordFloat = "lyricEffects/wordFloat";
constexpr auto kEmphasisGlow = "lyricEffects/emphasisGlow";
constexpr auto kSpringStrength = "lyricEffects/springStrength";
constexpr auto kSpringDamping = "lyricEffects/springDamping";

constexpr auto kBackgroundEnabled = "lyricEffects/amllBackground/enabled";
constexpr auto kBackgroundFlowSpeed = "lyricEffects/amllBackground/flowSpeed";
constexpr auto kBackgroundBassReactivity = "lyricEffects/amllBackground/bassReactivity";
constexpr auto kBackgroundScrimStrength = "lyricEffects/amllBackground/scrimStrength";

} // namespace

LyricEffectSettings &LyricEffectSettings::instance()
{
    static LyricEffectSettings settings;
    return settings;
}

LyricEffectSettings::LyricEffectSettings()
{
    load();
}

void LyricEffectSettings::load()
{
    QSettings settings;
    m_fontScale = std::clamp(
        settings.value(QString::fromLatin1(kFontScale), 1.0).toReal(), 0.6, 2.2);
    m_lineSpacing = std::clamp(
        settings.value(QString::fromLatin1(kLineSpacing), 1.0).toReal(), 0.65, 2.0);
    m_alignPosition = std::clamp(
        settings.value(QString::fromLatin1(kAlignPosition), 0.35).toReal(), 0.15, 0.85);
    m_inactiveScale = std::clamp(
        settings.value(QString::fromLatin1(kInactiveScale), 0.97).toReal(), 0.80, 1.0);
    m_distanceBlur = settings.value(QString::fromLatin1(kDistanceBlur), true).toBool();
    m_blurStrength = std::clamp(
        settings.value(QString::fromLatin1(kBlurStrength), 1.0).toReal(), 0.0, 2.5);
    m_wordSweep = settings.value(QString::fromLatin1(kWordSweep), true).toBool();
    m_wordFloat = settings.value(QString::fromLatin1(kWordFloat), true).toBool();
    m_emphasisGlow = settings.value(QString::fromLatin1(kEmphasisGlow), true).toBool();
    m_springStrength = std::clamp(
        settings.value(QString::fromLatin1(kSpringStrength), 1.0).toReal(), 0.3, 2.5);
    m_springDamping = std::clamp(
        settings.value(QString::fromLatin1(kSpringDamping), 1.0).toReal(), 0.3, 2.5);

    m_backgroundEnabled =
        settings.value(QString::fromLatin1(kBackgroundEnabled), true).toBool();
    m_backgroundFlowSpeed = std::clamp(
        settings.value(QString::fromLatin1(kBackgroundFlowSpeed), 1.0).toReal(),
        0.0, 2.5);
    m_backgroundBassReactivity = std::clamp(
        settings.value(QString::fromLatin1(kBackgroundBassReactivity), 1.0).toReal(),
        0.0, 2.5);
    m_backgroundScrimStrength = std::clamp(
        settings.value(QString::fromLatin1(kBackgroundScrimStrength), 0.68).toReal(),
        0.20, 0.95);

    // 已关闭总开关的旧配置迁移到对应的独立项目。
    if (settings.contains(QLatin1String(kLegacyLyricsEnabled))) {
        if (!settings.value(QLatin1String(kLegacyLyricsEnabled)).toBool()) {
            m_inactiveScale = 1.0;
            m_distanceBlur = false;
            m_wordSweep = false;
            settings.setValue(QLatin1String(kInactiveScale), m_inactiveScale);
            settings.setValue(QLatin1String(kDistanceBlur), m_distanceBlur);
            settings.setValue(QLatin1String(kWordSweep), m_wordSweep);
        }
        settings.remove(QLatin1String(kLegacyLyricsEnabled));
    }
}

void LyricEffectSettings::save() const
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(kFontScale), m_fontScale);
    settings.setValue(QString::fromLatin1(kLineSpacing), m_lineSpacing);
    settings.setValue(QString::fromLatin1(kAlignPosition), m_alignPosition);
    settings.setValue(QString::fromLatin1(kInactiveScale), m_inactiveScale);
    settings.setValue(QString::fromLatin1(kDistanceBlur), m_distanceBlur);
    settings.setValue(QString::fromLatin1(kBlurStrength), m_blurStrength);
    settings.setValue(QString::fromLatin1(kWordSweep), m_wordSweep);
    settings.setValue(QString::fromLatin1(kWordFloat), m_wordFloat);
    settings.setValue(QString::fromLatin1(kEmphasisGlow), m_emphasisGlow);
    settings.setValue(QString::fromLatin1(kSpringStrength), m_springStrength);
    settings.setValue(QString::fromLatin1(kSpringDamping), m_springDamping);

    settings.setValue(QString::fromLatin1(kBackgroundEnabled), m_backgroundEnabled);
    settings.setValue(QString::fromLatin1(kBackgroundFlowSpeed), m_backgroundFlowSpeed);
    settings.setValue(QString::fromLatin1(kBackgroundBassReactivity),
                      m_backgroundBassReactivity);
    settings.setValue(QString::fromLatin1(kBackgroundScrimStrength),
                      m_backgroundScrimStrength);
}

void LyricEffectSettings::setFontScale(qreal scale)
{
    scale = std::clamp(scale, 0.6, 2.2);
    if (qFuzzyCompare(m_fontScale + 1.0, scale + 1.0))
        return;
    m_fontScale = scale;
    save();
    emit layoutChanged();
}

void LyricEffectSettings::setLineSpacing(qreal scale)
{
    scale = std::clamp(scale, 0.65, 2.0);
    if (qFuzzyCompare(m_lineSpacing + 1.0, scale + 1.0))
        return;
    m_lineSpacing = scale;
    save();
    emit layoutChanged();
}

void LyricEffectSettings::setAlignPosition(qreal position)
{
    position = std::clamp(position, 0.15, 0.85);
    if (qFuzzyCompare(m_alignPosition + 1.0, position + 1.0))
        return;
    m_alignPosition = position;
    save();
    emit layoutChanged();
}

void LyricEffectSettings::setInactiveScale(qreal scale)
{
    scale = std::clamp(scale, 0.80, 1.0);
    if (qFuzzyCompare(m_inactiveScale + 1.0, scale + 1.0))
        return;
    m_inactiveScale = scale;
    save();
    emit effectsChanged();
}

void LyricEffectSettings::setDistanceBlur(bool enabled)
{
    if (m_distanceBlur == enabled)
        return;
    m_distanceBlur = enabled;
    save();
    emit effectsChanged();
}

void LyricEffectSettings::setBlurStrength(qreal strength)
{
    strength = std::clamp(strength, 0.0, 2.5);
    if (qFuzzyCompare(m_blurStrength + 1.0, strength + 1.0))
        return;
    m_blurStrength = strength;
    save();
    emit effectsChanged();
}

void LyricEffectSettings::setWordSweep(bool enabled)
{
    if (m_wordSweep == enabled)
        return;
    m_wordSweep = enabled;
    save();
    emit effectsChanged();
}

void LyricEffectSettings::setWordFloat(bool enabled)
{
    if (m_wordFloat == enabled)
        return;
    m_wordFloat = enabled;
    save();
    emit effectsChanged();
}

void LyricEffectSettings::setEmphasisGlow(bool enabled)
{
    if (m_emphasisGlow == enabled)
        return;
    m_emphasisGlow = enabled;
    save();
    emit effectsChanged();
}

void LyricEffectSettings::setSpringStrength(qreal scale)
{
    scale = std::clamp(scale, 0.3, 2.5);
    if (qFuzzyCompare(m_springStrength + 1.0, scale + 1.0))
        return;
    m_springStrength = scale;
    save();
    emit effectsChanged();
}

void LyricEffectSettings::setSpringDamping(qreal scale)
{
    scale = std::clamp(scale, 0.3, 2.5);
    if (qFuzzyCompare(m_springDamping + 1.0, scale + 1.0))
        return;
    m_springDamping = scale;
    save();
    emit effectsChanged();
}

void LyricEffectSettings::setBackgroundEnabled(bool enabled)
{
    if (m_backgroundEnabled == enabled)
        return;
    m_backgroundEnabled = enabled;
    save();
    emit backgroundChanged();
}

void LyricEffectSettings::setBackgroundFlowSpeed(qreal speed)
{
    speed = std::clamp(speed, 0.0, 2.5);
    if (qFuzzyCompare(m_backgroundFlowSpeed + 1.0, speed + 1.0))
        return;
    m_backgroundFlowSpeed = speed;
    save();
    emit backgroundChanged();
}

void LyricEffectSettings::setBackgroundBassReactivity(qreal gain)
{
    gain = std::clamp(gain, 0.0, 2.5);
    if (qFuzzyCompare(m_backgroundBassReactivity + 1.0, gain + 1.0))
        return;
    m_backgroundBassReactivity = gain;
    save();
    emit backgroundChanged();
}

void LyricEffectSettings::setBackgroundScrimStrength(qreal strength)
{
    strength = std::clamp(strength, 0.20, 0.95);
    if (qFuzzyCompare(m_backgroundScrimStrength + 1.0, strength + 1.0))
        return;
    m_backgroundScrimStrength = strength;
    save();
    emit backgroundChanged();
}

void LyricEffectSettings::resetToDefaults()
{
    m_fontScale = 1.0;
    m_lineSpacing = 1.0;
    m_alignPosition = 0.35;
    m_inactiveScale = 0.97;
    m_distanceBlur = true;
    m_blurStrength = 1.0;
    m_wordSweep = true;
    m_wordFloat = true;
    m_emphasisGlow = true;
    m_springStrength = 1.0;
    m_springDamping = 1.0;
    m_backgroundEnabled = true;
    m_backgroundFlowSpeed = 1.0;
    m_backgroundBassReactivity = 1.0;
    m_backgroundScrimStrength = 0.68;
    save();
    emit layoutChanged();
    emit effectsChanged();
    emit backgroundChanged();
}
