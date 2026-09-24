#include "micsynccontroller.h"

#include "core/playerengine.h"

bool nekoMicSyncBackendAvailable()
{
    return false;
}

void nekoMicSyncBackendSetPlayer(PlayerEngine *)
{
}

QString nekoMicSyncBackendDeviceLabel()
{
    return QStringLiteral("NekoMusicMic");
}

QString nekoMicSyncBackendHintKey()
{
    return QStringLiteral("micSyncUnsupportedHint");
}

bool nekoMicSyncBackendInstallBundled()
{
    return false;
}

bool nekoMicSyncBackendStart(QString *error)
{
    if (error)
        *error = QStringLiteral("当前平台暂不支持麦克风同步");
    return false;
}

void nekoMicSyncBackendStop()
{
}
