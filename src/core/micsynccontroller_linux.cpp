#include "micsynccontroller.h"

#include "core/playerengine.h"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QList>
#include <QMediaDevices>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>

namespace {

constexpr char kAppSink[] = "nekomusic_app_sink";
constexpr char kAppSinkDescription[] = "NekoMusicAppSink";
constexpr char kMixSink[] = "nekomusic_mic_bus";
constexpr char kMixDescription[] = "NekoMusicMicBus";
constexpr char kCombinedSource[] = "nekomusic_mic";
constexpr char kDeviceDescription[] = "NekoMusicMic";

struct PactlResult
{
    bool started = false;
    int exitCode = -1;
    QString out;
    QString err;

    bool ok() const { return started && exitCode == 0; }
};

PactlResult runPactl(const QStringList &args)
{
    PactlResult result;
    QProcess proc;
    proc.start(QStringLiteral("pactl"), args);
    if (!proc.waitForStarted(3000))
        return result;
    result.started = true;
    if (!proc.waitForFinished(8000)) {
        proc.kill();
        proc.waitForFinished(1000);
        result.err = QStringLiteral("pactl timeout");
        return result;
    }
    result.exitCode = proc.exitCode();
    result.out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    result.err = QString::fromLocal8Bit(proc.readAllStandardError());
    return result;
}

bool pactlAvailable()
{
    static const bool available = !QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty();
    return available;
}

PlayerEngine *&backendPlayer()
{
    static PlayerEngine *engine = nullptr;
    return engine;
}

QList<int> &loadedModules()
{
    static QList<int> modules;
    return modules;
}

/** 开启前的默认输入设备与播放器输出设备，关闭时还原。 */
QString &savedDefaultSource()
{
    static QString source;
    return source;
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

void unloadModule(int index)
{
    if (index >= 0)
        runPactl({QStringLiteral("unload-module"), QString::number(index)});
}

void unloadAllLoaded()
{
    QList<int> &modules = loadedModules();
    for (int i = modules.size() - 1; i >= 0; --i)
        unloadModule(modules[i]);
    modules.clear();
}

void restorePlayerOutput()
{
    if (backendPlayer() && hasSavedOutputDevice() && !savedOutputDevice().isNull())
        backendPlayer()->setOutputDevice(savedOutputDevice());
    hasSavedOutputDevice() = false;
    savedOutputDevice() = QAudioDevice();
}

/** 清理上次异常退出遗留的同名模块。 */
void removeLeftoverModules()
{
    const PactlResult result = runPactl({QStringLiteral("list"), QStringLiteral("short"),
                                         QStringLiteral("modules")});
    if (!result.ok())
        return;

    const QStringList lines = result.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char('\t'), Qt::SkipEmptyParts);
        if (fields.size() < 3)
            continue;
        const QString moduleName = fields.at(1);
        const QString args = fields.mid(2).join(QLatin1Char(' '));
        const bool ours =
            (moduleName == QStringLiteral("module-null-sink")
             && (args.contains(QLatin1String(kAppSink)) || args.contains(QLatin1String(kMixSink))))
            || (moduleName == QStringLiteral("module-loopback")
                && (args.contains(QLatin1String(kAppSink)) || args.contains(QLatin1String(kMixSink))))
            || (moduleName == QStringLiteral("module-remap-source")
                && args.contains(QStringLiteral("source_name=%1").arg(QLatin1String(kCombinedSource))));
        if (!ours)
            continue;
        bool ok = false;
        const int index = fields.at(0).toInt(&ok);
        if (ok)
            unloadModule(index);
    }
}

QString queryDefault(const QString &getArg, const QString &infoPrefix)
{
    const PactlResult direct = runPactl({getArg});
    if (direct.ok()) {
        const QString name = direct.out.trimmed();
        if (!name.isEmpty())
            return name;
    }

    const PactlResult info = runPactl({QStringLiteral("info")});
    if (!info.ok())
        return {};
    const QStringList lines = info.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(infoPrefix))
            return trimmed.section(QLatin1Char(':'), 1).trimmed();
    }
    return {};
}

