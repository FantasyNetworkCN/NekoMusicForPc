#include "globalshortcutportal_linux.h"
#include "appshortcuts.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QGuiApplication>
#include <QMetaType>
#include <QRandomGenerator>
#include <QTimer>
#include <QWindow>

using PortalShortcutEntry = QPair<QString, QVariantMap>;
using PortalShortcutList = QList<PortalShortcutEntry>;

Q_DECLARE_METATYPE(PortalShortcutEntry)
Q_DECLARE_METATYPE(PortalShortcutList)

namespace {

constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kGlobalShortcutsIface = "org.freedesktop.portal.GlobalShortcuts";
constexpr auto kRequestIface = "org.freedesktop.portal.Request";
constexpr auto kSessionIface = "org.freedesktop.portal.Session";

void registerPortalShortcutTypes()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;

    qDBusRegisterMetaType<PortalShortcutEntry>();
    qDBusRegisterMetaType<PortalShortcutList>();
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcutEntry &entry)
{
    argument.beginStructure();
    argument << entry.first << entry.second;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcutEntry &entry)
{
    argument.beginStructure();
    argument >> entry.first >> entry.second;
    argument.endStructure();
    return argument;
}

PortalShortcutList buildPortalShortcutList()
{
    PortalShortcutList shortcuts;
    for (int i = 0; i < AppShortcuts::ActionCount; ++i) {
        const auto action = static_cast<AppShortcuts::Action>(i);
        const QKeySequence seq = AppShortcuts::instance().sequence(action);
        if (seq.isEmpty())
            continue;

        const QString trigger = AppShortcuts::toPortalTrigger(seq);
        if (trigger.isEmpty())
            continue;

        QVariantMap options;
        options.insert(QStringLiteral("description"), AppShortcuts::actionLabel(action));
        options.insert(QStringLiteral("preferred_trigger"), trigger);
        shortcuts.append({AppShortcuts::portalShortcutId(action), options});
    }
    return shortcuts;
}

QDBusObjectPath requestHandleFromReply(const QDBusMessage &reply, QString *errorOut)
{
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (errorOut)
            *errorOut = reply.errorMessage();
        return {};
    }
    if (reply.arguments().isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("Portal returned empty reply");
        return {};
    }
    return reply.arguments().constFirst().value<QDBusObjectPath>();
}

} // namespace

GlobalShortcutPortalLinux::GlobalShortcutPortalLinux(QObject *parent)
    : QObject(parent)
{
    registerPortalShortcutTypes();
}

GlobalShortcutPortalLinux::~GlobalShortcutPortalLinux()
{
    unbind();
}

bool GlobalShortcutPortalLinux::isAvailable()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    QDBusInterface portal(kPortalService, kPortalPath, kGlobalShortcutsIface, bus);
    return portal.isValid();
}

void GlobalShortcutPortalLinux::setHostWindow(QWindow *window)
{
    m_hostWindow = window;
}

QString GlobalShortcutPortalLinux::nextToken(const char *prefix) const
{
    return QString::fromLatin1("%1_%2").arg(QString::fromLatin1(prefix)).arg(
        QRandomGenerator::global()->generate());
}

QString GlobalShortcutPortalLinux::parentWindowHandle() const
{
    if (!m_hostWindow)
        return {};

    m_hostWindow->winId();

    const WId wid = m_hostWindow->winId();
    if (wid == 0)
        return {};

    const QString platform = QGuiApplication::platformName();
    if (platform.contains(QStringLiteral("wayland"), Qt::CaseInsensitive))
        return QStringLiteral("wayland:0x%1").arg(static_cast<quintptr>(wid), 0, 16);
    if (platform.contains(QStringLiteral("xcb"), Qt::CaseInsensitive)
        || platform.contains(QStringLiteral("x11"), Qt::CaseInsensitive))
        return QStringLiteral("x11:0x%1").arg(static_cast<quintptr>(wid), 0, 16);

    if (!qgetenv("WAYLAND_DISPLAY").isEmpty())
        return QStringLiteral("wayland:0x%1").arg(static_cast<quintptr>(wid), 0, 16);
    return QStringLiteral("x11:0x%1").arg(static_cast<quintptr>(wid), 0, 16);
}

