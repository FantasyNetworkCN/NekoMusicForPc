/**
 * @file systemnotifier_linux.cpp
 * @brief 系统通知实现（Linux：org.freedesktop.Notifications）
 */

#include "core/systemnotifier.h"
#include "core/i18n.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QStringList>
#include <QVariantMap>

namespace {
constexpr auto kService = "org.freedesktop.Notifications";
constexpr auto kPath = "/org/freedesktop/Notifications";
constexpr auto kInterface = "org.freedesktop.Notifications";
/** 通知停留时长（毫秒），是否遵守由桌面环境决定 */
constexpr int kTimeoutMs = 6000;
/** 与 packaging/nekomusic.desktop 安装名一致，桌面才能配上应用图标 */
constexpr auto kDesktopEntry = "nekomusic";
constexpr auto kDefaultAction = "default";
} // namespace

SystemNotifier &SystemNotifier::instance()
{
    static SystemNotifier notifier;
    return notifier;
}

bool SystemNotifier::isSupported()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    QDBusConnectionInterface *iface = bus.interface();
    return iface && iface->isServiceRegistered(QString::fromLatin1(kService));
}

SystemNotifier::SystemNotifier(QObject *parent) : QObject(parent)
{
    QDBusConnection::sessionBus().connect(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                                          QString::fromLatin1(kInterface),
                                          QStringLiteral("ActionInvoked"), this,
                                          SLOT(handleActionInvoked(uint,QString)));
}

SystemNotifier::~SystemNotifier() = default;

void SystemNotifier::handleActionInvoked(uint id, const QString &action)
{
    if (id != m_lastId || action != QLatin1String(kDefaultAction))
        return;
    m_lastId = 0;
    emit clicked();
}

bool SystemNotifier::notify(const QString &title, const QString &body)
{
    if (!isSupported())
        return false;

    QDBusInterface iface(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                         QString::fromLatin1(kInterface), QDBusConnection::sessionBus());
    if (!iface.isValid())
        return false;

    // 必须是 QStringList：QVariantList 会被 Qt 编组成 `av`，而 Notify 的形参是 `as`
    QStringList actions;
    actions << QString::fromLatin1(kDefaultAction)
            << I18n::instance().tr(QStringLiteral("notifyActionView"));
    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QString::fromLatin1(kDesktopEntry));

    const QDBusMessage reply =
        iface.call(QStringLiteral("Notify"), QCoreApplication::applicationName(), m_lastId,
                   QStringLiteral("dialog-information"), title, body, actions, hints, kTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return false;
    m_lastId = reply.arguments().constFirst().toUInt();
    return true;
}