QString defaultSinkName()
{
    return queryDefault(QStringLiteral("get-default-sink"), QStringLiteral("Default Sink:"));
}

/** 真实麦克风源：默认输入优先，若默认已是我们的混音源则退回第一个非 monitor 源。 */
QString realMicSource()
{
    const QString current = queryDefault(QStringLiteral("get-default-source"),
                                         QStringLiteral("Default Source:"));
    if (!current.isEmpty() && current != QLatin1String(kCombinedSource)
        && !current.endsWith(QLatin1String(".monitor")))
        return current;

    const PactlResult list = runPactl({QStringLiteral("list"), QStringLiteral("short"),
                                       QStringLiteral("sources")});
    if (!list.ok())
        return {};
    const QStringList lines = list.out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char('\t'), Qt::SkipEmptyParts);
        if (fields.size() < 2)
            continue;
        const QString name = fields.at(1);
        if (!name.endsWith(QLatin1String(".monitor")) && name != QLatin1String(kCombinedSource))
            return name;
    }
    return {};
}

QAudioDevice findAppSinkDevice()
{
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice &device : outputs) {
        if (device.description() == QLatin1String(kAppSinkDescription))
            return device;
    }
    return {};
}

/** 新建的 null sink 需要一点时间才会被 Qt 枚举到。 */
QAudioDevice waitForAppSinkDevice(int timeoutMs)
{
    QAudioDevice device = findAppSinkDevice();
    if (!device.isNull())
        return device;

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QThread::msleep(100);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        device = findAppSinkDevice();
        if (!device.isNull())
            return device;
    }
    return {};
}

} // namespace

bool nekoMicSyncBackendAvailable()
{
    return pactlAvailable();
}

void nekoMicSyncBackendSetPlayer(PlayerEngine *engine)
{
    backendPlayer() = engine;
}

QString nekoMicSyncBackendDeviceLabel()
{
    return QStringLiteral("NekoMusicMic");
}

QString nekoMicSyncBackendHintKey()
{
    return pactlAvailable() ? QStringLiteral("micSyncHint")
                            : QStringLiteral("micSyncUnsupportedHint");
}

bool nekoMicSyncBackendInstallBundled()
{
    return false;
}

