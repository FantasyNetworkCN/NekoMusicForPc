/**
 * @file toast.cpp
 * @brief Toast 消息提示组件实现
 */

#include "toast.h"
#include "svgicon.h"
#include "theme/theme.h"

#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QTimer>
#include <QVariant>

Toast::Toast(QWidget *parent, const QString &message, Type type, int durationMs)
    : QWidget(parent), m_durationMs(durationMs)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    initUi(message, type);
}

void Toast::show(QWidget *parent, const QString &message, Type type, int durationMs)
{
    qDebug() << "[Toast] 创建Toast:" << message << ", type =" << type << ", duration =" << durationMs;
    // 调用方经常传入曲目行、按钮等子控件；Toast 必须锚定顶层窗口，
    // 否则会按子控件的小矩形计算位置，出现在列表内部甚至右侧屏幕外。
    QWidget *anchor = parent ? parent->window() : nullptr;
    if (!anchor)
        anchor = parent;
    auto *toast = new Toast(anchor, message, type, durationMs);

    // 确保窗口大小已计算
    toast->adjustSize();
    int toastW = toast->width();
    int toastH = toast->height();
    int parentW = anchor ? anchor->width() : 800;

    // 计算位置：标题栏下方，右上角
    int targetX = parentW - toastW - 24;
    int y = 64;
    QPoint startPos(parentW + 24, y);     // 屏幕右侧外
    QPoint targetPos(targetX, y);          // 最终位置

    // 初始放在屏幕外
    toast->move(startPos);

    // 透明度效果
    auto *effect = new QGraphicsOpacityEffect(toast);
    effect->setOpacity(0.0);
    toast->setGraphicsEffect(effect);

    // ── 入场动画：滑动 + 透明度 ──
    // 滑动：OutBack 产生过冲回弹的液态感
    auto *slideIn = new QPropertyAnimation(toast, "pos", toast);
    slideIn->setDuration(450);
    slideIn->setStartValue(startPos);
    slideIn->setEndValue(targetPos);
    slideIn->setEasingCurve(QEasingCurve::OutBack);

    // 透明度：比滑动先完成，形成"先显形再落位"
    auto *fadeIn = new QPropertyAnimation(effect, "opacity", toast);
    fadeIn->setDuration(250);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutCubic);

    auto *enterGroup = new QParallelAnimationGroup(toast);
    enterGroup->addAnimation(slideIn);
    enterGroup->addAnimation(fadeIn);

    // ── 出场动画：滑动 + 透明度 ──
    // 滑动：InBack 让Toast短暂犹豫后加速离开，像水滴被拉走
    auto *slideOut = new QPropertyAnimation(toast, "pos", toast);
    slideOut->setDuration(350);
    slideOut->setStartValue(targetPos);
    slideOut->setEndValue(QPoint(parentW + 24, y));
    slideOut->setEasingCurve(QEasingCurve::InBack);

    // 透明度
    auto *fadeOut = new QPropertyAnimation(effect, "opacity", toast);
    fadeOut->setDuration(250);
    fadeOut->setStartValue(1.0);
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InCubic);

    auto *exitGroup = new QParallelAnimationGroup(toast);
    exitGroup->addAnimation(slideOut);
    exitGroup->addAnimation(fadeOut);

    QObject::connect(exitGroup, &QParallelAnimationGroup::finished, toast, [toast]() {
        qDebug() << "[Toast] 出场动画结束，销毁Toast";
        toast->deleteLater();
    });

    // 定时触发退出
    auto *timer = new QTimer(toast);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, toast, [exitGroup]() {
        qDebug() << "[Toast] 定时器触发，开始出场动画";
        exitGroup->start();
    });

    // 启动
    static_cast<QWidget *>(toast)->show();
    enterGroup->start();
    qDebug() << "[Toast] Toast已show，入场动画启动";
    timer->start(durationMs);
}

void Toast::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 背景
    QPainterPath bgPath;
    bgPath.addRoundedRect(rect(), 12, 12);
    p.fillPath(bgPath, QColor(45, 38, 65, 230));

    // 左侧彩色条
    QColor borderColor = property("borderColor").value<QColor>();
    QPainterPath barPath;
    barPath.addRoundedRect(0, 2, 4, height() - 4, 2, 2);
    p.fillPath(barPath, borderColor);
}

void Toast::initUi(const QString &message, Type type)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(20, 16, 20, 16);
    lay->setSpacing(12);

    // 图标
    auto *iconLbl = new QLabel(this);
    iconLbl->setFixedSize(20, 20);
    QColor iconColor;
    if (type == Success) iconColor = QColor(74, 222, 128);
    else if (type == Error) iconColor = QColor(239, 68, 68);
    else iconColor = QColor(59, 130, 246);

    const char *iconName = "Info";
    if (type == Success)
        iconName = "Check";
    else if (type == Error)
        iconName = "Close";
    iconLbl->setPixmap(Icons::renderNamed(iconName, 20, iconColor));
    lay->addWidget(iconLbl);

    // 消息文本
    auto *msgLbl = new QLabel(message, this);
    msgLbl->setWordWrap(false);
    msgLbl->setStyleSheet(
        "QLabel { font-size: 14px; font-weight: 500; color: " + QString(Theme::kTextMain) + "; }"
    );
    lay->addWidget(msgLbl);

    // 设置窗口大小
    QFontMetrics fm(font());
    int textWidth = fm.horizontalAdvance(message);
    int minWidth = qMax(300, textWidth + 100);
    setFixedSize(minWidth, 52);

    // 更新左侧条的颜色
    QColor borderColor;
    if (type == Success) borderColor = QColor(74, 222, 128);
    else if (type == Error) borderColor = QColor(239, 68, 68);
    else borderColor = QColor(59, 130, 246);

    // 通过 stylesheet 实现左侧彩色条
    setStyleSheet(
        "Toast { border-radius: 12px; }"
    );

    // 存储颜色供 paintEvent 使用
    setProperty("borderColor", borderColor);
}
