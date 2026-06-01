#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#include "systemmediacontroller.h"
#include "core/playerengine.h"

#include <QTimer>
#include <QWidget>

static NSString *qToNs(const QString &s)
{
    if (s.isEmpty())
        return nil;
    const QByteArray utf8 = s.toUtf8();
    return [[NSString alloc] initWithBytes:utf8.constData()
                                    length:(NSUInteger)utf8.size()
                                  encoding:NSUTF8StringEncoding];
}

@interface NekoSystemMediaBridge : NSObject
@property (nonatomic, assign) SystemMediaController *ctrl;
- (instancetype)initWithCtrl:(SystemMediaController *)c;
- (void)teardown;
- (void)applyTrackTitle:(NSString *)title
                 artist:(NSString *)artist
                  album:(NSString *)album
                musicId:(int)musicId
               coverUrl:(NSString *)coverUrl
            durationSec:(double)durationSec;
- (void)applyPlaybackState:(int)state positionMs:(qint64)posMs playbackRate:(double)rate;
@end

@implementation NekoSystemMediaBridge

- (instancetype)initWithCtrl:(SystemMediaController *)c
{
    self = [super init];
    if (!self)
        return nil;
    _ctrl = c;
    MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];
    [cc.playCommand setEnabled:YES];
    [cc.pauseCommand setEnabled:YES];
    [cc.togglePlayPauseCommand setEnabled:YES];
    [cc.stopCommand setEnabled:YES];
    [cc.nextTrackCommand setEnabled:YES];
    [cc.previousTrackCommand setEnabled:YES];
    [cc.changePlaybackPositionCommand setEnabled:YES];

    [cc.playCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        if (self.ctrl)
            self.ctrl->firePlay();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [cc.pauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        if (self.ctrl)
            self.ctrl->firePause();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [cc.togglePlayPauseCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        if (self.ctrl)
            self.ctrl->firePlayPause();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [cc.stopCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        if (self.ctrl)
            self.ctrl->fireStop();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [cc.nextTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        if (self.ctrl)
            self.ctrl->fireNext();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [cc.previousTrackCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
        if (self.ctrl)
            self.ctrl->firePrevious();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
    [cc.changePlaybackPositionCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *ev) {
        if (!self.ctrl)
            return MPRemoteCommandHandlerStatusCommandFailed;
        if ([ev isKindOfClass:[MPChangePlaybackPositionCommandEvent class]]) {
            MPChangePlaybackPositionCommandEvent *pev = (MPChangePlaybackPositionCommandEvent *)ev;
            const qint64 us = static_cast<qint64>(pev.positionTime * 1000000.0);
            self.ctrl->fireSeekAbsolute(us);
            return MPRemoteCommandHandlerStatusSuccess;
        }
        return MPRemoteCommandHandlerStatusCommandFailed;
    }];
    return self;
}

- (void)teardown
{
    _ctrl = nil;
    MPNowPlayingInfoCenter *center = [MPNowPlayingInfoCenter defaultCenter];
    center.nowPlayingInfo = nil;
    center.playbackState = MPNowPlayingPlaybackStateStopped;
}

- (void)applyTrackTitle:(NSString *)title
                 artist:(NSString *)artist
                  album:(NSString *)album
                musicId:(int)musicId
               coverUrl:(NSString *)coverUrl
            durationSec:(double)durationSec
{
    Q_UNUSED(musicId);
    Q_UNUSED(coverUrl);
    NSMutableDictionary *info = [NSMutableDictionary dictionary];
    if (title)
        info[MPMediaItemPropertyTitle] = title;
    if (artist)
        info[MPMediaItemPropertyArtist] = artist;
    if (album)
        info[MPMediaItemPropertyAlbumTitle] = album;
    if (durationSec > 0.0)
        info[MPMediaItemPropertyPlaybackDuration] = @(durationSec);
    [MPNowPlayingInfoCenter defaultCenter].nowPlayingInfo = info;
}

- (void)applyPlaybackState:(int)state positionMs:(qint64)posMs playbackRate:(double)rate
{
    MPNowPlayingInfoCenter *center = [MPNowPlayingInfoCenter defaultCenter];
    MPNowPlayingPlaybackState ps = MPNowPlayingPlaybackStatePaused;
    if (state == static_cast<int>(PlayerEngine::Playing))
        ps = MPNowPlayingPlaybackStatePlaying;
    else if (state == static_cast<int>(PlayerEngine::Stopped))
        ps = MPNowPlayingPlaybackStateStopped;

    NSMutableDictionary *info = [center.nowPlayingInfo mutableCopy];
    if (!info)
        info = [NSMutableDictionary dictionary];
    info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(posMs / 1000.0);
    info[MPNowPlayingInfoPropertyPlaybackRate] = @(rate);
    center.nowPlayingInfo = info;
    center.playbackState = ps;
}

@end

SystemMediaController::SystemMediaController(QObject *parent)
    : QObject(parent)
{
    NekoSystemMediaBridge *b = [[NekoSystemMediaBridge alloc] initWithCtrl:this];
    m_macOpaque = (__bridge_retained void *)b;
}

SystemMediaController::~SystemMediaController()
{
    if (m_macOpaque) {
        NekoSystemMediaBridge *b = (__bridge_transfer NekoSystemMediaBridge *)m_macOpaque;
        [b teardown];
        m_macOpaque = nullptr;
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

void SystemMediaController::updateFromEngineState(PlayerEngine::PlaybackState state)
{
    QString next;
    switch (state) {
    case PlayerEngine::Playing:
        next = QStringLiteral("Playing");
        break;
    case PlayerEngine::Paused:
        next = QStringLiteral("Paused");
        break;
    default:
        next = QStringLiteral("Stopped");
        break;
    }
    m_playbackStatus = next;

    const qint64 pos = m_engine ? m_engine->position() : 0;
    const double rate = (state == PlayerEngine::Playing) ? 1.0 : 0.0;
    NekoSystemMediaBridge *b = (__bridge NekoSystemMediaBridge *)m_macOpaque;
    if (b)
        [b applyPlaybackState:static_cast<int>(state) positionMs:pos playbackRate:rate];

    if (state == PlayerEngine::Playing) {
        if (!m_positionTimer) {
            m_positionTimer = new QTimer(this);
            m_positionTimer->setInterval(900);
            connect(m_positionTimer, &QTimer::timeout, this, [this]() {
                if (!m_engine || m_playbackStatus != QStringLiteral("Playing"))
                    return;
                NekoSystemMediaBridge *br = (__bridge NekoSystemMediaBridge *)m_macOpaque;
                if (br)
                    [br applyPlaybackState:static_cast<int>(PlayerEngine::Playing)
                                positionMs:m_engine->position()
                            playbackRate:1.0];
            });
        }
        m_positionTimer->start();
    } else if (m_positionTimer) {
        m_positionTimer->stop();
    }
}

void SystemMediaController::updateMetadata(const MusicInfo &music, qint64 durationMs)
{
    NekoSystemMediaBridge *b = (__bridge NekoSystemMediaBridge *)m_macOpaque;
    if (!b)
        return;
    const double durSec = qMax(durationMs / 1000.0, static_cast<double>(music.duration));
    [b applyTrackTitle:qToNs(music.title)
                artist:qToNs(music.artist)
                 album:qToNs(music.album)
               musicId:music.id
              coverUrl:qToNs(music.coverUrl)
           durationSec:durSec];
}

void SystemMediaController::updateCapabilities(bool canNext, bool canPrev, bool canSeek)
{
    m_canGoNext = canNext;
    m_canGoPrevious = canPrev;
    m_canSeek = canSeek;
    MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];
    [cc.nextTrackCommand setEnabled:canNext];
    [cc.previousTrackCommand setEnabled:canPrev];
    [cc.changePlaybackPositionCommand setEnabled:canSeek];
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
    Q_UNUSED(m_volume);
}

void SystemMediaController::notifySeeked(qlonglong positionUs)
{
    Q_UNUSED(positionUs);
}

void SystemMediaController::onPositionMsChanged(qint64 positionMs)
{
    if (m_playbackStatus != QStringLiteral("Playing"))
        return;
    NekoSystemMediaBridge *b = (__bridge NekoSystemMediaBridge *)m_macOpaque;
    if (b)
        [b applyPlaybackState:static_cast<int>(PlayerEngine::Playing) positionMs:positionMs playbackRate:1.0];
}
