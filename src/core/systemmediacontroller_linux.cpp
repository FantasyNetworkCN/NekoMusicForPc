#include "systemmediacontroller.h"
#include "core/covercache.h"
#include "core/playerengine.h"
#include "theme/theme.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>
#include <QTimer>
#include <QWidget>
#include <QUrl>

class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool FullScreen READ fullScreen CONSTANT)
    Q_PROPERTY(bool CanSetFullScreen READ canSetFullScreen CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes CONSTANT)

public:
    explicit MprisRootAdaptor(SystemMediaController *parent);

    bool canQuit() const;
    bool fullScreen() const;
    bool canSetFullScreen() const;
    bool canRaise() const;
    bool hasTrackList() const;
    QString identity() const;
    QString desktopEntry() const;
    QStringList supportedUriSchemes() const;
    QStringList supportedMimeTypes() const;

public slots:
    void Raise();
    void Quit();

private:
    SystemMediaController *m_ctrl;
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus NOTIFY playbackStatusChanged)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus NOTIFY loopStatusChanged)
    Q_PROPERTY(double Rate READ rate CONSTANT)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(QVariantMap Metadata READ metadata NOTIFY metadataChanged)
    Q_PROPERTY(double Volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(qlonglong Position READ position NOTIFY positionChanged)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canGoNext NOTIFY canGoNextChanged)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious NOTIFY canGoPreviousChanged)
    Q_PROPERTY(bool CanPlay READ canPlay NOTIFY canPlayChanged)
    Q_PROPERTY(bool CanPause READ canPause NOTIFY canPauseChanged)
    Q_PROPERTY(bool CanSeek READ canSeek NOTIFY canSeekChanged)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)

public:
    explicit MprisPlayerAdaptor(SystemMediaController *parent);

    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString &status);
    double rate() const;
    bool shuffle() const;
    void setShuffle(bool shuffle);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;
    double minimumRate() const;
    double maximumRate() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canSeek() const;
    bool canControl() const;

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position);

    void tellPlaybackStatusChanged() { emit playbackStatusChanged(); }
    void tellLoopStatusChanged() { emit loopStatusChanged(); }
    void tellShuffleChanged() { emit shuffleChanged(); }
    void tellMetadataChanged() { emit metadataChanged(); }
    void tellVolumeChanged() { emit volumeChanged(); }
    void tellPositionChanged() { emit positionChanged(); }
    void tellCanGoNextChanged() { emit canGoNextChanged(); }
    void tellCanGoPreviousChanged() { emit canGoPreviousChanged(); }
    void tellCanPlayChanged() { emit canPlayChanged(); }
    void tellCanPauseChanged() { emit canPauseChanged(); }
    void tellCanSeekChanged() { emit canSeekChanged(); }
    void tellSeeked(qlonglong positionUs) { emit Seeked(positionUs); }

signals:
    void playbackStatusChanged();
    void loopStatusChanged();
    void shuffleChanged();
    void metadataChanged();
    void volumeChanged();
    void positionChanged();
    void canGoNextChanged();
    void canGoPreviousChanged();
    void canPlayChanged();
    void canPauseChanged();
    void canSeekChanged();
    void Seeked(qlonglong position);

private:
    SystemMediaController *m_ctrl;
};

namespace {

constexpr char kMprisPath[] = "/org/mpris/MediaPlayer2";
constexpr char kMprisService[] = "org.mpris.MediaPlayer2.NekoMusic";
constexpr int kPositionEmitIntervalMs = 900;

const QDBusObjectPath kNoTrackId(QStringLiteral("/org/mpris/MediaPlayer2/Track/NoTrack"));

QString absoluteArtUrl(const MusicInfo &music)
{
    const QString resolved = CoverCache::resolveCoverUrl(music.coverUrl);
    if (resolved.isEmpty())
        return {};
    if (resolved.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)
        || resolved.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || resolved.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        return resolved;
    }
    return resolved;
}

QString trackUrlForMetadata(const MusicInfo &music, PlayerEngine *engine)
{
    if (music.isLocalFile())
        return QUrl::fromLocalFile(music.localPath).toString();
    if (engine) {
        const QUrl src = engine->currentMediaUrl();
        if (src.isValid())
            return src.toString();
    }
    if (music.id > 0) {
        return QStringLiteral("%1/detail/%2").arg(QString::fromUtf8(Theme::kApiBase)).arg(music.id);
    }
    return {};
}

} // namespace

