#include "toggleswitch.h"

#include "theme/theme.h"
#include "theme/thememanager.h"

#include <QEasingCurve>
#include <QPainter>
#include <QPropertyAnimation>

namespace {

QColor blendColor(const QColor &from, const QColor &to, qreal t)
{
    const qreal k = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * k,
                            from.greenF() + (to.greenF() - from.greenF()) * k,
                            from.blueF() + (to.blueF() - from.blueF()) * k,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * k);
}

} // namespace

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_anim = new QPropertyAnimation(this, "knob", this);
    m_anim->setDuration(Theme::kAnimFast);
    m_anim->setEasingCurve(QEasingCurve::InOutCubic);
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(44, 24);
}

void ToggleSwitch::setKnob(qreal value)
{
    const qreal clamped = qBound(0.0, value, 1.0);
    if (qFuzzyCompare(m_knob, clamped))
        return;
    m_knob = clamped;
    update();
}

void ToggleSwitch::checkStateSet()
{
    QAbstractButton::checkStateSet();
    animateTo(isChecked());
}

void ToggleSwitch::animateTo(bool checked)
{
    if (!m_anim) {
        m_knob = checked ? 1.0 : 0.0;
        update();
        return;
    }
    m_anim->stop();
    m_anim->setStartValue(m_knob);
    m_anim->setEndValue(checked ? 1.0 : 0.0);
    m_anim->start();
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal h = height();
    const qreal w = width();
    const qreal radius = h / 2.0;

    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    QColor trackOff = dark ? QColor(255, 255, 255, 38) : QColor(66, 91, 125, 65);
    QColor trackOn(Theme::kLavender);
    if (!isEnabled()) {
        trackOff.setAlpha(22);
        trackOn.setAlpha(90);
    }
    const QColor track = blendColor(trackOff, trackOn, m_knob);

    painter.setPen(hasFocus() ? QPen(QColor(Theme::kLavenderDk), 2) : QPen(Qt::NoPen));
    painter.setBrush(track);
    painter.drawRoundedRect(QRectF(0, 0, w, h), radius, radius);

    const qreal margin = 3.0;
    const qreal knobDiameter = h - margin * 2.0;
    const qreal travel = w - knobDiameter - margin * 2.0;
    const qreal knobX = margin + travel * m_knob;

    painter.setBrush(QColor(255, 255, 255, isEnabled() ? 245 : 150));
    painter.drawEllipse(QRectF(knobX, margin, knobDiameter, knobDiameter));
}
