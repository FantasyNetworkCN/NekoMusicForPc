#pragma once

#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QUrl>

#include "core/musicinfo.h"

class QTimer;

class PlayerEngine : public QObject
{
    Q_OBJECT

public:
    enum PlaybackState {
        Stopped,
        Playing,
        Paused
    };
    Q_ENUM(PlaybackState)

    explicit PlayerEngine(QObject *parent = nullptr);
    ~PlayerEngine() override;

    void play(const QUrl &url);
    /** 切换本地文件并尽量从 resumeMs 继续（用于 .part 缓冲播完后切到正式缓存文件）。 */
    void playLocalResuming(const QString &localPath, qint64 resumeMs);
    void play();
    void pause();
    void stop();
    void setVolume(float volume);
    float volume() const;
    void setPosition(qint64 position);
    void fadeIn();
    void fadeOut();
    void setCurrentMusic(const MusicInfo& music);
    const MusicInfo &currentMusic() const { return m_currentMusic; }

    PlaybackState playbackState() const;
    /** 与 QMediaPlayer 一致；淡出过程中 m_state 可能已为 Paused 但底层仍在 Playing 时为 true。 */
    bool isActuallyPlaying() const;
    bool isFadingOut() const { return m_fadingOut; }
    /** 对齐 QMediaPlayer，供 MPRIS / 系统媒体用；淡出过程中底层仍在播时仍视为 Paused。 */
    PlaybackState transportStateForOs() const;
    QUrl currentMediaUrl() const;
    qint64 duration() const;
    qint64 position() const;
    /** QMediaPlayer 解析出的音频码率（bps），未就绪时为 0 */
    int audioBitRateBps() const;

signals:
    void stateChanged(PlaybackState state);
    /** QMediaPlayer::playbackState 每次变化时发出（与 m_state 是否被淡出逻辑屏蔽无关）。 */
    void mediaPlaybackStateChanged();
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void fadeComplete();
    void musicStarted(const MusicInfo& music);
    void mediaError(const QString &error);
    void playbackFinished();
    /** 播放器元数据就绪时发出（含可用码率） */
    void audioMetaReady();

private:
    void onPlayerMetaDataChanged();
    void cancelFade();
    void onMediaStateChanged(QMediaPlayer::PlaybackState state);
    void onFadeTick();
    /** 等底层 Stopped 后再 setSource，避免切歌/重试时 FFmpeg demuxer 竞态。 */
    void openMedia(const QUrl &url, qint64 resumeMs = -1);
    void applyPendingOpen(quint64 gen);
    void scheduleResumeAfterOpen(qint64 resumeMs);

    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    PlaybackState m_state = Stopped;
    float m_targetVolume = 1.0f;
    QTimer *m_fadeTimer = nullptr;
    bool m_fadingIn = false;
    bool m_fadingOut = false;
    MusicInfo m_currentMusic;
    qint64 m_seekLimitMs = -1; // -1 means no limit
    QUrl m_pendingUrl;
    qint64 m_pendingResumeMs = -1;
    quint64 m_openGen = 0;
    QMetaObject::Connection m_stopForOpenConn;

public:
    void setSeekLimitMs(qint64 limitMs) { m_seekLimitMs = limitMs; }
    qint64 seekLimitMs() const { return m_seekLimitMs; }
};
