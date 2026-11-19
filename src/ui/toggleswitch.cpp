#include "toggleswitch.h"

#include "core/appearancesettings.h"
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
    connect(this, &QAbstractButton::toggled, this, &ToggleSwitch::animateTo);
    m_pressAnim = new QPropertyAnimation(this, "pressure", this);
    connect(m_pressAnim, &QPropertyAnimation::valueChanged, this, [this]() { update(); });
    connect(this, &QAbstractButton::pressed, this, [this]() { animatePressure(1); });
    connect(this, &QAbstractButton::released, this, [this]() { animatePressure(0); });
    connect(&AppearanceSettings::instance(), &AppearanceSettings::liquidGlassProgressChanged,
            this, [this](qreal progress) {
                m_liquidProgress = progress;
                if (progress <= 0)
                    m_controlGlass.clear();
                update();
            });
    m_liquidProgress = AppearanceSettings::instance().liquidGlassProgress();
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
    const qreal target = checked ? 1.0 : 0.0;
    if (m_anim->state() == QAbstractAnimation::Running && m_anim->endValue().toReal() == target)
        return;
    m_anim->stop();
    if (qFuzzyCompare(m_knob, target))
        return;
    const bool glass = AppearanceSettings::instance().liquidGlassEnabled()
        || AppearanceSettings::instance().liquidGlassProgress() > 0;
    m_anim->setDuration(glass ? (checked ? 250 : 280) : Theme::kAnimFast);
    m_anim->setEasingCurve(glass ? QEasingCurve::OutCubic : QEasingCurve::InOutCubic);
    m_anim->setStartValue(m_knob);
    m_anim->setEndValue(target);
    m_anim->start();
}

void ToggleSwitch::animatePressure(qreal target)
{
    const auto &appearance = AppearanceSettings::instance();
    if (target > 0 && !appearance.liquidGlassEnabled() && appearance.liquidGlassProgress() <= 0)
        return;
    m_pressAnim->stop();
    if (m_pressure == target)
        return;
    m_pressAnim->setDuration(target > 0 ? 90 : 230);
    m_pressAnim->setEasingCurve(target > 0 ? QEasingCurve::OutQuad : QEasingCurve::OutQuint);
    m_pressAnim->setStartValue(m_pressure);
    m_pressAnim->setEndValue(target);
    m_pressAnim->start();
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const qreal progress = m_liquidProgress;
    if (progress <= 0) {
        paintStandard(painter, dark);
        return;
    }
    const auto &backdrop = LiquidGlass::shellBackdrop(this);
    if (backdrop.isEmpty()) {
        paintStandard(painter, dark);
        return;
    }
    if (progress < 1) {
        paintStandard(painter, dark);
        painter.setOpacity(progress);
    }
    paintLiquid(painter, backdrop, dark);
}

void ToggleSwitch::paintStandard(QPainter &painter, bool dark) const
{
    const qreal h = height();
    const qreal w = width();
    const qreal radius = h / 2.0;

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

void ToggleSwitch::paintLiquid(QPainter &painter, const LiquidGlass::Backdrop &backdrop, bool dark)
{
    const QPoint origin = mapTo(window(), QPoint());
    const QRect track = rect();
    const qreal radius = track.height() / 2.0;
    constexpr qreal inset = 2.5;
    const qreal diameter = track.height() - 2 * inset;
    const qreal travel = track.width() - diameter - 2 * inset;
    const qreal thumbWidth = diameter + 3.5 * m_pressure;
    const qreal thumbHeight = diameter - 1.3 * m_pressure;
    const qreal centerX = qBound(inset + thumbWidth / 2,
                                 inset + diameter / 2 + travel * m_knob,
                                 track.width() - inset - thumbWidth / 2);
    const QRectF thumb(centerX - thumbWidth / 2, (track.height() - thumbHeight) / 2,
                       thumbWidth, thumbHeight);
    LiquidGlass::ControlRecipe recipe;
    recipe.track = QRectF(track);
    recipe.thumb = thumb;
    recipe.trackRadius = radius;
    recipe.thumbRadius = thumbHeight / 2;
    recipe.pressure = m_pressure;
    recipe.dark = dark;
    recipe.checked = isChecked();
    m_controlGlass.paint(painter, track, backdrop, track.translated(origin), recipe);

    if (hasFocus()) {
        painter.setPen(QPen(QColor(Theme::kLavenderDk), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(track).adjusted(0.75, 0.75, -0.75, -0.75),
                                radius, radius);
    }
}
