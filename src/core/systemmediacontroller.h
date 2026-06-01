#pragma once

#include "core/musicinfo.h"
#include "core/playerengine.h"
#include <QObject>
#include <QString>

class QTimer;
class QWidget;

#if defined(Q_OS_LINUX)
class MprisRootAdaptor;
class MprisPlayerAdaptor;
#include <QDBusObjectPath>
#include <QVariantMap>
#endif

/**
 * Cross-platform system media integration (one implementation is linked per OS):
 * - Linux / *BSD: MPRIS over session D-Bus (desktop keys, DE applets, lock screen).
 * - macOS: MediaPlayer (MPNowPlayingInfoCenter, MPRemoteCommandCenter).
 * - Windows: WM_APPCOMMAND multimedia keys via Qt native event filter (works when the app receives
 *   foreground command messages). OS overlay / taskbar “Now playing” would need extra WinRT work.
 * - Other platforms: stub (no OS integration).
 */
class SystemMediaController : public QObject
{
    Q_OBJECT

public:
    explicit SystemMediaController(QObject *parent = nullptr);
    ~SystemMediaController() override;

    /** Needed on Windows (SMTC HWND); optional elsewhere (e.g. future use). */
    void setHostWindow(QWidget *host);

    void setPlayerEngine(PlayerEngine *engine);

    void updateFromEngineState(PlayerEngine::PlaybackState state);
    void updateMetadata(const MusicInfo &music, qint64 durationMs);
    void updateCapabilities(bool canNext, bool canPrev, bool canSeek);
    void updateLoopShuffle(const QString &playMode);
    void syncVolumeFromEngine(double volume01);
    void notifySeeked(qlonglong positionUs);

    void fireRaise() { emit raiseRequested(); }
    void fireQuit() { emit quitRequested(); }
    void fireNext() { emit nextRequested(); }
    void firePrevious() { emit previousRequested(); }
    void firePlay() { emit playRequested(); }
    void firePause() { emit pauseRequested(); }
    void firePlayPause() { emit playPauseRequested(); }
    void fireStop() { emit stopRequested(); }
    void fireSeekRelative(qint64 deltaUs) { emit seekRelativeUs(deltaUs); }
    void fireSeekAbsolute(qint64 positionUs) { emit seekAbsoluteUs(positionUs); }

#if defined(Q_OS_LINUX)
    QString mprisPlaybackStatus() const { return m_playbackStatus; }
    QString mprisLoopStatus() const { return m_loopStatus; }
    bool mprisShuffle() const { return m_shuffle; }
    QVariantMap mprisMetadata() const { return m_metadata; }
    double mprisVolume() const { return m_volume; }
    qlonglong mprisPositionUs() const;
    bool mprisCanGoNext() const { return m_canGoNext; }
    bool mprisCanGoPrevious() const { return m_canGoPrevious; }
    bool mprisCanPlay() const;
    bool mprisCanPause() const;
    bool mprisCanSeek() const { return m_canSeek; }
    QString mprisCurrentTrackPath() const { return m_currentTrackId.path(); }
    void applyVolumeFromMpris(double v);
    void applyLoopStatusFromMpris(const QString &status);
    void applyShuffleFromMpris(bool shuffle);
    void syncPlaybackStatusFromEngine();
    QString computeMprisPlaybackStatus() const;
#endif

public slots:
    void onPositionMsChanged(qint64 positionMs);

signals:
    void raiseRequested();
    void quitRequested();
    void nextRequested();
    void previousRequested();
    void playRequested();
    void pauseRequested();
    void playPauseRequested();
    void stopRequested();
    void seekRelativeUs(qint64 deltaUs);
    void seekAbsoluteUs(qint64 positionUs);
    void volumeSetByOs(double volume01);
    void loopStatusSetRequested(const QString &status);
    void shuffleSetRequested(bool shuffle);

private:
#if defined(Q_OS_LINUX)
    void startPositionTimer();
    void stopPositionTimer();

    MprisRootAdaptor *m_rootAdaptor = nullptr;
    MprisPlayerAdaptor *m_playerAdaptor = nullptr;
#endif

    PlayerEngine *m_engine = nullptr;
    QTimer *m_positionTimer = nullptr;
    QWidget *m_hostWindow = nullptr;

    QString m_playbackStatus = QStringLiteral("Stopped");
    QString m_loopStatus = QStringLiteral("None");
    bool m_shuffle = false;
    double m_volume = 1.0;
    bool m_canGoNext = false;
    bool m_canGoPrevious = false;
    bool m_canSeek = false;
    bool m_hasActiveTrack = false;
    qint64 m_lastPositionEmitMs = -1;

#if defined(Q_OS_LINUX)
    QVariantMap m_metadata;
    QDBusObjectPath m_currentTrackId;
#endif

#if defined(Q_OS_MACOS)
    void *m_macOpaque = nullptr;
#endif

#if defined(Q_OS_WIN)
    void *m_winOpaque = nullptr;
#endif
};
