#include "micsynccontroller.h"

#include "core/applog.h"
#include "core/i18n.h"
#include "core/playerengine.h"

#include <QCoreApplication>
#include <QElapsedTimer>

/** 平台后端：Linux 为 pactl loopback，Windows 为虚拟声卡，其它平台为 stub。 */
bool nekoMicSyncBackendAvailable();
bool nekoMicSyncBackendStart(QString *error);
void nekoMicSyncBackendStop();
void nekoMicSyncBackendSetPlayer(PlayerEngine *engine);
bool nekoMicSyncBackendInstallBundled();
QString nekoMicSyncBackendDeviceLabel();
QString nekoMicSyncBackendHintKey();

MicSyncController &MicSyncController::instance()
{
    static MicSyncController inst;
    return inst;
}

MicSyncController::MicSyncController(QObject *parent)
    : QObject(parent)
{
    if (QCoreApplication *app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, [this]() {
            if (m_enabled) {
                nekoMicSyncBackendStop();
                m_enabled = false;
            }
        });
    }
}

MicSyncController::~MicSyncController()
{
    if (m_enabled)
        nekoMicSyncBackendStop();
}

bool MicSyncController::isSupported()
{
    return nekoMicSyncBackendAvailable();
}

void MicSyncController::setPlayerEngine(PlayerEngine *engine)
{
    m_playerEngine = engine;
    nekoMicSyncBackendSetPlayer(engine);
}

QString MicSyncController::deviceName()
{
    return nekoMicSyncBackendDeviceLabel();
}

QString MicSyncController::hintKey()
{
    return nekoMicSyncBackendHintKey();
}

bool MicSyncController::installBundledDriver()
{
    return nekoMicSyncBackendInstallBundled();
}

void MicSyncController::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;

    // 切换音频输出设备会同步停/启 QAudioSink，播放中调用可能卡住 GUI 线程，
    // 因此这里记录耗时：热键卡死时能直接看出是不是这一步。
    QElapsedTimer timer;
    timer.start();
    qInfo().noquote() << QStringLiteral("[MicSync] 切换 -> %1")
                             .arg(enabled ? QStringLiteral("开") : QStringLiteral("关"));

    if (enabled) {
        QString error;
        if (!nekoMicSyncBackendStart(&error)) {
            emit failed(error.isEmpty()
                            ? I18n::instance().tr(QStringLiteral("micSyncFailed")).arg(deviceName())
                            : error);
            return;
        }
    } else {
        nekoMicSyncBackendStop();
    }

    m_enabled = enabled;
    emit enabledChanged(enabled);

    const qint64 cost = timer.elapsed();
    if (cost >= 200) {
        qWarning().noquote() << QStringLiteral("[MicSync] 切换耗时 %1，期间界面无响应")
                                    .arg(AppLog::msText(cost));
    } else {
        qInfo().noquote() << QStringLiteral("[MicSync] 切换完成 耗时%1").arg(AppLog::msText(cost));
    }
}

void MicSyncController::toggle()
{
    // The Windows backend may install its bundled virtual cable during start;
    // do not reject the request solely because the device is not present yet.
#if !defined(Q_OS_WIN)
    if (!m_enabled && !isSupported()) {
        emit failed(I18n::instance().tr(QStringLiteral("micSyncUnsupported")));
        return;
    }
#endif
    setEnabled(!m_enabled);
}