MprisRootAdaptor::MprisRootAdaptor(SystemMediaController *parent)
    : QDBusAbstractAdaptor(parent)
    , m_ctrl(parent)
{
}

bool MprisRootAdaptor::canQuit() const { return true; }
bool MprisRootAdaptor::fullScreen() const { return false; }
bool MprisRootAdaptor::canSetFullScreen() const { return false; }
bool MprisRootAdaptor::canRaise() const { return true; }
bool MprisRootAdaptor::hasTrackList() const { return false; }

QString MprisRootAdaptor::identity() const
{
    return QStringLiteral("NekoMusic");
}

QString MprisRootAdaptor::desktopEntry() const
{
    return QStringLiteral("nekomusic");
}

QStringList MprisRootAdaptor::supportedUriSchemes() const
{
    return {QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("file")};
}

QStringList MprisRootAdaptor::supportedMimeTypes() const
{
    return {QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"), QStringLiteral("audio/ogg"),
            QStringLiteral("audio/x-wav"), QStringLiteral("audio/mp4"), QStringLiteral("audio/aac")};
}

void MprisRootAdaptor::Raise()
{
    m_ctrl->fireRaise();
}

void MprisRootAdaptor::Quit()
{
    m_ctrl->fireQuit();
}

MprisPlayerAdaptor::MprisPlayerAdaptor(SystemMediaController *parent)
    : QDBusAbstractAdaptor(parent)
    , m_ctrl(parent)
{
}

QString MprisPlayerAdaptor::playbackStatus() const { return m_ctrl->mprisPlaybackStatus(); }
QString MprisPlayerAdaptor::loopStatus() const { return m_ctrl->mprisLoopStatus(); }

void MprisPlayerAdaptor::setLoopStatus(const QString &status)
{
    m_ctrl->applyLoopStatusFromMpris(status);
}

double MprisPlayerAdaptor::rate() const { return 1.0; }
bool MprisPlayerAdaptor::shuffle() const { return m_ctrl->mprisShuffle(); }

void MprisPlayerAdaptor::setShuffle(bool shuffle)
{
    m_ctrl->applyShuffleFromMpris(shuffle);
}

QVariantMap MprisPlayerAdaptor::metadata() const { return m_ctrl->mprisMetadata(); }
double MprisPlayerAdaptor::volume() const { return m_ctrl->mprisVolume(); }

void MprisPlayerAdaptor::setVolume(double v)
{
    m_ctrl->applyVolumeFromMpris(v);
}

qlonglong MprisPlayerAdaptor::position() const
{
    return m_ctrl->mprisPositionUs();
}

double MprisPlayerAdaptor::minimumRate() const { return 1.0; }
double MprisPlayerAdaptor::maximumRate() const { return 1.0; }
bool MprisPlayerAdaptor::canGoNext() const { return m_ctrl->mprisCanGoNext(); }
bool MprisPlayerAdaptor::canGoPrevious() const { return m_ctrl->mprisCanGoPrevious(); }
bool MprisPlayerAdaptor::canPlay() const { return m_ctrl->mprisCanPlay(); }
bool MprisPlayerAdaptor::canPause() const { return m_ctrl->mprisCanPause(); }
bool MprisPlayerAdaptor::canSeek() const { return m_ctrl->mprisCanSeek(); }
bool MprisPlayerAdaptor::canControl() const { return true; }

void MprisPlayerAdaptor::Next()
{
    m_ctrl->fireNext();
}

void MprisPlayerAdaptor::Previous()
{
    m_ctrl->firePrevious();
}

void MprisPlayerAdaptor::Pause()
{
    m_ctrl->firePause();
}

void MprisPlayerAdaptor::PlayPause()
{
    m_ctrl->firePlayPause();
}

void MprisPlayerAdaptor::Stop()
{
    m_ctrl->fireStop();
}

void MprisPlayerAdaptor::Play()
{
    m_ctrl->firePlay();
}

void MprisPlayerAdaptor::Seek(qlonglong offset)
{
    m_ctrl->fireSeekRelative(offset);
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong position)
{
    if (trackId.path() != m_ctrl->mprisCurrentTrackPath())
        return;
    m_ctrl->fireSeekAbsolute(position);
}

