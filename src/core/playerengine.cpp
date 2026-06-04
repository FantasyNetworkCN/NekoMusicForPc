#include "playerengine.h"
#include <QMediaMetaData>
#include <QTimer>
#include <memory>

PlayerEngine::PlayerEngine(QObject *parent)
    : QObject(parent)
    , m_player(new QMediaPlayer(this))
    , m_audioOutput(new QAudioOutput(this))
{
    m_player->setAudioOutput(m_audioOutput);

    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, &PlayerEngine::onMediaStateChanged);
    connect(m_player, &QMediaPlayer::positionChanged,
            this, &PlayerEngine::positionChanged);
    connect(m_player, &QMediaPlayer::durationChanged,
            this, &PlayerEngine::durationChanged);
    connect(m_player, &QMediaPlayer::errorOccurred,
            this, [this](QMediaPlayer::Error error, const QString &errorString) {
                Q_UNUSED(error);
                emit mediaError(errorString);
            });
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::EndOfMedia) {
                    emit playbackFinished();
                }
            });
    connect(m_player, &QMediaPlayer::metaDataChanged, this, &PlayerEngine::onPlayerMetaDataChanged);
}

PlayerEngine::~PlayerEngine() = default;

void PlayerEngine::cancelFade()
{
    if (m_fadeTimer) {
        m_fadeTimer->stop();
        delete m_fadeTimer;
        m_fadeTimer = nullptr;
    }
    m_fadingIn = false;
    m_fadingOut = false;
}

void PlayerEngine::play(const QUrl &url)
{
    openMedia(url);
}

void PlayerEngine::playLocalResuming(const QString &localPath, qint64 resumeMs)
{
    openMedia(QUrl::fromLocalFile(localPath), resumeMs);
}

void PlayerEngine::openMedia(const QUrl &url, qint64 resumeMs)
{
    cancelFade();
    if (m_audioOutput)
        m_audioOutput->setVolume(m_targetVolume);

    ++m_openGen;
    const quint64 gen = m_openGen;
    m_pendingUrl = url;
    m_pendingResumeMs = resumeMs;

    if (m_player->playbackState() == QMediaPlayer::StoppedState) {
        applyPendingOpen(gen);
        return;
    }

    if (!m_stopForOpenConn) {
        m_stopForOpenConn = connect(m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                if (state != QMediaPlayer::StoppedState)
                    return;
                applyPendingOpen(m_openGen);
            });
    }
    m_player->stop();
}

void PlayerEngine::applyPendingOpen(quint64 gen)
{
    if (gen != m_openGen || m_pendingUrl.isEmpty())
        return;

    const QUrl url = m_pendingUrl;
    const qint64 resumeMs = m_pendingResumeMs;
    m_pendingUrl = QUrl();
    m_pendingResumeMs = -1;

    // 先清空再加载，降低 QFFmpeg demuxer 在快速切源时的崩溃概率
    m_player->setSource(QUrl());
    m_player->setSource(url);
    m_player->play();

    if (resumeMs > 0)
        scheduleResumeAfterOpen(resumeMs);
}

void PlayerEngine::scheduleResumeAfterOpen(qint64 resumeMs)
{
    const qint64 dur = m_player->duration();
    if (dur > 0) {
        m_player->setPosition(qMin(resumeMs, qMax(qint64(0), dur - 1)));
        return;
    }
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_player, &QMediaPlayer::durationChanged, this,
        [this, resumeMs, conn](qint64 d) {
            if (d <= 0)
                return;
            disconnect(*conn);
            m_player->setPosition(qMin(resumeMs, qMax(qint64(0), d - 1)));
        });
}

void PlayerEngine::play()
{
    cancelFade();
    if (m_audioOutput)
        m_audioOutput->setVolume(m_targetVolume);
    m_player->play();
}

void PlayerEngine::pause()
{
    cancelFade();
    m_player->pause();
}

void PlayerEngine::stop()
{
    cancelFade();
    ++m_openGen;
    m_pendingUrl = QUrl();
    m_pendingResumeMs = -1;
    m_player->stop();
}

void PlayerEngine::setVolume(float volume)
{
    m_targetVolume = qBound(0.0f, volume, 1.0f);
    if (m_audioOutput) {
        m_audioOutput->setVolume(m_targetVolume);
    }
}

