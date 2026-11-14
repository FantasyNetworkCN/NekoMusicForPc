#pragma once

#include <QObject>
#include <QList>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QJsonObject>
#include <functional>
#include <limits>

class QWebSocket;
class QTimer;
class ApiClient;

/**
 * PC 一起听会话。
 *
 * REST 负责创建/加入/ticket，QWebSocket 持有房间权威状态。PC 不提供独立房间页，
 * 播放页、PlayerBar 和播放队列抽屉都消费本类的状态。
 */
class TogetherRoomClient : public QObject
{
    Q_OBJECT
public:
    static TogetherRoomClient &instance();

    bool isActive() const { return !m_roomToken.isEmpty(); }
    bool isConnected() const { return m_connected; }
    QString roomToken() const { return m_roomToken; }
    QString roomUrl() const;
    QVariantMap roomState() const { return m_room; }
    QVariantMap playlist() const;
    QVariantMap playback() const;
    QVariantList members() const;
    QString currentEntryId() const;
    qint64 estimatedPositionMs() const;

    /** 创建房间并把当前在线歌曲加入空临时歌单后开始播放。 */
    void createWithMusic(int musicId);
    /** 通过随机 URL 的 token 加入房间。 */
    void joinRoom(const QString &token);
    void leaveRoom();
    void closeRoom();

    // 统一播放/列表操作
    void playTrack(int musicId);
    void playTracks(const QList<int> &musicIds, int startIndex);
    void addTrack(int musicId);
    void addTracks(const QList<int> &musicIds);
    void queueAsNext(int musicId);
    void togglePlayPause();
    void resume();
    void pause();
    void next();
    void previous();
    void seekTo(qint64 positionMs);
    void togglePlayMode();
    void setPlayMode(const QString &mode);
    void selectEntry(const QString &entryId);
    void removeEntry(const QString &entryId);
    void clearPlaylist();

signals:
    void stateChanged();
    void playbackChanged();
    void activeChanged(bool active);
    void connectionChanged(bool connected);
    void roomUrlReady(const QString &url);
    void errorOccurred(const QString &message);
    void roomClosed();

private:
    explicit TogetherRoomClient(QObject *parent = nullptr);

    void connectSocket(const QString &ticket);
    void reconnectSocket();
    void joinRoomInternal(const QString &token, int pendingMusicId = 0);
    void handleTextMessage(const QString &text);
    void applyEvent(const QVariantMap &event);
    void resetState();
    void sendControl(const QString &action,
                     const std::function<void(QJsonObject &)> &fill = {});
    void flushPendingControls();
    void sendClockPing();
    QString socketUrl(const QString &ticket) const;

    ApiClient *m_api = nullptr;
    QWebSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_clockTimer = nullptr;
    QString m_roomToken;
    QVariantMap m_room;
    QString m_pendingCreatedUrl;
    bool m_connected = false;
    bool m_closing = false;
    QList<QJsonObject> m_pendingControls;
    qint64 m_serverOffsetMs = 0;
    qint64 m_bestClockRttMs = std::numeric_limits<qint64>::max();
    QHash<QString, qint64> m_clockSentAt;
};