SystemMediaController::SystemMediaController(QObject *parent)
    : QObject(parent)
    , m_rootAdaptor(new MprisRootAdaptor(this))
    , m_playerAdaptor(new MprisPlayerAdaptor(this))
{
    Q_UNUSED(m_rootAdaptor);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning() << "MPRIS: D-Bus session unavailable, system media keys integration disabled";
        return;
    }

    if (!bus.registerService(QString::fromLatin1(kMprisService))) {
        qWarning() << "MPRIS: registerService failed:" << bus.lastError().message();
        return;
    }

    if (!bus.registerObject(QString::fromLatin1(kMprisPath), this,
                            QDBusConnection::ExportAdaptors | QDBusConnection::ExportScriptableSlots)) {
        qWarning() << "MPRIS: registerObject failed:" << bus.lastError().message();
        bus.unregisterService(QString::fromLatin1(kMprisService));
        return;
    }
}

SystemMediaController::~SystemMediaController()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        bus.unregisterObject(QString::fromLatin1(kMprisPath));
        bus.unregisterService(QString::fromLatin1(kMprisService));
    }
}

void SystemMediaController::setHostWindow(QWidget *host)
{
    m_hostWindow = host;
    Q_UNUSED(m_hostWindow);
}

void SystemMediaController::setPlayerEngine(PlayerEngine *engine)
{
    m_engine = engine;
}

QString SystemMediaController::computeMprisPlaybackStatus() const
{
    if (!m_engine || !m_hasActiveTrack)
        return QStringLiteral("Stopped");
    if (m_engine->isFadingOut() && m_engine->isActuallyPlaying())
        return QStringLiteral("Paused");
    if (m_engine->isActuallyPlaying())
        return QStringLiteral("Playing");
    if (m_engine->playbackState() == PlayerEngine::Paused
        || m_engine->transportStateForOs() == PlayerEngine::Paused) {
        return QStringLiteral("Paused");
    }
    return QStringLiteral("Stopped");
}

void SystemMediaController::syncPlaybackStatusFromEngine()
{
    if (!m_playerAdaptor)
        return;

    const QString next = computeMprisPlaybackStatus();
    if (m_playbackStatus != next) {
        m_playbackStatus = next;
        m_playerAdaptor->tellPlaybackStatusChanged();
    }

    m_playerAdaptor->tellCanPlayChanged();
    m_playerAdaptor->tellCanPauseChanged();

    if (next == QStringLiteral("Playing"))
        startPositionTimer();
    else
        stopPositionTimer();
}

void SystemMediaController::updateFromEngineState(PlayerEngine::PlaybackState state)
{
    Q_UNUSED(state);
    syncPlaybackStatusFromEngine();
}

void SystemMediaController::updateMetadata(const MusicInfo &music, qint64 durationMs)
{
    QVariantMap meta;
    m_hasActiveTrack = false;

    const auto putCommonFields = [&](const MusicInfo &info, const QString &trackPath) {
        m_currentTrackId = QDBusObjectPath(trackPath);
        meta.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(m_currentTrackId));
        if (!info.title.isEmpty())
            meta.insert(QStringLiteral("xesam:title"), info.title);
        if (!info.artist.isEmpty()) {
            meta.insert(QStringLiteral("xesam:artist"), QStringList{info.artist});
            meta.insert(QStringLiteral("xesam:albumArtist"), QStringList{info.artist});
        }
        if (!info.album.isEmpty())
            meta.insert(QStringLiteral("xesam:album"), info.album);
        const QString art = absoluteArtUrl(info);
        if (!art.isEmpty())
            meta.insert(QStringLiteral("mpris:artUrl"), art);
        const QString url = trackUrlForMetadata(info, m_engine);
        if (!url.isEmpty())
            meta.insert(QStringLiteral("xesam:url"), url);
        const qint64 lenMs = qMax(durationMs, static_cast<qint64>(info.duration) * 1000);
        if (lenMs > 0)
            meta.insert(QStringLiteral("mpris:length"), lenMs * 1000LL);
        m_hasActiveTrack = true;
    };

    if (music.id > 0) {
        putCommonFields(music, QStringLiteral("/org/mpris/MediaPlayer2/track/%1").arg(music.id));
    } else if (music.isLocalFile()) {
        putCommonFields(music, QStringLiteral("/org/mpris/MediaPlayer2/track/local%1")
                                   .arg(qHash(music.localPath)));
    } else {
        m_currentTrackId = kNoTrackId;
        meta.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(kNoTrackId));
    }

    m_metadata = meta;
    m_playerAdaptor->tellMetadataChanged();
    m_playerAdaptor->tellPositionChanged();
    m_lastPositionEmitMs = -1;
    syncPlaybackStatusFromEngine();
}

