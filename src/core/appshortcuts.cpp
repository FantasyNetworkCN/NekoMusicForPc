#include "appshortcuts.h"

#include "core/i18n.h"

#include <QKeyCombination>
#include <QSettings>

namespace {

constexpr const char *kGroup = "shortcuts";

} // namespace

AppShortcuts &AppShortcuts::instance()
{
    static AppShortcuts inst;
    return inst;
}

AppShortcuts::AppShortcuts(QObject *parent)
    : QObject(parent)
{
    resetToDefaults();
}

QKeySequence AppShortcuts::defaultSequence(Action action)
{
    switch (action) {
    case PlayPause:
        return QKeySequence(QStringLiteral("Ctrl+P"));
    case NextTrack:
        return QKeySequence(QStringLiteral("Ctrl+Alt+Right"));
    case PreviousTrack:
        return QKeySequence(QStringLiteral("Ctrl+Alt+Left"));
    case ActionCount:
        break;
    }
    return {};
}

QString AppShortcuts::portalShortcutId(Action action)
{
    switch (action) {
    case PlayPause:
        return QStringLiteral("play_pause");
    case NextTrack:
        return QStringLiteral("next_track");
    case PreviousTrack:
        return QStringLiteral("previous_track");
    case ActionCount:
        break;
    }
    return {};
}

QString AppShortcuts::actionLabel(Action action)
{
    switch (action) {
    case PlayPause:
        return I18n::instance().tr(QStringLiteral("shortcutPlayPause"));
    case NextTrack:
        return I18n::instance().tr(QStringLiteral("shortcutNextTrack"));
    case PreviousTrack:
        return I18n::instance().tr(QStringLiteral("shortcutPreviousTrack"));
    case ActionCount:
        break;
    }
    return {};
}

AppShortcuts::Action AppShortcuts::actionFromPortalId(const QString &id)
{
    for (int i = 0; i < ActionCount; ++i) {
        if (portalShortcutId(static_cast<Action>(i)) == id)
            return static_cast<Action>(i);
    }
    return ActionCount;
}

namespace {

QString portalKeyFromQtKey(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QString(QChar('a' + (key - Qt::Key_A)));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return QString(QChar('0' + (key - Qt::Key_0)));

    switch (key) {
    case Qt::Key_Space:
        return QStringLiteral("space");
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QStringLiteral("Return");
    case Qt::Key_Escape:
        return QStringLiteral("Escape");
    case Qt::Key_Tab:
        return QStringLiteral("Tab");
    case Qt::Key_Backspace:
        return QStringLiteral("BackSpace");
    case Qt::Key_Comma:
        return QStringLiteral("comma");
    case Qt::Key_Period:
        return QStringLiteral("period");
    case Qt::Key_Minus:
        return QStringLiteral("minus");
    case Qt::Key_Equal:
        return QStringLiteral("equal");
    case Qt::Key_Semicolon:
        return QStringLiteral("semicolon");
    case Qt::Key_Apostrophe:
        return QStringLiteral("apostrophe");
    case Qt::Key_BracketLeft:
        return QStringLiteral("bracketleft");
    case Qt::Key_BracketRight:
        return QStringLiteral("bracketright");
    case Qt::Key_Backslash:
        return QStringLiteral("backslash");
    case Qt::Key_Slash:
        return QStringLiteral("slash");
    case Qt::Key_QuoteLeft:
        return QStringLiteral("grave");
    case Qt::Key_Less:
        return QStringLiteral("less");
    case Qt::Key_Greater:
        return QStringLiteral("greater");
    case Qt::Key_Left:
        return QStringLiteral("Left");
    case Qt::Key_Right:
        return QStringLiteral("Right");
    default:
        break;
    }
    return {};
}

} // namespace

QString AppShortcuts::toPortalTrigger(const QKeySequence &seq)
{
    if (seq.isEmpty())
        return {};

    const QKeyCombination combo = seq[0];
    const int key = combo.key();
    if (key == 0 || key == Qt::Key_unknown)
        return {};

    QStringList parts;
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    if (mods & Qt::ControlModifier)
        parts << QStringLiteral("CTRL");
    if (mods & Qt::AltModifier)
        parts << QStringLiteral("ALT");
    if (mods & Qt::ShiftModifier)
        parts << QStringLiteral("SHIFT");
    if (mods & Qt::MetaModifier)
        parts << QStringLiteral("LOGO");

    const QString keyName = portalKeyFromQtKey(key);
    if (keyName.isEmpty())
        return {};
    parts << keyName;
    return parts.join(QLatin1Char('+'));
}

QString AppShortcuts::settingsKey(Action action)
{
    switch (action) {
    case PlayPause:
        return QStringLiteral("playPause");
    case NextTrack:
        return QStringLiteral("nextTrack");
    case PreviousTrack:
        return QStringLiteral("previousTrack");
    case ActionCount:
        break;
    }
    return {};
}

QKeySequence AppShortcuts::sequence(Action action) const
{
    if (action < 0 || action >= ActionCount)
        return {};
    return m_sequences[action];
}

void AppShortcuts::setSequence(Action action, const QKeySequence &seq)
{
    if (action < 0 || action >= ActionCount)
        return;
    if (m_sequences[action] == seq)
        return;
    m_sequences[action] = seq;
    save();
    emit shortcutsChanged();
}

void AppShortcuts::resetToDefaults()
{
    for (int i = 0; i < ActionCount; ++i)
        m_sequences[i] = defaultSequence(static_cast<Action>(i));
}

void AppShortcuts::resetAllAndSave()
{
    resetToDefaults();
    save();
    emit shortcutsChanged();
}

void AppShortcuts::load()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kGroup));
    for (int i = 0; i < ActionCount; ++i) {
        const auto action = static_cast<Action>(i);
        const QString key = settingsKey(action);
        const QString stored = settings.value(key).toString();
        if (stored.isEmpty())
            m_sequences[i] = defaultSequence(action);
        else
            m_sequences[i] = QKeySequence(stored);
    }
    settings.endGroup();
}

void AppShortcuts::save()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kGroup));
    for (int i = 0; i < ActionCount; ++i) {
        const auto action = static_cast<Action>(i);
        settings.setValue(settingsKey(action), m_sequences[i].toString());
    }
    settings.endGroup();
}
