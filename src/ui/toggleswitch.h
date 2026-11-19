#pragma once

#include <QAbstractButton>

#include "ui/liquidglass.h"

class QPainter;
class QPropertyAnimation;

/** 胶囊开关，可随外观设置过渡到折射轨道与滑块。 */
class ToggleSwitch final : public QAbstractButton
{
    Q_OBJECT
    Q_PROPERTY(qreal knob READ knob WRITE setKnob)
    Q_PROPERTY(qreal pressure MEMBER m_pressure)

public:
    explicit ToggleSwitch(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

    qreal knob() const { return m_knob; }
    void setKnob(qreal value);
    void followLiquidGlassProgress();

protected:
    void paintEvent(QPaintEvent *event) override;
    void checkStateSet() override;

private:
    void animateTo(bool checked);
    void animatePressure(qreal target);
    void paintStandard(QPainter &painter, bool dark) const;
    void paintLiquid(QPainter &painter, const LiquidGlass::Backdrop &backdrop, bool dark);

    qreal m_knob = 0.0;
    qreal m_pressure = 0.0;
    bool m_followsLiquidGlass = false;
    QPropertyAnimation *m_anim = nullptr;
    QPropertyAnimation *m_pressAnim = nullptr;
    LiquidGlass::Surface m_glassTrack;
    LiquidGlass::Surface m_glassThumb;
};
