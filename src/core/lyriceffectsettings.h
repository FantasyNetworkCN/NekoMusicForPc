#pragma once

#include <QObject>

/**
 * AMLL 歌词与流动背景的可持久化效果参数。
 *
 * 所有值都做了安全范围钳制；设置页拖动滑块时立即生效，无需重启。
 */
class LyricEffectSettings final : public QObject
{
    Q_OBJECT

public:
    static LyricEffectSettings &instance();

    qreal fontScale() const { return m_fontScale; }
    qreal lineSpacing() const { return m_lineSpacing; }
    qreal alignPosition() const { return m_alignPosition; }
    qreal inactiveScale() const { return m_inactiveScale; }
    bool distanceBlur() const { return m_distanceBlur; }
    qreal blurStrength() const { return m_blurStrength; }
    bool wordSweep() const { return m_wordSweep; }
    bool wordFloat() const { return m_wordFloat; }
    bool emphasisGlow() const { return m_emphasisGlow; }
    qreal springStrength() const { return m_springStrength; }
    qreal springDamping() const { return m_springDamping; }

    bool backgroundEnabled() const { return m_backgroundEnabled; }
    qreal backgroundFlowSpeed() const { return m_backgroundFlowSpeed; }
    qreal backgroundBassReactivity() const { return m_backgroundBassReactivity; }
    qreal backgroundScrimStrength() const { return m_backgroundScrimStrength; }

    void setFontScale(qreal scale);
    void setLineSpacing(qreal scale);
    void setAlignPosition(qreal position);
    void setInactiveScale(qreal scale);
    void setDistanceBlur(bool enabled);
    void setBlurStrength(qreal strength);
    void setWordSweep(bool enabled);
    void setWordFloat(bool enabled);
    void setEmphasisGlow(bool enabled);
    void setSpringStrength(qreal scale);
    void setSpringDamping(qreal scale);

    void setBackgroundEnabled(bool enabled);
    void setBackgroundFlowSpeed(qreal speed);
    void setBackgroundBassReactivity(qreal gain);
    void setBackgroundScrimStrength(qreal strength);

    void resetToDefaults();

signals:
    /** 字号、行距、焦点位置等影响排版几何的参数变化。 */
    void layoutChanged();
    /** 模糊、扫光、上浮、强调、弹簧等纯视觉参数变化。 */
    void effectsChanged();
    /** AMLL 流动背景参数变化。 */
    void backgroundChanged();

private:
    LyricEffectSettings();
    LyricEffectSettings(const LyricEffectSettings &) = delete;
    LyricEffectSettings &operator=(const LyricEffectSettings &) = delete;

    void load();
    void save() const;

    qreal m_fontScale = 1.0;
    qreal m_lineSpacing = 1.0;
    qreal m_alignPosition = 0.35;
    qreal m_inactiveScale = 0.97;
    bool m_distanceBlur = true;
    qreal m_blurStrength = 1.0;
    bool m_wordSweep = true;
    bool m_wordFloat = true;
    bool m_emphasisGlow = true;
    qreal m_springStrength = 1.0;
    qreal m_springDamping = 1.0;

    bool m_backgroundEnabled = true;
    qreal m_backgroundFlowSpeed = 1.0;
    qreal m_backgroundBassReactivity = 1.0;
    qreal m_backgroundScrimStrength = 0.68;
};