void PlayerEngine::fadeIn()
{
    cancelFade();
    m_fadingIn = true;
    m_audioOutput->setVolume(0.0f);
    m_player->play();

    // 若在 fadeOut 过程中 m_state 已提前为 Paused，而 QMediaPlayer 仍在 Playing，
    // 此时再 play() 可能不会再次触发 playbackStateChanged，导致系统媒体与引擎状态脱节。
    auto syncPlaying = [this]() {
        if (m_player->playbackState() == QMediaPlayer::PlayingState && m_state != Playing) {
            m_state = Playing;
            emit stateChanged(m_state);
        }
    };
    syncPlaying();
    QTimer::singleShot(0, this, syncPlaying);

    m_fadeTimer = new QTimer(this);
    connect(m_fadeTimer, &QTimer::timeout, this, &PlayerEngine::onFadeTick);
    m_fadeTimer->start(20); // ~50 ticks for 1s fade
}

void PlayerEngine::fadeOut()
{
    cancelFade();
    m_fadingOut = true;

    // Immediately update state so UI shows paused
    m_state = Paused;
    emit stateChanged(m_state);

    m_fadeTimer = new QTimer(this);
    connect(m_fadeTimer, &QTimer::timeout, this, &PlayerEngine::onFadeTick);
    m_fadeTimer->start(20);
}

void PlayerEngine::onFadeTick()
{
    if (!m_audioOutput) return;

    const float step = 0.04f;

    if (m_fadingIn) {
        float vol = m_audioOutput->volume() + step;
        if (vol >= m_targetVolume) {
            vol = m_targetVolume;
            m_fadingIn = false;
            m_fadeTimer->stop();
            delete m_fadeTimer;
            m_fadeTimer = nullptr;
            emit fadeComplete();
        }
        m_audioOutput->setVolume(vol);
    } else if (m_fadingOut) {
        float vol = m_audioOutput->volume() - step;
        if (vol <= 0.0f) {
            vol = 0.0f;
            m_audioOutput->setVolume(vol);
            m_player->pause();
            m_fadingOut = false;
            m_fadeTimer->stop();
            delete m_fadeTimer;
            m_fadeTimer = nullptr;
            emit fadeComplete();
        } else {
            m_audioOutput->setVolume(vol);
        }
    }
}

PlayerEngine::PlaybackState PlayerEngine::playbackState() const
{
    return m_state;
}

bool PlayerEngine::isActuallyPlaying() const
{
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

PlayerEngine::PlaybackState PlayerEngine::transportStateForOs() const
{
    const auto ps = m_player->playbackState();
    if (m_fadingOut && ps == QMediaPlayer::PlayingState)
        return Paused;
    switch (ps) {
    case QMediaPlayer::PlayingState:
        return Playing;
    case QMediaPlayer::PausedState:
        return Paused;
    case QMediaPlayer::StoppedState:
    default:
        return Stopped;
    }
}

qint64 PlayerEngine::duration() const
{
    return m_player->duration();
}

qint64 PlayerEngine::position() const
{
    return m_player->position();
}

QUrl PlayerEngine::currentMediaUrl() const
{
    return m_player ? m_player->source() : QUrl();
}

int PlayerEngine::audioBitRateBps() const
{
    if (!m_player)
        return 0;
    const QVariant v = m_player->metaData().value(QMediaMetaData::AudioBitRate);
    if (!v.isValid())
        return 0;
    bool ok = false;
    const int bps = v.toInt(&ok);
    return ok && bps > 0 ? bps : 0;
}

void PlayerEngine::onPlayerMetaDataChanged()
{
    if (audioBitRateBps() > 0)
        emit audioMetaReady();
}

float PlayerEngine::volume() const
{
    return m_audioOutput ? m_audioOutput->volume() : 0.0f;
}

void PlayerEngine::setPosition(qint64 position)
{
    if (!m_player) return;
    if (m_seekLimitMs >= 0 && position > m_seekLimitMs) {
        position = m_seekLimitMs;
    }
    m_player->setPosition(position);
}

void PlayerEngine::onMediaStateChanged(QMediaPlayer::PlaybackState state)
{
    // fadeOut 期间 QMediaPlayer 仍为 Playing，避免把已对外声明的 Paused 又改回 m_state
    if (!(m_fadingOut && state == QMediaPlayer::PlayingState)) {
        switch (state) {
        case QMediaPlayer::PlayingState:
            m_state = Playing;
            if (m_currentMusic.id > 0 || m_currentMusic.isLocalFile()) {
                emit musicStarted(m_currentMusic);
            }
            break;
        case QMediaPlayer::PausedState:
            m_state = Paused;
            break;
        case QMediaPlayer::StoppedState:
            m_state = Stopped;
            // 勿在此处 emit playbackFinished：用户 stop() 切歌也会进入 Stopped，
            // 会与「自然播完」竞态，误触发自动下一首。自然结束由 MediaStatus::EndOfMedia 发出。
            emit stateChanged(m_state);
            break;
        }
        emit stateChanged(m_state);
    }
    emit mediaPlaybackStateChanged();
}

void PlayerEngine::setCurrentMusic(const MusicInfo& music)
{
    m_currentMusic = music;
}
