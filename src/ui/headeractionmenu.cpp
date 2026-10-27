#include "ui/headeractionmenu.h"

#include "theme/thememanager.h"
#include "ui/svgicon.h"

#include <QAction>

HeaderActionMenu::HeaderActionMenu(QWidget *parent)
    : QMenu(parent)
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    setStyleSheet(dark
                      ? QStringLiteral(
                            "QMenu { background: #2a2a2a; border: 1px solid rgba(255,255,255,0.1);"
                            " border-radius: 10px; padding: 6px 0; }"
                            "QMenu::item { color: #eee; padding: 9px 20px 9px 14px; }"
                            "QMenu::item:selected { background: rgba(230,57,80,0.2); }"
                            "QMenu::item:disabled { color: rgba(238,238,238,0.38); }"
                            "QMenu::separator { height: 1px; background: rgba(255,255,255,0.1);"
                            " margin: 6px 12px; }")
                      : QStringLiteral(
                            "QMenu { background: #fff; border: 1px solid rgba(33,37,41,0.12);"
                            " border-radius: 10px; padding: 6px 0; }"
                            "QMenu::item { color: #212529; padding: 9px 20px 9px 14px; }"
                            "QMenu::item:selected { background: rgba(230,57,80,0.12); }"
                            "QMenu::item:disabled { color: rgba(33,37,41,0.38); }"
                            "QMenu::separator { height: 1px; background: rgba(33,37,41,0.1);"
                            " margin: 6px 12px; }"));
}

QAction *HeaderActionMenu::addHeaderAction(const QString &text, const char *iconName, bool enabled)
{
    QAction *action = addAction(text);
    action->setEnabled(enabled);
    if (iconName && *iconName) {
        const bool dark = Theme::ThemeManager::instance().isDarkMode();
        const QColor color = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
        action->setIcon(Icons::renderNamed(iconName, 18, color));
    }
    return action;
}
