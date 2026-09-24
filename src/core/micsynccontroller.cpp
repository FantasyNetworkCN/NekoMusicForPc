#include "micsynccontroller.h"

#include "core/i18n.h"
#include "core/playerengine.h"

#include <QCoreApplication>

/** 平台后端：Linux 为 pactl loopback，Windows 为虚拟声卡，其它平台为 stub。 */
bool nekoMicSyncBackendAvailable();
bool nekoMicSyncBackendStart(QString *error);
void nekoMicSyncBackendStop();
void nekoMicSyncBackendSetPlayer(PlayerEngine *engine);
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

void MicSyncController::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;

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
