#include "globalshortcutcontroller.h"
#include "globalshortcutcontroller_p.h"
#include "appshortcuts.h"

#include "core/i18n.h"

#include <QShortcut>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <functional>

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
struct GlobalShortcutControllerBackendImpl;
class GlobalShortcutController;
void nekoGlobalShortcutLinuxInitPortal(GlobalShortcutControllerBackendImpl *impl,
                                       GlobalShortcutController *controller);
void nekoGlobalShortcutLinuxSetPortalHost(GlobalShortcutControllerBackendImpl *impl, QWindow *window);
bool nekoGlobalShortcutLinuxStartPortal(GlobalShortcutControllerBackendImpl *impl,
                                        GlobalShortcutController *controller,
                                        bool requestConfigureUi);
void nekoGlobalShortcutLinuxStopPortal(GlobalShortcutControllerBackendImpl *impl);
void nekoGlobalShortcutLinuxOpenPortalConfigure(GlobalShortcutControllerBackendImpl *impl,
                                                GlobalShortcutController *controller);
#endif

class InAppFallbackBackend final : public QObject
{
public:
    explicit InAppFallbackBackend(QWidget *parentWidget)
        : QObject(parentWidget), m_parent(parentWidget)
    {
    }

    void reload(const std::function<void(AppShortcuts::Action)> &onAction)
    {
        qDeleteAll(m_shortcuts);
        m_shortcuts.clear();

        auto bind = [this, &onAction](const QKeySequence &seq, AppShortcuts::Action action) {
            if (seq.isEmpty())
                return;
            auto *sc = new QShortcut(seq, m_parent);
            sc->setContext(Qt::ApplicationShortcut);
            connect(sc, &QShortcut::activated, this, [onAction, action]() { onAction(action); });
            m_shortcuts.append(sc);
        };

        const AppShortcuts &app = AppShortcuts::instance();
        bind(app.sequence(AppShortcuts::PlayPause), AppShortcuts::PlayPause);
        bind(app.sequence(AppShortcuts::NextTrack), AppShortcuts::NextTrack);
        bind(app.sequence(AppShortcuts::PreviousTrack), AppShortcuts::PreviousTrack);
    }

    void stop()
    {
        qDeleteAll(m_shortcuts);
        m_shortcuts.clear();
    }

    bool hasShortcuts() const { return !m_shortcuts.isEmpty(); }

private:
    QWidget *m_parent;
    QList<QShortcut *> m_shortcuts;
};

namespace {

bool isWaylandSession()
{
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
    if (platform.contains("wayland"))
        return true;
    return !qgetenv("WAYLAND_DISPLAY").isEmpty();
}

} // namespace

struct GlobalShortcutController::BackendImpl : GlobalShortcutControllerBackendImpl {
};

GlobalShortcutController &GlobalShortcutController::instance()
{
    static GlobalShortcutController inst;
    return inst;
}

GlobalShortcutController::GlobalShortcutController(QObject *parent)
    : QObject(parent), m_impl(new BackendImpl)
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    nekoGlobalShortcutLinuxInitPortal(m_impl, this);
#endif

    m_settingsRebindTimer = new QTimer(this);
    m_settingsRebindTimer->setSingleShot(true);
    m_settingsRebindTimer->setInterval(400);
    connect(m_settingsRebindTimer, &QTimer::timeout, this,
            &GlobalShortcutController::performSettingsRebind);
}

void GlobalShortcutController::installFallback(QWidget *parentWidget)
{
    if (!parentWidget || m_impl->fallback)
        return;
    m_impl->fallback = new InAppFallbackBackend(parentWidget);
}

void GlobalShortcutController::setHostWindow(QWindow *window)
{
    m_hostWindow = window;
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    nekoGlobalShortcutLinuxSetPortalHost(m_impl, window);
#endif
}

void GlobalShortcutController::prepareHostWindowForPortal()
{
    if (!m_hostWindow)
        return;

    m_hostWindow->winId();
    m_hostWindow->raise();
    m_hostWindow->requestActivate();
}

void GlobalShortcutController::activateBackend(Backend backend, bool active)
{
    m_backend = backend;
    m_active = active;
    emit bindingStateChanged(active, backend);
}

void GlobalShortcutController::tryFallbackAfterPortalFailure(const QString &reason)
{
    if (m_impl->fallback) {
        m_impl->fallback->reload([this](AppShortcuts::Action action) {
            switch (action) {
            case AppShortcuts::PlayPause:
                emit playPauseTriggered();
                break;
            case AppShortcuts::NextTrack:
                emit nextTrackTriggered();
                break;
            case AppShortcuts::PreviousTrack:
                emit previousTrackTriggered();
                break;
            default:
                break;
            }
        });
        if (m_impl->fallback->hasShortcuts()) {
            activateBackend(Backend::InAppFallback, true);
            emit bindingFailed(I18n::instance().tr(QStringLiteral("shortcutGlobalPortalFailed"))
                                   .arg(reason));
            return;
        }
    }

    m_active = false;
    m_backend = Backend::None;
    emit bindingFailed(reason);
}

void GlobalShortcutController::start(bool requestConfigureUi)
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (nekoGlobalShortcutLinuxStartPortal(m_impl, this, requestConfigureUi))
        return;
#endif

    tryFallbackAfterPortalFailure(I18n::instance().tr(QStringLiteral("shortcutGlobalUnavailable")));
}

void GlobalShortcutController::rebind(bool requestSystemPermission)
{
    stop();
    QTimer::singleShot(0, this, [this, requestSystemPermission]() {
        start(requestSystemPermission && isWaylandSession());
    });
}

void GlobalShortcutController::scheduleRebindAfterSettingsChange()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (!isWaylandSession()) {
        rebind(false);
        return;
    }
    m_settingsRebindTimer->start();
#else
    rebind(false);
#endif
}

void GlobalShortcutController::performSettingsRebind()
{
    stop();
    prepareHostWindowForPortal();
    start(true);
}

void GlobalShortcutController::stop()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    nekoGlobalShortcutLinuxStopPortal(m_impl);
#endif
    if (m_impl->fallback)
        m_impl->fallback->stop();

    m_active = false;
    m_backend = Backend::None;
}

void GlobalShortcutController::openSystemConfigureUi()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    nekoGlobalShortcutLinuxOpenPortalConfigure(m_impl, this);
#endif
}

QString GlobalShortcutController::statusText() const
{
    switch (m_backend) {
    case Backend::Portal:
        return I18n::instance().tr(QStringLiteral("shortcutGlobalPortalActive"));
    case Backend::InAppFallback:
        return I18n::instance().tr(QStringLiteral("shortcutGlobalFallbackActive"));
    default:
        return I18n::instance().tr(QStringLiteral("shortcutGlobalInactive"));
    }
}

void GlobalShortcutController::dispatchAction(const QString &portalId)
{
    switch (AppShortcuts::actionFromPortalId(portalId)) {
    case AppShortcuts::PlayPause:
        emit playPauseTriggered();
        break;
    case AppShortcuts::NextTrack:
        emit nextTrackTriggered();
        break;
    case AppShortcuts::PreviousTrack:
        emit previousTrackTriggered();
        break;
    default:
        break;
    }
}

void GlobalShortcutController::reportPortalConfigureFailed(const QString &reason)
{
    emit bindingFailed(I18n::instance().tr(QStringLiteral("shortcutGlobalConfigureFailed")).arg(reason));
}
