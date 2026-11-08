#pragma once

#include "theme/theme.h"
#include "theme/thememanager.h"

#include <QColor>
#include <QString>

namespace AuthDialogChrome {

constexpr int kDialogWidth = 820;
constexpr int kCompactDialogWidth = 420;
constexpr int kDialogHeight = 460;
constexpr int kOuterPad = 12;
constexpr int kCardPadH = 42;
constexpr int kCardPadV = 24;
constexpr int kSectionSpacing = 12;
constexpr int kFieldSpacing = 8;
constexpr int kFieldHeight = 52;
constexpr int kPrimaryBtnHeight = 52;
constexpr int kLinkBtnHeight = 32;

struct Palette {
    QString cardBg;
    QString cardBorder;
    QString titleColor;
    QString bodyColor;
    QString msgColor;
    QString accent;
    QString accentHover;
    QString buttonText;
};

inline Palette palette(bool dark)
{
    if (dark) {
        return {
            QStringLiteral("#19243A"),
            QStringLiteral("#344660"),
            QStringLiteral("#EAF0FA"),
            QStringLiteral("#A7B8CD"),
            QStringLiteral("#F28B82"),
            QStringLiteral("#8ADFF5"),
            QStringLiteral("#B4EEFC"),
            QStringLiteral("#123044"),
        };
    }
    return {
        QStringLiteral("#F7FAFF"),
        QStringLiteral("#D1DFED"),
        QStringLiteral("#182B45"),
        QStringLiteral("#526780"),
        QStringLiteral("#B3261E"),
        QStringLiteral("#1677B8"),
        QStringLiteral("#218CC8"),
        QStringLiteral("#FFFFFF"),
    };
}

inline Palette currentPalette()
{
    return palette(Theme::ThemeManager::instance().isDarkMode());
}

inline QString cardStyleSheet(const Palette &p)
{
    return QStringLiteral(
               "QWidget#authDialogCard {"
               "  background: %1;"
               "  border: 1px solid %2;"
               "  border-radius: 24px;"
               "}")
        .arg(p.cardBg, p.cardBorder);
}

inline QString titleStyleSheet(const Palette &p)
{
    return QStringLiteral(
               "QLabel { color: %1; font-size: 25px; font-weight: 700; padding: 0; }")
        .arg(p.titleColor);
}

inline QString subtitleStyleSheet(const Palette &p)
{
    return QStringLiteral(
               "QLabel { color: %1; font-size: 13px; padding: 0; }")
        .arg(p.bodyColor);
}

inline QString bodyStyleSheet(const Palette &p)
{
    return QStringLiteral("QLabel { color: %1; font-size: 13px; }").arg(p.bodyColor);
}

inline QString msgStyleSheet(const QString &color)
{
    return QStringLiteral("QLabel { color: %1; font-size: 13px; min-height: 20px; }").arg(color);
}

inline QString controlsStyleSheet(const Palette &p)
{
    return QStringLiteral(
               "QLineEdit#dialogEdit { background: transparent; color: %1; border: 1px solid %2;"
               " padding: 0 16px; border-radius: 12px; font-size: 14px; }"
               "QLineEdit#dialogEdit:focus { border: 2px solid %3; }"
               "QPushButton#dialogBtn { background: %4; color: %8; border: none;"
               " border-radius: 12px; font-size: 14px; font-weight: 700; }"
               "QPushButton#dialogBtn:hover { background: %5; }"
               "QPushButton#dialogBtn:disabled { background: rgba(128,128,128,90); color: rgba(255,255,255,150); }"
               "QPushButton#dialogSecondaryBtn { background: transparent; color: %3; border: 1px solid %3;"
               " border-radius: 12px; font-size: 14px; font-weight: 600; }"
               "QPushButton#dialogSecondaryBtn:hover { background: rgba(26,115,232,18); }"
               "QPushButton#dialogLinkBtn { background: transparent; border: none; color: %6; font-size: 13px; }"
               "QPushButton#dialogLinkBtn:hover { color: %3; }"
               "QPushButton#dialogCloseBtn { background: transparent; border: none; font-size: 25px; color: %6; }"
               "QPushButton#dialogCloseBtn:hover { color: %3; }"
               "QLabel#authBrandName { color: %1; font-size: 16px; font-weight: 700; }"
               "QLabel#authFieldLabel { color: %7; font-size: 12px; font-weight: 600; padding-top: 2px; }")
        .arg(p.titleColor, p.cardBorder, p.accent, p.accent, p.accentHover, p.bodyColor, p.bodyColor, p.buttonText);
}

} // namespace AuthDialogChrome