bool nekoMicSyncBackendStart(QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!pactlAvailable())
        return fail(QStringLiteral("未找到 pactl，请安装 PulseAudio / pipewire-pulse"));
    if (!backendPlayer())
        return fail(QStringLiteral("播放器尚未就绪"));

    removeLeftoverModules();
    unloadAllLoaded();

    const QString speakerSink = defaultSinkName();
    if (speakerSink.isEmpty())
        return fail(QStringLiteral("无法获取默认音频输出设备"));

    const QString mic = realMicSource();
    if (mic.isEmpty())
        return fail(QStringLiteral("无法获取默认麦克风输入设备"));

    auto load = [&](const QStringList &args, const QString &what) -> int {
        const PactlResult result = runPactl(QStringList{QStringLiteral("load-module")} + args);
        bool ok = false;
        const int index = result.out.trimmed().toInt(&ok);
        if (!result.ok() || !ok) {
            const QString detail = result.err.trimmed();
            if (error)
                *error = detail.isEmpty() ? what : QStringLiteral("%1：%2").arg(what, detail);
            return -1;
        }
        return index;
    };

    auto rollback = [&]() {
        restorePlayerOutput();
        unloadAllLoaded();
        removeLeftoverModules();
    };

    // 1) 本应用专属输出总线：只有这个应用的声音会进入麦克风
    const int appSinkModule =
        load({QStringLiteral("module-null-sink"), QStringLiteral("sink_name=%1").arg(QLatin1String(kAppSink)),
              QStringLiteral("sink_properties=device.description=%1").arg(QLatin1String(kAppSinkDescription))},
             QStringLiteral("创建应用输出总线失败"));
    if (appSinkModule < 0) {
        rollback();
        return false;
    }
    loadedModules().append(appSinkModule);

    // 2) 应用声音仍然从扬声器放出来
    const int monitorModule =
        load({QStringLiteral("module-loopback"), QStringLiteral("source=%1.monitor").arg(QLatin1String(kAppSink)),
              QStringLiteral("sink=%1").arg(speakerSink), QStringLiteral("latency_msec=20"),
              QStringLiteral("source_dont_move=true"), QStringLiteral("sink_dont_move=true")},
             QStringLiteral("回放应用声音失败"));
    if (monitorModule < 0) {
        rollback();
        return false;
    }
    loadedModules().append(monitorModule);

    // 3) 麦克风混音总线（不接扬声器，避免啸叫）
    const int mixSinkModule =
        load({QStringLiteral("module-null-sink"), QStringLiteral("sink_name=%1").arg(QLatin1String(kMixSink)),
              QStringLiteral("sink_properties=device.description=%1").arg(QLatin1String(kMixDescription))},
             QStringLiteral("创建麦克风混音总线失败"));
    if (mixSinkModule < 0) {
        rollback();
        return false;
    }
    loadedModules().append(mixSinkModule);

    // 4) 本应用的音乐 -> 麦克风混音总线
    const int musicModule =
        load({QStringLiteral("module-loopback"), QStringLiteral("source=%1.monitor").arg(QLatin1String(kAppSink)),
              QStringLiteral("sink=%1").arg(QLatin1String(kMixSink)), QStringLiteral("latency_msec=20"),
              QStringLiteral("source_dont_move=true"), QStringLiteral("sink_dont_move=true")},
             QStringLiteral("把播放声音接入麦克风失败"));
    if (musicModule < 0) {
        rollback();
        return false;
    }
    loadedModules().append(musicModule);

    // 5) 真实麦克风 -> 麦克风混音总线
    const int micModule =
        load({QStringLiteral("module-loopback"), QStringLiteral("source=%1").arg(mic),
              QStringLiteral("sink=%1").arg(QLatin1String(kMixSink)), QStringLiteral("latency_msec=20"),
              QStringLiteral("source_dont_move=true"), QStringLiteral("sink_dont_move=true")},
             QStringLiteral("把麦克风接入混音总线失败"));
    if (micModule < 0) {
        rollback();
        return false;
    }
    loadedModules().append(micModule);

    // 6) 混音总线 -> 系统可见的麦克风
    const int remapModule =
        load({QStringLiteral("module-remap-source"),
              QStringLiteral("master=%1.monitor").arg(QLatin1String(kMixSink)),
              QStringLiteral("source_name=%1").arg(QLatin1String(kCombinedSource)),
              QStringLiteral("source_properties=device.description=%1").arg(QLatin1String(kDeviceDescription))},
             QStringLiteral("注册混音后的麦克风失败"));
    if (remapModule < 0) {
        rollback();
        return false;
    }
    loadedModules().append(remapModule);

    // 7) 把播放器输出切到专属总线，确保只同步本应用的声音
    const QAudioDevice appDevice = waitForAppSinkDevice(2000);
    if (appDevice.isNull()) {
        rollback();
        return fail(QStringLiteral("未找到应用输出设备，无法只同步本应用的声音"));
    }
    savedOutputDevice() = backendPlayer()->outputDevice();
    hasSavedOutputDevice() = true;
    backendPlayer()->setOutputDevice(appDevice);

    savedDefaultSource() = mic;
    runPactl({QStringLiteral("set-default-source"), QLatin1String(kCombinedSource)});
    runPactl({QStringLiteral("set-source-volume"), QLatin1String(kCombinedSource), QStringLiteral("100%")});
    return true;
}

void nekoMicSyncBackendStop()
{
    restorePlayerOutput();

    const QString previous = savedDefaultSource();
    if (!previous.isEmpty())
        runPactl({QStringLiteral("set-default-source"), previous});
    savedDefaultSource().clear();

    unloadAllLoaded();
    if (pactlAvailable())
        removeLeftoverModules();
}