void SystemMediaController::updateCapabilities(bool canNext, bool canPrev, bool canSeek)
{
    if (m_canGoNext != canNext) {
        m_canGoNext = canNext;
        m_playerAdaptor->tellCanGoNextChanged();
    }
    if (m_canGoPrevious != canPrev) {
        m_canGoPrevious = canPrev;
        m_playerAdaptor->tellCanGoPreviousChanged();
    }
    if (m_canSeek != canSeek) {
        m_canSeek = canSeek;
        m_playerAdaptor->tellCanSeekChanged();
    }
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

    if (m_loopStatus != loop) {
        m_loopStatus = loop;
        m_playerAdaptor->tellLoopStatusChanged();
    }
    if (m_shuffle != shuf) {
        m_shuffle = shuf;
        m_playerAdaptor->tellShuffleChanged();
    }
}

void SystemMediaController::applyLoopStatusFromMpris(const QString &status)
{
    emit loopStatusSetRequested(status);
}

void SystemMediaController::applyShuffleFromMpris(bool shuffle)
{
    emit shuffleSetRequested(shuffle);
}

void SystemMediaController::syncVolumeFromEngine(double volume01)
{
    volume01 = qBound(0.0, volume01, 1.0);
    if (qFuzzyCompare(m_volume + 1.0, volume01 + 1.0))
        return;
    m_volume = volume01;
    m_playerAdaptor->tellVolumeChanged();
}

void SystemMediaController::onPositionMsChanged(qint64 positionMs)
{
    Q_UNUSED(positionMs);
    if (m_playbackStatus != QStringLiteral("Playing"))
        return;
    if (!m_engine)
        return;
    const qint64 ms = m_engine->position();
    if (m_lastPositionEmitMs < 0 || qAbs(ms - m_lastPositionEmitMs) >= 1000) {
        m_lastPositionEmitMs = ms;
        m_playerAdaptor->tellPositionChanged();
    }
}

void SystemMediaController::startPositionTimer()
{
    if (!m_positionTimer) {
        m_positionTimer = new QTimer(this);
        m_positionTimer->setInterval(kPositionEmitIntervalMs);
        connect(m_positionTimer, &QTimer::timeout, this, [this]() {
            if (!m_engine || m_playbackStatus != QStringLiteral("Playing"))
                return;
            const qint64 ms = m_engine->position();
            if (m_lastPositionEmitMs < 0 || qAbs(ms - m_lastPositionEmitMs) >= 1000) {
                m_lastPositionEmitMs = ms;
                m_playerAdaptor->tellPositionChanged();
            }
        });
    }
    m_lastPositionEmitMs = -1;
    m_positionTimer->start();
}

void SystemMediaController::stopPositionTimer()
{
    if (m_positionTimer)
        m_positionTimer->stop();
}

void SystemMediaController::applyVolumeFromMpris(double v)
{
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_volume + 1.0, v + 1.0))
        return;
    m_volume = v;
    m_playerAdaptor->tellVolumeChanged();
    emit volumeSetByOs(v);
}

void SystemMediaController::notifySeeked(qlonglong positionUs)
{
    if (m_playerAdaptor)
        m_playerAdaptor->tellSeeked(positionUs);
}

qlonglong SystemMediaController::mprisPositionUs() const
{
    if (!m_engine)
        return 0;
    return static_cast<qlonglong>(m_engine->position()) * 1000LL;
}

bool SystemMediaController::mprisCanPlay() const
{
    if (!m_engine || !m_hasActiveTrack)
        return false;
    if (m_engine->isActuallyPlaying())
        return false;
    return m_playbackStatus != QStringLiteral("Playing");
}

bool SystemMediaController::mprisCanPause() const
{
    if (!m_engine || !m_hasActiveTrack)
        return false;
    if (m_engine->isActuallyPlaying() && !m_engine->isFadingOut())
        return true;
    return m_playbackStatus == QStringLiteral("Playing");
}

#include "systemmediacontroller_linux.moc"
