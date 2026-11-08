/**
 * @file thememanager.cpp
 * @brief 主题管理器实现
 */

#include "thememanager.h"
#include <QFile>
#include <QApplication>
#include <QStyleHints>
#include <QMap>

namespace Theme
{

ThemeManager &ThemeManager::instance()
{
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_settings()
{
    loadSettings();
    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
                if (m_mode == System)
                    emit themeChanged(m_mode);
            });
}

ThemeMode ThemeManager::currentMode() const
{
    return m_mode;
}

void ThemeManager::setMode(ThemeMode mode)
{
    if (m_mode == mode)
        return;

    m_mode = mode;
    saveSettings();
    emit themeChanged(mode);
}

QString ThemeManager::currentStyleSheet() const
{
    QString stylePath;
    
    switch (m_mode) {
    case System:
        // 检查系统主题
        if (QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark) {
            stylePath = ":/style.qss";
        } else {
            stylePath = ":/style-light.qss";
        }
        break;
    case Dark:
        stylePath = ":/style.qss";
        break;
    case Light:
        stylePath = ":/style-light.qss";
        break;
    }
    
    const bool dark = isDarkMode();
    QString components = loadStyleSheet(QStringLiteral(":/style-modern.qss"));
    const QMap<QString, QString> colors = {
        {"INK", dark ? "#EAF0FA" : "#182B45"},
        {"SECONDARY", dark ? "#A7B8CD" : "#526780"},
        {"MUTED", dark ? "#8193AB" : "#667C94"},
        {"ACCENT", dark ? "#8ADFF5" : "#1677B8"},
        {"ACCENTLINE", dark ? "rgba(100,205,240,65)" : "rgba(22,119,184,65)"},
        {"LINE", dark ? "rgba(190,215,248,26)" : "rgba(66,105,148,30)"},
        {"SURFACE", dark ? "rgba(32,46,68,135)" : "rgba(255,255,255,150)"},
        {"HOVER", dark ? "rgba(137,190,224,28)" : "rgba(255,255,255,220)"},
        {"SELECTED", dark ? "rgba(64,197,241,25)" : "rgba(22,119,184,22)"},
        {"POPUP", dark ? "#19243A" : "#F7FAFF"},
        {"SCROLL", dark ? "rgba(166,191,220,65)" : "rgba(67,96,130,65)"},
        {"CTA", dark ? "#B0E8F5" : "#1677B8"},
        {"CTATEXT", dark ? "#123044" : "#FFFFFF"},
    };
    for (auto it = colors.constBegin(); it != colors.constEnd(); ++it)
        components.replace(QStringLiteral("@") + it.key() + QStringLiteral("@"), it.value());
    return loadStyleSheet(stylePath) + QLatin1Char('\n') + components;
}

bool ThemeManager::isDarkMode() const
{
    switch (m_mode) {
    case System:
        return QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    case Dark:
        return true;
    case Light:
        return false;
    }
    return true;
}

void ThemeManager::loadSettings()
{
    const int mode = m_settings.value("themeMode", System).toInt();
    m_mode = mode >= System && mode <= Light ? static_cast<ThemeMode>(mode) : System;
}

void ThemeManager::saveSettings()
{
    m_settings.setValue("themeMode", static_cast<int>(m_mode));
}

QString ThemeManager::loadStyleSheet(const QString &path) const
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString::fromUtf8(f.readAll());
    return QString();
}

} // namespace Theme
