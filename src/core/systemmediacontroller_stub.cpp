#include "systemmediacontroller.h"
#include "core/playerengine.h"

#include <QWidget>

SystemMediaController::SystemMediaController(QObject *parent)
    : QObject(parent)
{
}

SystemMediaController::~SystemMediaController() = default;

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
