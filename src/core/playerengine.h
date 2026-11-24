#pragma once

#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioBuffer>
#include <QAudioDevice>
#include <QUrl>

#include "core/musicinfo.h"

class QTimer;
class HttpMediaDevice;
class QAudioBufferOutput;

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
    /** 切换远程/任意 URL 并尽量从 resumeMs 继续（用于音质切换断点续传）。 */
    void playResuming(const QUrl &url, qint64 resumeMs);
    /** 在当前播放不中断的情况下切换媒体源。 */
    void switchSourceWithoutRestart(const QUrl &url);
    void play();
    void pause();
    void stop();
    void setVolume(float volume);
    /** 分离混音接管输出时，仅静音原声，不改主音量或淡入淡出状态。 */
    void setOriginalAudioMuted(bool muted);
    /** 当前音频输出设备（麦克风同步会临时切到虚拟声卡，关闭时还原）。 */
    QAudioDevice outputDevice() const;
    void setOutputDevice(const QAudioDevice &device);
    /** 主音量设置，不受淡入淡出过程或原声静音影响。 */
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
    void volumeChanged(float volume);
    void outputDeviceChanged(const QAudioDevice &device);
    void seekStarted();
    /** QMediaPlayer::playbackState 每次变化时发出（与 m_state 是否被淡出逻辑屏蔽无关）。 */
    void mediaPlaybackStateChanged();
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void fadeComplete();
    void currentMusicChanged(const MusicInfo &music);
    void musicStarted(const MusicInfo& music);
    void mediaError(const QString &error);
    void playbackFinished();
    /** 播放器元数据就绪时发出（含可用码率） */
    void audioMetaReady();
    /** 已解码、正在送入主音频输出的 PCM；分离混音直接复用这一路数据。 */
    void decodedAudioBuffer(const QAudioBuffer &buffer);
    /** 解码音频的低频能量（0..1），用于 AMLL 流动背景随鼓点起伏。 */
    void lowFrequencyLevelChanged(qreal level);

private:
    void onPlayerMetaDataChanged();
    void connectPlayerSignals(QMediaPlayer *player);
    /** http(s) 源改用应用网络栈的 QIODevice，保证 User-Agent 与其它请求一致。 */
    void attachHttpDevice(QMediaPlayer *player, const QUrl &url, HttpMediaDevice *&slot);
    /** 延迟析构旧设备：FFmpeg 解复用线程可能仍在收尾，立即析构有 UAF 风险。 */
    void retireStreamDevice(HttpMediaDevice *device);
    void cancelFade();
    void persistVolume();
    void onMediaStateChanged(QMediaPlayer::PlaybackState state);
    void onFadeTick();
    /** 等底层 Stopped 后再 setSource，避免切歌/重试时 FFmpeg demuxer 竞态。 */
    void openMedia(const QUrl &url, qint64 resumeMs = -1);
    void applyPendingOpen(quint64 gen);
    void scheduleResumeAfterOpen(qint64 resumeMs);
    /** 新媒体可 seek 且时长就绪后执行断点 seek；未命中则后续信号继续重试。 */
    void applyPendingResume();
    /** 时长就绪且 mediaStatus 至少 LoadedMedia 时，setPosition 才不会被后端丢弃。 */
    bool resumeMediaReady() const;
    void clearPendingResume();
    void cancelQualitySwitch();
    void analyzeAudioBuffer(const QAudioBuffer &buffer);

    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    bool m_originalAudioMuted = false;
    PlaybackState m_state = Stopped;
    float m_targetVolume = 1.0f;
    QTimer *m_volumeSaveTimer = nullptr;
    QTimer *m_fadeTimer = nullptr;
    bool m_fadingIn = false;
    bool m_fadingOut = false;
    MusicInfo m_currentMusic;
    qint64 m_seekLimitMs = -1; // -1 means no limit
    QUrl m_pendingUrl;
    qint64 m_pendingResumeMs = -1;
    qint64 m_resumeTargetMs = -1;
    QMetaObject::Connection m_resumeStatusConn;
    QMetaObject::Connection m_resumeDurationConn;
    QMetaObject::Connection m_resumeSeekableConn;
    QMetaObject::Connection m_resumePositionConn;
    QTimer *m_resumeTimeoutTimer = nullptr;
    quint64 m_openGen = 0;
    QMetaObject::Connection m_stopForOpenConn;
    QMediaPlayer *m_qualitySwitchPlayer = nullptr;
    QAudioOutput *m_qualitySwitchOutput = nullptr;
    HttpMediaDevice *m_streamDevice = nullptr;
    HttpMediaDevice *m_qualitySwitchDevice = nullptr;
    quint64 m_qualitySwitchGen = 0;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAudioBufferOutput *m_audioBufferOutput = nullptr;
    qreal m_audioLowPass = 0.0;
    qreal m_audioLevel = 0.0;
#endif

public:
    void setSeekLimitMs(qint64 limitMs) { m_seekLimitMs = limitMs; }
    qint64 seekLimitMs() const { return m_seekLimitMs; }
};
