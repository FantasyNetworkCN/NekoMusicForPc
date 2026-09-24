#include "micsynccontroller.h"

#include "core/i18n.h"
#include "core/playerengine.h"

#include <QAudioDevice>
#include <QMediaDevices>
#include <QProcess>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QStringList>

namespace {

/**
 * Windows 无法向物理麦克风注入音频，需要第三方虚拟声卡：
 * 把本应用的播放输出切到虚拟声卡的播放端，用户再把对应的录音端选作麦克风。
 */
struct VirtualCable
{
    QAudioDevice render;  // 播放输出要切到的设备
    QString captureLabel; // 提示用户在语音软件里选择的录音设备名
};

const QStringList &cableNameHints()
{
    static const QStringList hints = {
        QStringLiteral("nekomusic mic"),
        QStringLiteral("nekomusic virtual"),
        QStringLiteral("cable input"),            // VB-Audio Virtual Cable
        QStringLiteral("voicemeeter input"),      // VoiceMeeter
        QStringLiteral("voicemeeter aux input"),
        QStringLiteral("voicemeeter vaio3 input"),
        QStringLiteral("virtual audio cable"),    // VAC / 其它
        QStringLiteral("line 1 (virtual audio cable)"),
    };
    return hints;
}

QString bundledInstallerPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("drivers/nekomic/install.bat")),
        QDir(appDir).filePath(QStringLiteral("drivers/nekomic/install.exe")),
        QDir(appDir).filePath(QStringLiteral("drivers/nekomic/VBCABLE_Setup_x64.exe")),
        QDir(appDir).filePath(QStringLiteral("VBCABLE_Setup_x64.exe")),
        QDir(appDir).filePath(QStringLiteral("nekomic-install.exe")),
    };
    for (const QString &path : candidates) {
        if (QFileInfo(path).isFile())
            return path;
    }
    return {};
}

bool installBundledVirtualCable()
{
    const QString installer = bundledInstallerPath();
    if (installer.isEmpty())
        return false;
    // The bundled installer must request elevation itself (or be an elevated
    // helper executable). startDetached keeps the UI responsive while Windows
    // shows the normal UAC prompt.
    return QProcess::startDetached(installer, {});
}

bool nekoMicSyncBackendInstallBundled()
{
    return installBundledVirtualCable();
}

bool looksLikeVirtualCable(const QString &name)
{
    const QString lower = name.toLower();
    for (const QString &hint : cableNameHints()) {
        if (lower.contains(hint))
            return true;
    }
    return false;
}

VirtualCable findVirtualCable()
{
    VirtualCable cable;
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice &device : outputs) {
        if (looksLikeVirtualCable(device.description())) {
            cable.render = device;
            break;
        }
    }
    if (cable.render.isNull())
        return cable;

    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    QString wanted = cable.render.description();
    const int inputPos = wanted.lastIndexOf(QStringLiteral("Input"), -1, Qt::CaseInsensitive);
    if (inputPos >= 0)
        wanted.replace(inputPos, 5, QStringLiteral("Output"));

    for (const QAudioDevice &device : inputs) {
        if (device.description().compare(wanted, Qt::CaseInsensitive) == 0) {
            cable.captureLabel = device.description();
            return cable;
        }
    }
    for (const QAudioDevice &device : inputs) {
        const QString description = device.description();
        if (looksLikeVirtualCable(description)
            && description.contains(QStringLiteral("Output"), Qt::CaseInsensitive)) {
            cable.captureLabel = description;
            return cable;
        }
    }
    for (const QAudioDevice &device : inputs) {
        if (looksLikeVirtualCable(device.description())) {
            cable.captureLabel = device.description();
            return cable;
        }
    }
    return cable;
}

PlayerEngine *&playerEngine()
{
    static PlayerEngine *engine = nullptr;
    return engine;
}

QAudioDevice &savedOutputDevice()
{
    static QAudioDevice device;
    return device;
}

bool &hasSavedOutputDevice()
{
    static bool saved = false;
    return saved;
}

} // namespace

bool nekoMicSyncBackendAvailable()
{
    return !findVirtualCable().render.isNull();
}

void nekoMicSyncBackendSetPlayer(PlayerEngine *engine)
{
    playerEngine() = engine;
}

QString nekoMicSyncBackendDeviceLabel()
{
    const VirtualCable cable = findVirtualCable();
    if (!cable.captureLabel.isEmpty())
        return cable.captureLabel;
    return QStringLiteral("Virtual Audio Cable");
}

QString nekoMicSyncBackendHintKey()
{
    return QStringLiteral("micSyncHintWindows");
}

bool nekoMicSyncBackendStart(QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!playerEngine())
        return fail(I18n::instance().tr(QStringLiteral("micSyncFailed")).arg(QStringLiteral("player")));

    VirtualCable cable = findVirtualCable();
    if (cable.render.isNull()) {
        if (installBundledVirtualCable()) {
            return fail(I18n::instance().tr(QStringLiteral("micSyncInstallPending")));
        }
    }
    if (cable.render.isNull())
        return fail(I18n::instance().tr(QStringLiteral("micSyncWindowsNoCable")));

    if (!hasSavedOutputDevice()) {
        savedOutputDevice() = playerEngine()->outputDevice();
        hasSavedOutputDevice() = true;
    }
    playerEngine()->setOutputDevice(cable.render);
    return true;
}

void nekoMicSyncBackendStop()
{
    if (playerEngine() && hasSavedOutputDevice() && !savedOutputDevice().isNull())
        playerEngine()->setOutputDevice(savedOutputDevice());
    hasSavedOutputDevice() = false;
    savedOutputDevice() = QAudioDevice();
}
