#include "systemmediacontroller.h"
#include "core/playerengine.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
class WinAppCommandFilter : public QAbstractNativeEventFilter
{
public:
    explicit WinAppCommandFilter(SystemMediaController *c)
        : m_ctrl(c)
    {
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        if (!m_ctrl || eventType != "windows_generic_MSG")
            return false;
        auto *msg = static_cast<MSG *>(message);
        if (msg->message != WM_APPCOMMAND)
            return false;
        const int cmd = GET_APPCOMMAND_LPARAM(msg->lParam);
        switch (cmd) {
        case APPCOMMAND_MEDIA_PLAY_PAUSE:
            m_ctrl->firePlayPause();
            break;
        case APPCOMMAND_MEDIA_STOP:
            m_ctrl->fireStop();
            break;
        case APPCOMMAND_MEDIA_NEXTTRACK:
            m_ctrl->fireNext();
            break;
        case APPCOMMAND_MEDIA_PREVIOUSTRACK:
            m_ctrl->firePrevious();
            break;
        default:
            return false;
        }
        if (result)
            *result = TRUE;
        return true;
    }

private:
    SystemMediaController *m_ctrl = nullptr;
};
#endif

struct WinMediaNativeState {
#ifdef Q_OS_WIN
    WinAppCommandFilter *filter = nullptr;
#endif
};

} // namespace

SystemMediaController::SystemMediaController(QObject *parent)
    : QObject(parent)
{
    auto *st = new WinMediaNativeState;
#ifdef Q_OS_WIN
    st->filter = new WinAppCommandFilter(this);
    if (QCoreApplication *app = QCoreApplication::instance())
        app->installNativeEventFilter(st->filter);
#endif
    m_winOpaque = st;
}

SystemMediaController::~SystemMediaController()
{
    if (m_winOpaque) {
        auto *st = static_cast<WinMediaNativeState *>(m_winOpaque);
#ifdef Q_OS_WIN
        if (st->filter) {
            if (QCoreApplication *app = QCoreApplication::instance())
                app->removeNativeEventFilter(st->filter);
            delete st->filter;
        }
#endif
        delete st;
        m_winOpaque = nullptr;
    }
}

void SystemMediaController::setHostWindow(QWidget *host)
{
    m_hostWindow = host;
}

void SystemMediaController::setPlayerEngine(PlayerEngine *engine)
{
    m_engine = engine;
}

void SystemMediaController::updateFromEngineState(PlayerEngine::PlaybackState state)
{
    switch (state) {
    case PlayerEngine::Playing:
        m_playbackStatus = QStringLiteral("Playing");
        break;
    case PlayerEngine::Paused:
        m_playbackStatus = QStringLiteral("Paused");
        break;
    default:
        m_playbackStatus = QStringLiteral("Stopped");
        break;
    }
    Q_UNUSED(m_positionTimer);
}

void SystemMediaController::updateMetadata(const MusicInfo &music, qint64 durationMs)
{
    Q_UNUSED(music);
    Q_UNUSED(durationMs);
}

void SystemMediaController::updateCapabilities(bool canNext, bool canPrev, bool canSeek)
{
    m_canGoNext = canNext;
    m_canGoPrevious = canPrev;
    m_canSeek = canSeek;
}

void SystemMediaController::updateLoopShuffle(const QString &playMode)
{
    QString loop = QStringLiteral("None");
    bool shuf = false;
    if (playMode == QStringLiteral("single"))
        loop = QStringLiteral("Track");
    else if (playMode == QStringLiteral("list"))
        loop = QStringLiteral("Playlist");
    else if (playMode == QStringLiteral("random"))
        shuf = true;
    m_loopStatus = loop;
    m_shuffle = shuf;
    Q_UNUSED(m_loopStatus);
    Q_UNUSED(m_shuffle);
}

void SystemMediaController::syncVolumeFromEngine(double volume01)
{
    m_volume = qBound(0.0, volume01, 1.0);
}

void SystemMediaController::notifySeeked(qlonglong positionUs)
{
    Q_UNUSED(positionUs);
}

void SystemMediaController::onPositionMsChanged(qint64 positionMs)
{
    Q_UNUSED(positionMs);
}
