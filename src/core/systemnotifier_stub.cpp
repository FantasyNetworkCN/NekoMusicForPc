/**
 * @file systemnotifier_stub.cpp
 * @brief 系统通知占位实现（Windows/macOS：沿用托盘气泡通知）
 */

#include "core/systemnotifier.h"

SystemNotifier &SystemNotifier::instance()
{
    static SystemNotifier notifier;
    return notifier;
}

bool SystemNotifier::isSupported()
{
    return false;
}

SystemNotifier::SystemNotifier(QObject *parent) : QObject(parent) {}

SystemNotifier::~SystemNotifier() = default;

bool SystemNotifier::notify(const QString &, const QString &)
{
    return false;
}

void SystemNotifier::handleActionInvoked(uint, const QString &) {}
