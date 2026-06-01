#pragma once

class QWindow;
class GlobalShortcutController;
struct GlobalShortcutControllerBackendImpl;

void nekoGlobalShortcutLinuxInitPortal(GlobalShortcutControllerBackendImpl *impl,
                                       GlobalShortcutController *controller);
void nekoGlobalShortcutLinuxSetPortalHost(GlobalShortcutControllerBackendImpl *impl, QWindow *window);
bool nekoGlobalShortcutLinuxStartPortal(GlobalShortcutControllerBackendImpl *impl,
                                        GlobalShortcutController *controller,
                                        bool requestConfigureUi);
void nekoGlobalShortcutLinuxStopPortal(GlobalShortcutControllerBackendImpl *impl);
void nekoGlobalShortcutLinuxOpenPortalConfigure(GlobalShortcutControllerBackendImpl *impl,
                                                GlobalShortcutController *controller);
