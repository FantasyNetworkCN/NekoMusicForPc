#pragma once

#include <QAbstractScrollArea>
#include <QApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWidget>

/** Windows Fusion 下 QScrollArea 视口常带不透明系统背景，会盖住父级毛玻璃/渐变。 */
inline void nekoPolishScrollAreaViewport(QScrollArea *scroll)
{
    if (!scroll)
        return;
    scroll->setAutoFillBackground(false);
    if (QWidget *vp = scroll->viewport()) {
        vp->setAutoFillBackground(false);
        vp->setAttribute(Qt::WA_NoSystemBackground, true);
    }
}

class NekoSmoothScrollFilter final : public QObject
{
public:
    explicit NekoSmoothScrollFilter(QObject *parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Wheel)
            return QObject::eventFilter(watched, event);

        auto *watchedWidget = qobject_cast<QWidget *>(watched);
        if (watchedWidget
            && watchedWidget->property("nekoDisableSmoothScroll").toBool()) {
            return false;
        }

        auto *area = qobject_cast<QAbstractScrollArea *>(watched);
        if (!area) {
            area = watchedWidget
                ? qobject_cast<QAbstractScrollArea *>(
                      watchedWidget->parentWidget())
                : nullptr;
        }
        if (!area || watched != area->viewport())
            return QObject::eventFilter(watched, event);
        if (area->property("nekoDisableSmoothScroll").toBool())
            return false;

        auto *wheel = static_cast<QWheelEvent *>(event);
        const QPoint pixelDelta = wheel->pixelDelta();
        const QPoint angleDelta = wheel->angleDelta();
        const bool horizontal = qAbs(pixelDelta.x()) > qAbs(pixelDelta.y())
                                || (angleDelta.x() != 0 && qAbs(angleDelta.x()) >= qAbs(angleDelta.y()))
                                || (wheel->modifiers() & Qt::ShiftModifier);
        QScrollBar *bar = horizontal ? area->horizontalScrollBar() : area->verticalScrollBar();
        const int rawDelta = horizontal
                                 ? (pixelDelta.x() != 0 ? pixelDelta.x() : angleDelta.x())
                                 : (pixelDelta.y() != 0 ? pixelDelta.y() : angleDelta.y());
        if (!bar || rawDelta == 0 || bar->minimum() == bar->maximum())
            return QObject::eventFilter(watched, event);

        const int baseStep = qMax(24, bar->singleStep() * QApplication::wheelScrollLines());
        const int distance = pixelDelta.isNull() ? rawDelta * baseStep / 120 : rawDelta;

        QPointer<QPropertyAnimation> animationPtr = m_animations.value(bar);
        auto *animation = animationPtr.data();
        // 仅当上一段动画仍在运行时才基于其记录目标累加：动画已结束或滚动条被
        // 外部复位（切换列表/刷新内容后 value 跳回顶部等）时，旧目标已与当前
        // 位置脱节，必须从当前值重新起算，否则第一下滚动会从旧目标起跳爆冲。
        const bool running = animation && animation->state() == QAbstractAnimation::Running;
        const int recorded = m_targets.value(bar, bar->value());
        const int start = (running && qAbs(recorded - bar->value()) <= 4 * baseStep)
                              ? recorded
                              : bar->value();
        const int target = qBound(bar->minimum(), start - distance, bar->maximum());
        if (target == bar->value())
            return true;

        if (!animation) {
            animation = new QPropertyAnimation(bar, "value", this);
            animation->setEasingCurve(QEasingCurve::OutCubic);
            m_animations.insert(bar, animation);
            connect(bar, &QObject::destroyed, this, [this, bar]() {
                m_targets.remove(bar);
                m_animations.remove(bar);
            });
        }

        m_targets.insert(bar, target);
        animation->stop();
        animation->setDuration(220);
        animation->setStartValue(bar->value());
        animation->setEndValue(target);
        animation->start();
        return true;
    }

private:
    QHash<QScrollBar *, int> m_targets;
    QHash<QScrollBar *, QPointer<QPropertyAnimation>> m_animations;
};

inline void nekoInstallSmoothScroll(QApplication &app)
{
    app.installEventFilter(new NekoSmoothScrollFilter(&app));
}
