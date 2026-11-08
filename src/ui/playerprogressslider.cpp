#include "playerprogressslider.h"

#include "theme/thememanager.h"

#include <QEnterEvent>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QEasingCurve>

namespace {

constexpr int kGrooveH = 3;
constexpr int kHandlePx = 14;
constexpr int kAnimMs = 180;

} // namespace

PlayerProgressSlider::PlayerProgressSlider(QWidget *parent)
    : QSlider(Qt::Horizontal, parent)
{
    setMouseTracking(true);

    m_handleAnim = new QPropertyAnimation(this, "handleReveal", this);
    m_handleAnim->setDuration(kAnimMs);
    m_handleAnim->setEasingCurve(QEasingCurve::OutCubic);
}

void PlayerProgressSlider::setHandleReveal(qreal reveal)
{
    const qreal clamped = qBound(0.0, reveal, 1.0);
    if (qFuzzyCompare(m_handleReveal, clamped))
        return;
    m_handleReveal = clamped;
    update();
}

void PlayerProgressSlider::animateHandleReveal(bool show)
{
    if (!m_handleAnim)
        return;
    m_handleAnim->stop();
    m_handleAnim->setStartValue(m_handleReveal);
    m_handleAnim->setEndValue(show ? 1.0 : 0.0);
    m_handleAnim->start();
}

void PlayerProgressSlider::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QRect r = rect();
    const int grooveY = (r.height() - kGrooveH) / 2;
    const QRect groove(r.left(), grooveY, r.width(), kGrooveH);

    const int range = maximum() - minimum();
    const double t = range > 0 ? (value() - minimum()) / static_cast<double>(range) : 0.0;

    p.setPen(Qt::NoPen);
    p.setBrush(dark ? QColor(255, 255, 255, 16) : QColor(0, 0, 0, 26));
    p.drawRoundedRect(groove, 2, 2);

    if (t > 0.0) {
        QRect filled(groove.left(), groove.top(), qMax(kGrooveH, int(groove.width() * t)), kGrooveH);
        p.setBrush(dark ? QColor(64, 197, 241) : QColor(22, 119, 184));
        p.drawRoundedRect(filled, 2, 2);
    }

    if (m_handleReveal <= 0.001)
        return;

    const int cx = groove.left() + int(groove.width() * t);
    const int cy = r.center().y();
    const int size = qMax(2, int(kHandlePx * m_handleReveal));
    const QRect handleRect(cx - size / 2, cy - size / 2, size, size);

    p.setOpacity(m_handleReveal);
    p.setBrush(dark ? QColor(138, 223, 245) : QColor(138, 223, 245));
    p.setPen(QPen(dark ? QColor(22, 119, 184, 140) : QColor(64, 197, 241, 90), 1));
    p.drawEllipse(handleRect);
}

void PlayerProgressSlider::enterEvent(QEnterEvent *event)
{
    QSlider::enterEvent(event);
    animateHandleReveal(true);
}

void PlayerProgressSlider::leaveEvent(QEvent *event)
{
    QSlider::leaveEvent(event);
    if (!isSliderDown())
        animateHandleReveal(false);
}

int PlayerProgressSlider::valueFromX(int x) const
{
    const int span = maximum() - minimum();
    if (span <= 0)
        return minimum();

    // 轨道横跨整个控件；-1 保证点到最右侧能到达最大值。
    const int trackW = qMax(1, width() - 1);
    const qreal t = qBound(qreal(0.0), qreal(x) / qreal(trackW), qreal(1.0));
    return minimum() + qRound(t * span);
}

void PlayerProgressSlider::mousePressEvent(QMouseEvent *event)
{
    // 样式表把 groove/handle 尺寸置 0，QSlider 默认点击定位会失效；
    // 这里自行按 x 计算目标值，实现「点击跳转 + 拖动」。
    if (event->button() == Qt::LeftButton && maximum() > minimum()) {
        setSliderDown(true);
        setValue(valueFromX(int(event->position().x())));
        event->accept();
    } else {
        QSlider::mousePressEvent(event);
    }
    animateHandleReveal(true);
}

void PlayerProgressSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (isSliderDown() && (event->buttons() & Qt::LeftButton)) {
        setValue(valueFromX(int(event->position().x())));
        event->accept();
        return;
    }
    QSlider::mouseMoveEvent(event);
}

void PlayerProgressSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isSliderDown()) {
        setValue(valueFromX(int(event->position().x())));
        // setSliderDown(false) 会发出 sliderReleased，由外部连接负责 seek。
        setSliderDown(false);
        event->accept();
    } else {
        QSlider::mouseReleaseEvent(event);
    }
    if (!underMouse())
        animateHandleReveal(false);
}
