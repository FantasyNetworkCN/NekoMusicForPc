#include "globalshortcutcontroller.h"
#include "globalshortcutcontroller_p.h"
#include "appshortcuts.h"

#include "core/applog.h"
#include "core/i18n.h"

#include <QShortcut>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <functional>

#if defined(Q_OS_WIN)
// 交叉编译时 Qt 目标宏由编译器给出，Windows 后端源文件只在 NEKO_TARGET_WINDOWS 下参与编译
#include "globalshortcutcontroller_win.h"
#endif

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
            if (seq.isEmpty() || !AppShortcuts::isActionSupported(action))
                return;
            auto *sc = new QShortcut(seq, m_parent);
            sc->setContext(Qt::ApplicationShortcut);
            connect(sc, &QShortcut::activated, this, [onAction, action]() { onAction(action); });
            m_shortcuts.append(sc);
        };

        const AppShortcuts &app = AppShortcuts::instance();
        for (int i = 0; i < AppShortcuts::ActionCount; ++i) {
            const auto action = static_cast<AppShortcuts::Action>(i);
            bind(app.sequence(action), action);
        }
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
#if defined(Q_OS_WIN)
    nekoGlobalShortcutWinInit(m_impl, this);
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

bool GlobalShortcutController::installInAppFallback()
{
    if (!m_impl->fallback)
        return false;

    m_impl->fallback->reload([this](AppShortcuts::Action action) {
        qInfo().noquote() << QStringLiteral("[Hotkey] 应用内快捷键触发 %1")
                                 .arg(AppShortcuts::actionLabel(action));
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
        case AppShortcuts::MicSync:
            emit micSyncTriggered();
            break;
        case AppShortcuts::ToggleDesktopLyrics:
            emit desktopLyricsToggleTriggered();
            break;
        default:
            break;
        }
    });
    if (!m_impl->fallback->hasShortcuts())
        return false;

    activateBackend(Backend::InAppFallback, true);
    return true;
}

void GlobalShortcutController::tryFallbackAfterPortalFailure(const QString &reason)
{
    if (installInAppFallback()) {
        emit bindingFailed(I18n::instance().tr(QStringLiteral("shortcutGlobalPortalFailed"))
                               .arg(reason));
        return;
    }

    m_active = false;
    m_backend = Backend::None;
    emit bindingFailed(reason);
}

void GlobalShortcutController::tryFallbackAfterWinFailure(const QString &reason)
{
    if (installInAppFallback()) {
        emit bindingFailed(I18n::instance().tr(QStringLiteral("shortcutGlobalWinFailed")).arg(reason));
        return;
    }

    m_active = false;
    m_backend = Backend::None;
    emit bindingFailed(reason);
}

void GlobalShortcutController::notifyBindingFailure(const QString &reason)
{
    emit bindingFailed(reason);
}

void GlobalShortcutController::start(bool requestConfigureUi)
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (nekoGlobalShortcutLinuxStartPortal(m_impl, this, requestConfigureUi))
        return;
#endif
#if defined(Q_OS_WIN)
    if (nekoGlobalShortcutWinStart(m_impl, this))
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
#if defined(Q_OS_WIN)
    nekoGlobalShortcutWinStop(m_impl);
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
    case Backend::WinRegister:
        return I18n::instance().tr(QStringLiteral("shortcutGlobalWinActive"));
    case Backend::InAppFallback:
        return I18n::instance().tr(QStringLiteral("shortcutGlobalFallbackActive"));
    default:
        return I18n::instance().tr(QStringLiteral("shortcutGlobalInactive"));
    }
}

void GlobalShortcutController::dispatchAction(const QString &portalId)
{
    const AppShortcuts::Action action = AppShortcuts::actionFromPortalId(portalId);
    // 全局热键在 GUI 线程分发：卡死之后回看日志时，这条是“用户按了哪个键”的起点。
    qInfo().noquote() << QStringLiteral("[Hotkey] 触发 %1").arg(AppShortcuts::actionLabel(action));
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
    case AppShortcuts::MicSync:
        emit micSyncTriggered();
        break;
    case AppShortcuts::ToggleDesktopLyrics:
        emit desktopLyricsToggleTriggered();
        break;
    default:
        break;
    }
}

void GlobalShortcutController::reportPortalConfigureFailed(const QString &reason)
{
    emit bindingFailed(I18n::instance().tr(QStringLiteral("shortcutGlobalConfigureFailed")).arg(reason));
}