void GlobalShortcutPortalLinux::connectActivatedSignal()
{
    QDBusConnection::sessionBus().connect(
        kPortalService, kPortalPath, kGlobalShortcutsIface, QStringLiteral("Activated"), this,
        SLOT(onPortalActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
}

void GlobalShortcutPortalLinux::disconnectActivatedSignal()
{
    QDBusConnection::sessionBus().disconnect(
        kPortalService, kPortalPath, kGlobalShortcutsIface, QStringLiteral("Activated"), this,
        SLOT(onPortalActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
}

void GlobalShortcutPortalLinux::closeSession()
{
    if (m_sessionPath.isEmpty())
        return;

    QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, m_sessionPath, kSessionIface,
                                                        QStringLiteral("Close"));
    QDBusConnection::sessionBus().call(msg);
    m_sessionPath.clear();
    m_bound = false;
}

void GlobalShortcutPortalLinux::unbind()
{
    disconnectActivatedSignal();
    clearPendingRequest();
    closeSession();
    m_bindingInProgress = false;
}

void GlobalShortcutPortalLinux::clearPendingRequest()
{
    if (m_pendingRequestPath.isEmpty())
        return;
    QDBusConnection::sessionBus().disconnect(
        kPortalService, m_pendingRequestPath, kRequestIface, QStringLiteral("Response"), this,
        SLOT(onPortalRequestResponse(uint,QVariantMap)));
    m_pendingRequestPath.clear();
    m_pendingRequest = PendingRequest::None;
}

void GlobalShortcutPortalLinux::watchPortalRequest(const QDBusObjectPath &requestPath,
                                                     PendingRequest kind)
{
    clearPendingRequest();
    m_pendingRequest = kind;
    m_pendingRequestPath = requestPath.path();
    QDBusConnection::sessionBus().connect(
        kPortalService, m_pendingRequestPath, kRequestIface, QStringLiteral("Response"), this,
        SLOT(onPortalRequestResponse(uint,QVariantMap)));
}

void GlobalShortcutPortalLinux::onPortalRequestResponse(uint response, const QVariantMap &results)
{
    const PendingRequest kind = m_pendingRequest;
    clearPendingRequest();

    if (kind == PendingRequest::CreateSession) {
        if (response != 0) {
            m_bindingInProgress = false;
            emit bindFailed(QStringLiteral("CreateSession rejected (code %1)").arg(response));
            return;
        }

        m_sessionPath = results.value(QStringLiteral("session_handle")).toString();
        if (m_sessionPath.isEmpty()) {
            m_bindingInProgress = false;
            emit bindFailed(QStringLiteral("CreateSession missing session_handle"));
            return;
        }

        beginBindShortcuts();
        return;
    }

    if (kind == PendingRequest::BindShortcuts) {
        m_bindingInProgress = false;
        if (response != 0) {
            emit bindFailed(QStringLiteral("BindShortcuts rejected (code %1)").arg(response));
            return;
        }

        m_bound = true;
        m_activationToken = results.value(QStringLiteral("activation_token")).toString();
        connectActivatedSignal();
        emit bindSucceeded();

        if (m_openConfigureAfterBind) {
            m_openConfigureAfterBind = false;
            QTimer::singleShot(150, this, &GlobalShortcutPortalLinux::beginConfigureShortcuts);
        }
        return;
    }

    if (kind == PendingRequest::ConfigureShortcuts) {
        if (response != 0) {
            emit configureUiFailed(
                QStringLiteral("ConfigureShortcuts rejected (code %1)").arg(response));
        }
    }
}

void GlobalShortcutPortalLinux::bind(bool requestConfigureUi)
{
    if (m_bindingInProgress)
        return;

    const PortalShortcutList shortcuts = buildPortalShortcutList();
    if (shortcuts.isEmpty()) {
        emit bindFailed(QStringLiteral("No shortcuts configured"));
        return;
    }

    m_openConfigureAfterBind = requestConfigureUi;
    m_activationToken.clear();
    unbind();
    m_openConfigureAfterBind = requestConfigureUi;
    m_bindingInProgress = true;
    beginCreateSession();
}

void GlobalShortcutPortalLinux::beginCreateSession()
{
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), nextToken("req"));
    options.insert(QStringLiteral("session_handle_token"), nextToken("ses"));

    QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, kPortalPath,
                                                      kGlobalShortcutsIface,
                                                      QStringLiteral("CreateSession"));
    msg << options;

    QString error;
    const QDBusObjectPath requestPath =
        requestHandleFromReply(QDBusConnection::sessionBus().call(msg), &error);
    if (requestPath.path().isEmpty()) {
        m_bindingInProgress = false;
        emit bindFailed(error.isEmpty() ? QStringLiteral("CreateSession failed") : error);
        return;
    }

    watchPortalRequest(requestPath, PendingRequest::CreateSession);
}

