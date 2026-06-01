#include "globalshortcutcontroller_linux.h"
#include "globalshortcutcontroller.h"
#include "globalshortcutcontroller_p.h"
#include "globalshortcutportal_linux.h"
#include "core/i18n.h"

void nekoGlobalShortcutLinuxInitPortal(GlobalShortcutControllerBackendImpl *impl,
                                       GlobalShortcutController *controller)
{
    if (!impl || !controller || impl->portalLinux)
        return;

    auto *portal = new GlobalShortcutPortalLinux(controller);
    impl->portalLinux = portal;

    QObject::connect(portal, &GlobalShortcutPortalLinux::shortcutActivated, controller,
                     &GlobalShortcutController::dispatchAction);
    QObject::connect(portal, &GlobalShortcutPortalLinux::bindSucceeded, controller, [controller]() {
        controller->activateBackend(GlobalShortcutController::Backend::Portal, true);
    });
    QObject::connect(portal, &GlobalShortcutPortalLinux::bindFailed, controller,
                     &GlobalShortcutController::tryFallbackAfterPortalFailure);
    QObject::connect(portal, &GlobalShortcutPortalLinux::configureUiFailed, controller,
                     &GlobalShortcutController::reportPortalConfigureFailed);
}

void nekoGlobalShortcutLinuxSetPortalHost(GlobalShortcutControllerBackendImpl *impl, QWindow *window)
{
    if (!impl || !impl->portalLinux)
        return;
    static_cast<GlobalShortcutPortalLinux *>(impl->portalLinux)->setHostWindow(window);
}

bool nekoGlobalShortcutLinuxStartPortal(GlobalShortcutControllerBackendImpl *impl,
                                        GlobalShortcutController *controller,
                                        bool requestConfigureUi)
{
    if (!impl || !impl->portalLinux || !controller)
        return false;
    auto *portal = static_cast<GlobalShortcutPortalLinux *>(impl->portalLinux);
    if (!GlobalShortcutPortalLinux::isAvailable())
        return false;
    controller->prepareHostWindowForPortal();
    portal->bind(requestConfigureUi);
    return true;
}

void nekoGlobalShortcutLinuxStopPortal(GlobalShortcutControllerBackendImpl *impl)
{
    if (!impl || !impl->portalLinux)
        return;
    static_cast<GlobalShortcutPortalLinux *>(impl->portalLinux)->unbind();
}

void nekoGlobalShortcutLinuxOpenPortalConfigure(GlobalShortcutControllerBackendImpl *impl,
                                                  GlobalShortcutController *controller)
{
    if (!impl || !impl->portalLinux || !controller)
        return;
    controller->prepareHostWindowForPortal();
    static_cast<GlobalShortcutPortalLinux *>(impl->portalLinux)->openConfigureUi();
}
