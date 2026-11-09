#include "playerengine.h"
#include "httpmediadevice.h"
#include "core/applog.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QMediaMetaData>
#include <QSignalBlocker>
#include <QUrl>
#include <QTimer>
#include <memory>

PlayerEngine::PlayerEngine(QObject *parent)
    : QObject(parent)
    , m_player(new QMediaPlayer(this))
    , m_audioOutput(new QAudioOutput(this))
{
    m_player->setAudioOutput(m_audioOutput);

    connectPlayerSignals(m_player);
}

PlayerEngine::~PlayerEngine()
{
    qInfo().noquote() << QStringLiteral("[Player] 引擎析构，开始停止播放器");
    if (m_player)
        m_player->stop();
    if (m_qualitySwitchPlayer)
        m_qualitySwitchPlayer->stop();
    if (m_streamDevice)
        m_streamDevice->abort();
    if (m_qualitySwitchDevice)
        m_qualitySwitchDevice->abort();
}

namespace {

bool isHttpUrl(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

} // namespace

void PlayerEngine::attachHttpDevice(QMediaPlayer *player, const QUrl &url, HttpMediaDevice *&slot)
{
    if (!player)
        return;

    QElapsedTimer timer;
    timer.start();

    auto *device = new HttpMediaDevice(url, this);
    connect(device, &HttpMediaDevice::fetchError, this, [this, device](const QString &error) {
        if (device != m_streamDevice && device != m_qualitySwitchDevice)
            return;
        qWarning() << "[PlayerEngine] 音频流错误:" << error;
        emit mediaError(error);
    });
    device->open(QIODevice::ReadOnly);
    device->start();
    const qint64 prepareMs = timer.elapsed();

    // 关键点：Qt 的 FFmpeg 后端会在调用线程（GUI 线程）上同步等待媒体源打开完成，
    // 因此这一行就是“按下切歌/播放键后界面整片卡住”的直接落点。
    // 打开快慢取决于网络首包，日志里 prepareMs 与本行耗时可以把两者区分开。
    player->setSourceDevice(device, url);
    const qint64 attachMs = timer.elapsed() - prepareMs;

    // 先交棒给播放器，再回收旧设备，避免交接瞬间旧设备读到半截数据。
    HttpMediaDevice *previous = slot;
    slot = device;
    retireStreamDevice(previous);

    qInfo().noquote() << QStringLiteral("[Player] 挂载音频流 准备%1 + 打开%2 = %3（%4）")
                             .arg(AppLog::msText(prepareMs), AppLog::msText(attachMs),
                                  AppLog::msText(timer.elapsed()), AppLog::safeUrl(url));
    if (attachMs >= 200) {
        qWarning().noquote() << QStringLiteral("[Player] setSourceDevice 同步阻塞 GUI 线程 %1，界面会在此期间无响应")
                                    .arg(AppLog::msText(attachMs));
    }
}

void PlayerEngine::retireStreamDevice(HttpMediaDevice *device)
{
    if (!device)
        return;
    QElapsedTimer timer;
    timer.start();
    // 只停机、不析构：FFmpeg 解复用线程在切源之后仍可能回调 readData()/size()/seek()。
    // 对象是 PlayerEngine 的子对象，会随 m_player（先创建、先析构）之后一起销毁，
    // 那时后端线程已经 join 完毕，不会再出现 use-after-free。
    device->shutdown();
    qInfo().noquote() << QStringLiteral("[Player] 退役音频流设备 耗时%1")
                             .arg(AppLog::msText(timer.elapsed()));
}

QAudioDevice PlayerEngine::outputDevice() const
{
    return m_audioOutput ? m_audioOutput->device() : QAudioDevice();
}

void PlayerEngine::setOutputDevice(const QAudioDevice &device)
{
    if (!m_audioOutput || device.isNull())
        return;
    if (m_audioOutput->device() == device)
        return;
    QElapsedTimer timer;
    timer.start();
    m_audioOutput->setDevice(device);
    const qint64 cost = timer.elapsed();
    // 播放中换设备会同步停/启 QAudioSink，是已知的可卡顿点，记录下来。
    if (cost >= 200) {
        qWarning().noquote() << QStringLiteral("[Player] 切换音频输出设备阻塞 GUI %1（%2）")
                                    .arg(AppLog::msText(cost), device.description());
    } else {
        qInfo().noquote() << QStringLiteral("[Player] 切换音频输出设备 耗时%1（%2）")
                                 .arg(AppLog::msText(cost), device.description());
    }
}

void PlayerEngine::cancelFade()
{
    if (m_fadeTimer) {
        m_fadeTimer->stop();
        // 不能 delete：cancelFade 可能在 fade 的 timeout 回调链里被调用，
        // 直接析构正在发信号的定时器是未定义行为。
        m_fadeTimer->deleteLater();
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

void PlayerEngine::playResuming(const QUrl &url, qint64 resumeMs)
{
    openMedia(url, resumeMs);
}

void PlayerEngine::switchSourceWithoutRestart(const QUrl &url)
{
    if (url.isEmpty() || !m_player)
        return;

    cancelFade();
    cancelQualitySwitch();
    const quint64 generation = ++m_qualitySwitchGen;
    QMediaPlayer *oldPlayer = m_player;
    QAudioOutput *oldOutput = m_audioOutput;
    const bool wasPlaying = oldPlayer->playbackState() == QMediaPlayer::PlayingState;
    const PlaybackState previousState = m_state;

    auto *candidate = new QMediaPlayer(this);
    auto *candidateOutput = new QAudioOutput(this);
    candidateOutput->setDevice(oldOutput->device());
    candidateOutput->setVolume(0.0f);
    candidate->setAudioOutput(candidateOutput);
    m_qualitySwitchPlayer = candidate;
    m_qualitySwitchOutput = candidateOutput;

    auto handedOff = std::make_shared<bool>(false);
    connect(candidate, &QMediaPlayer::mediaStatusChanged, this,
        [this, candidate, candidateOutput, oldPlayer, oldOutput, wasPlaying, previousState, generation, handedOff]
        (QMediaPlayer::MediaStatus status) {
            if (*handedOff || generation != m_qualitySwitchGen
                || (status != QMediaPlayer::LoadedMedia
                    && status != QMediaPlayer::BufferedMedia))
                return;
            *handedOff = true;

            const qint64 handoffPosition = qMax<qint64>(0, oldPlayer->position());
            candidate->setPosition(handoffPosition);
            if (wasPlaying)
                candidate->play();
            else
                candidate->pause();

            const QSignalBlocker oldPlayerSignals(oldPlayer);
            oldPlayer->pause();
            oldPlayer->stop();
            oldPlayer->deleteLater();
            oldOutput->deleteLater();

            m_player = candidate;
            m_audioOutput = candidateOutput;
            m_qualitySwitchPlayer = nullptr;
            m_qualitySwitchOutput = nullptr;
            HttpMediaDevice *retiredDevice = m_streamDevice;
            m_streamDevice = m_qualitySwitchDevice;
            m_qualitySwitchDevice = nullptr;
            retireStreamDevice(retiredDevice);
            disconnect(candidate, nullptr, this, nullptr);
            connectPlayerSignals(candidate);
            candidateOutput->setVolume(m_targetVolume);
            m_state = previousState;
            emit durationChanged(candidate->duration());
            emit positionChanged(handoffPosition);
            emit stateChanged(m_state);
            qDebug() << "[音质切换] 无感接管 position=" << handoffPosition;
        });
    connect(candidate, &QMediaPlayer::errorOccurred, this,
        [this, candidate, candidateOutput, generation](QMediaPlayer::Error error, const QString &message) {
            Q_UNUSED(error);
            if (generation != m_qualitySwitchGen)
                return;
            qWarning() << "[音质切换] 新音质加载失败:" << message;
            if (m_qualitySwitchPlayer == candidate) {
                m_qualitySwitchPlayer = nullptr;
                m_qualitySwitchOutput = nullptr;
                retireStreamDevice(m_qualitySwitchDevice);
                m_qualitySwitchDevice = nullptr;
                candidate->deleteLater();
                candidateOutput->deleteLater();
            }
        });

    if (isHttpUrl(url))
        attachHttpDevice(candidate, url, m_qualitySwitchDevice);
    else
        candidate->setSource(url);
    candidate->play();
}

void PlayerEngine::openMedia(const QUrl &url, qint64 resumeMs)
{
    cancelFade();
    cancelQualitySwitch();
    ++m_qualitySwitchGen;
    if (m_audioOutput)
        m_audioOutput->setVolume(m_targetVolume);

    // 丢弃上一轮未完成的断点 seek，避免误跳到新曲目的位置。
    clearPendingResume();

    ++m_openGen;
    const quint64 gen = m_openGen;
    m_pendingUrl = url;
    m_pendingResumeMs = resumeMs;

    qInfo().noquote() << QStringLiteral("[Player] openMedia state=%1 resume=%2 %3")
                             .arg(static_cast<int>(m_player->playbackState()))
                             .arg(resumeMs)
                             .arg(AppLog::safeUrl(url));

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

    // stop() 会同步等待 FFmpeg 解复用线程收工，播放中切歌时这一步也可能阻塞 GUI。
    QElapsedTimer stopTimer;
    stopTimer.start();
    m_player->stop();
    const qint64 stopMs = stopTimer.elapsed();
    if (stopMs >= 200) {
        qWarning().noquote() << QStringLiteral("[Player] QMediaPlayer::stop() 阻塞 GUI 线程 %1")
                                    .arg(AppLog::msText(stopMs));
    }
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
    QElapsedTimer timer;
    timer.start();
    m_player->setSource(QUrl());
    const qint64 resetMs = timer.elapsed();

    if (isHttpUrl(url))
        attachHttpDevice(m_player, url, m_streamDevice);
    else
        m_player->setSource(url);
    const qint64 loadMs = timer.elapsed() - resetMs;

    m_player->play();
    const qint64 playMs = timer.elapsed() - resetMs - loadMs;

    // 这三段耗时都发生在 GUI 线程上：切源慢会直接表现为界面无响应。
    qInfo().noquote() << QStringLiteral("[Player] 打开完成 清空%1 加载%2 起播%3（合计%4）")
                             .arg(AppLog::msText(resetMs), AppLog::msText(loadMs),
                                  AppLog::msText(playMs), AppLog::msText(timer.elapsed()));
    if (loadMs >= 300 || resetMs >= 300) {
        qWarning().noquote() << QStringLiteral("[Player] 切源在 GUI 线程耗时偏高（%1），界面可能卡顿")
                                    .arg(AppLog::msText(timer.elapsed()));
    }

    if (resumeMs > 0)
        scheduleResumeAfterOpen(resumeMs);
}

bool PlayerEngine::resumeMediaReady() const
{
    if (m_player->duration() <= 0)
        return false;
    // 注意：isSeekable() 在 LoadingMedia 阶段就可能为 true，但此时 setPosition 会被
    // 后端静默丢弃（实测 FFmpeg/HTTP）。必须等 mediaStatus 至少 LoadedMedia 再 seek。
    const auto st = m_player->mediaStatus();
    return st == QMediaPlayer::LoadedMedia
        || st == QMediaPlayer::StalledMedia
        || st == QMediaPlayer::BufferingMedia
        || st == QMediaPlayer::BufferedMedia;
}

void PlayerEngine::scheduleResumeAfterOpen(qint64 resumeMs)
{
    clearPendingResume();
    if (resumeMs <= 0)
        return;

    // 切源瞬间 duration()/可 seek 状态都没就绪，直接 setPosition 会被后端丢弃，
    // 于是新档从 0 开始。这里保留目标，等媒体就绪后 seek，并持续重试直到命中。
    m_resumeTargetMs = resumeMs;
    qDebug() << "[PlayerEngine] 断点续传等待就绪 target=" << resumeMs;

    m_resumeStatusConn = connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
        [this](QMediaPlayer::MediaStatus) { applyPendingResume(); });
    m_resumeDurationConn = connect(m_player, &QMediaPlayer::durationChanged, this,
        [this](qint64 dur) { if (dur > 0) applyPendingResume(); });
    m_resumeSeekableConn = connect(m_player, &QMediaPlayer::seekableChanged, this,
        [this](bool seekable) { if (seekable) applyPendingResume(); });

    // 成功判定：position 落到目标附近（seek 生效）即结束；否则由上面的信号重试。
    m_resumePositionConn = connect(m_player, &QMediaPlayer::positionChanged, this,
        [this](qint64 pos) {
            if (m_resumeTargetMs <= 0)
                return;
            if (qAbs(pos - m_resumeTargetMs) <= 2000) {
                qDebug() << "[PlayerEngine] 断点续传命中 pos=" << pos
                         << "target=" << m_resumeTargetMs;
                clearPendingResume();
            }
        });

    // 兜底超时，避免 pending 一直挂着。
    if (!m_resumeTimeoutTimer) {
        m_resumeTimeoutTimer = new QTimer(this);
        m_resumeTimeoutTimer->setSingleShot(true);
        m_resumeTimeoutTimer->setInterval(8000);
        connect(m_resumeTimeoutTimer, &QTimer::timeout, this, [this]() {
            qDebug() << "[PlayerEngine] 断点续传超时 target=" << m_resumeTargetMs
                     << "pos=" << m_player->position()
                     << "seekable=" << m_player->isSeekable();
            clearPendingResume();
        });
    }
    m_resumeTimeoutTimer->start();
}

void PlayerEngine::applyPendingResume()
{
    if (m_resumeTargetMs <= 0)
        return;
    if (!resumeMediaReady())
        return;

    const qint64 dur = m_player->duration();
    const qint64 target = qMin(m_resumeTargetMs, qMax(qint64(0), dur - 1));
    qDebug() << "[PlayerEngine] 断点续传 seek ->" << target
             << "dur=" << dur << "seekable=" << m_player->isSeekable();
    // 不清除 pending：若这次 seek 被后端丢弃，后续状态信号会继续重试。
    m_player->setPosition(target);
}

void PlayerEngine::clearPendingResume()
{
    m_resumeTargetMs = -1;
    if (m_resumeTimeoutTimer)
        m_resumeTimeoutTimer->stop();

    if (m_resumeStatusConn) {
        disconnect(m_resumeStatusConn);
        m_resumeStatusConn = QMetaObject::Connection();
    }
    if (m_resumeDurationConn) {
        disconnect(m_resumeDurationConn);
        m_resumeDurationConn = QMetaObject::Connection();
    }
    if (m_resumeSeekableConn) {
        disconnect(m_resumeSeekableConn);
        m_resumeSeekableConn = QMetaObject::Connection();
    }
    if (m_resumePositionConn) {
        disconnect(m_resumePositionConn);
        m_resumePositionConn = QMetaObject::Connection();
    }
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
    cancelQualitySwitch();
    ++m_qualitySwitchGen;
    ++m_openGen;
    m_pendingUrl = QUrl();
    m_pendingResumeMs = -1;
    clearPendingResume();
    m_player->stop();
}

void PlayerEngine::cancelQualitySwitch()
{
    if (m_qualitySwitchPlayer) {
        m_qualitySwitchPlayer->stop();
        m_qualitySwitchPlayer->deleteLater();
        m_qualitySwitchPlayer = nullptr;
    }
    if (m_qualitySwitchOutput) {
        m_qualitySwitchOutput->deleteLater();
        m_qualitySwitchOutput = nullptr;
    }
    retireStreamDevice(m_qualitySwitchDevice);
    m_qualitySwitchDevice = nullptr;
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
            m_fadeTimer->deleteLater();
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
            m_fadeTimer->deleteLater();
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

void PlayerEngine::connectPlayerSignals(QMediaPlayer *player)
{
    connect(player, &QMediaPlayer::playbackStateChanged,
            this, &PlayerEngine::onMediaStateChanged);
    connect(player, &QMediaPlayer::positionChanged,
            this, &PlayerEngine::positionChanged);
    connect(player, &QMediaPlayer::durationChanged,
            this, &PlayerEngine::durationChanged);
    connect(player, &QMediaPlayer::errorOccurred,
            this, [this](QMediaPlayer::Error error, const QString &errorString) {
                Q_UNUSED(error);
                emit mediaError(errorString);
            });
    connect(player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus status) {
                // 记录"数据供给中断"：BufferingMedia / StalledMedia 出现时，
                // 用户听到的就是断音。蓝牙链路抖动通常不触发这两个状态，
                // 因此这份日志也用来区分"是数据没来"还是"声卡侧吞不下"。
                if (status == QMediaPlayer::StalledMedia)
                    qWarning().noquote() << QStringLiteral("[Player] 媒体停滞(StalledMedia)：音频数据供给中断");
                else if (status == QMediaPlayer::BufferingMedia)
                    qInfo().noquote() << QStringLiteral("[Player] 缓冲中(BufferingMedia)");
                if (status == QMediaPlayer::EndOfMedia)
                    emit playbackFinished();
            });
    connect(player, &QMediaPlayer::metaDataChanged, this, &PlayerEngine::onPlayerMetaDataChanged);
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