void GlobalShortcutPortalLinux::beginBindShortcuts()
{
    const PortalShortcutList shortcuts = buildPortalShortcutList();
    if (shortcuts.isEmpty() || m_sessionPath.isEmpty()) {
        m_bindingInProgress = false;
        emit bindFailed(QStringLiteral("No session or shortcuts"));
        return;
    }

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), nextToken("bind"));

    const QString parent = parentWindowHandle();
    QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, kPortalPath,
                                                      kGlobalShortcutsIface,
                                                      QStringLiteral("BindShortcuts"));
    msg << QDBusObjectPath(m_sessionPath) << QVariant::fromValue(shortcuts) << parent << options;

    QString error;
    const QDBusObjectPath requestPath =
        requestHandleFromReply(QDBusConnection::sessionBus().call(msg), &error);
    if (requestPath.path().isEmpty()) {
        m_bindingInProgress = false;
        emit bindFailed(error.isEmpty() ? QStringLiteral("BindShortcuts failed") : error);
        return;
    }

    watchPortalRequest(requestPath, PendingRequest::BindShortcuts);
}

void GlobalShortcutPortalLinux::beginConfigureShortcuts()
{
    if (m_sessionPath.isEmpty()) {
        emit configureUiFailed(QStringLiteral("No active portal session"));
        return;
    }

    const QString parent = parentWindowHandle();
    if (parent.isEmpty()) {
        emit configureUiFailed(QStringLiteral("Missing parent window for portal dialog"));
        return;
    }

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), nextToken("cfg"));
    if (!m_activationToken.isEmpty())
        options.insert(QStringLiteral("activation_token"), m_activationToken);

    QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, kPortalPath,
                                                      kGlobalShortcutsIface,
                                                      QStringLiteral("ConfigureShortcuts"));
    msg << QDBusObjectPath(m_sessionPath) << parent << options;

    QString error;
    const QDBusObjectPath requestPath =
        requestHandleFromReply(QDBusConnection::sessionBus().call(msg), &error);
    if (requestPath.path().isEmpty()) {
        emit configureUiFailed(error.isEmpty() ? QStringLiteral("ConfigureShortcuts failed") : error);
        return;
    }

    watchPortalRequest(requestPath, PendingRequest::ConfigureShortcuts);
}

void GlobalShortcutPortalLinux::openConfigureUi()
{
    if (m_sessionPath.isEmpty()) {
        bind(true);
        return;
    }

    beginConfigureShortcuts();
}

void GlobalShortcutPortalLinux::onPortalActivated(const QDBusObjectPath &sessionHandle,
                                                  const QString &shortcutId, qulonglong,
                                                  const QVariantMap &)
{
    if (sessionHandle.path() != m_sessionPath)
        return;
    emit shortcutActivated(shortcutId);
}
