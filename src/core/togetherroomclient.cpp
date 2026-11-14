#include "core/togetherroomclient.h"

#include "core/apiclient.h"
#include "theme/theme.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QWebSocket>
#include <algorithm>

TogetherRoomClient::TogetherRoomClient(QObject *parent)
    : QObject(parent)
    , m_api(new ApiClient(this))
    , m_socket(nullptr)
    , m_reconnectTimer(new QTimer(this))
    , m_clockTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(2000);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TogetherRoomClient::reconnectSocket);

    m_clockTimer->setInterval(15000);
    connect(m_clockTimer, &QTimer::timeout, this, &TogetherRoomClient::sendClockPing);
}

TogetherRoomClient &TogetherRoomClient::instance()
{
    static TogetherRoomClient client;
    return client;
}

QString TogetherRoomClient::roomUrl() const
{
    if (m_roomToken.isEmpty())
        return {};
    return QString::fromUtf8("%1/together/%2").arg(Theme::kApiBase, m_roomToken);
}

QVariantMap TogetherRoomClient::playlist() const
{
    return m_room.value(QStringLiteral("playlist")).toMap();
}

QVariantMap TogetherRoomClient::playback() const
{
    return m_room.value(QStringLiteral("playback")).toMap();
}

QVariantList TogetherRoomClient::members() const
{
    return m_room.value(QStringLiteral("members")).toList();
}

QString TogetherRoomClient::currentEntryId() const
{
    return playback().value(QStringLiteral("currentEntryId")).toString();
}

qint64 TogetherRoomClient::estimatedPositionMs() const
{
    const QVariantMap pb = playback();
    qint64 position = pb.value(QStringLiteral("positionMs")).toLongLong();
    if (!pb.value(QStringLiteral("playing")).toBool())
        return position;
    const qint64 effectiveAt = pb.value(QStringLiteral("effectiveAt")).toLongLong();
    const qint64 nowServer = QDateTime::currentMSecsSinceEpoch() + m_serverOffsetMs;
    return position + qMax<qint64>(0, nowServer - effectiveAt);
}

void TogetherRoomClient::createWithMusic(int musicId)
{
    if (isActive())
        leaveRoom();
    m_api->createTogetherRoom([this, musicId](bool ok, const QString &message, const QString &url) {
        if (!ok) {
            emit errorOccurred(message.isEmpty() ? QStringLiteral("创建一起听房间失败") : message);
            return;
        }
        const QUrl roomUrlValue(url);
        const QString token = roomUrlValue.path().section(QLatin1Char('/'), -1);
        if (token.isEmpty()) {
            emit errorOccurred(QStringLiteral("创建一起听房间失败"));
            return;
        }
        m_roomToken = token;
        m_pendingCreatedUrl = url;
        joinRoomInternal(token, musicId);
    });
}

void TogetherRoomClient::joinRoom(const QString &token)
{
    if (token.isEmpty())
        return;
    if (isActive())
        leaveRoom();
    m_roomToken = token;
    joinRoomInternal(token);
}

void TogetherRoomClient::joinRoomInternal(const QString &token, int pendingMusicId)
{
    m_closing = false;
    m_api->joinTogetherRoom(token, [this, token, pendingMusicId](
                                            bool ok, const QString &message,
                                            const ApiClient::TogetherJoinResult &joined) {
        if (!ok) {
            resetState();
            emit errorOccurred(message.isEmpty() ? QStringLiteral("加入一起听房间失败") : message);
            return;
        }
        m_roomToken = token;
        m_room = joined.room;
        m_bestClockRttMs = std::numeric_limits<qint64>::max();
        m_serverOffsetMs = 0;
        m_clockSentAt.clear();
        emit activeChanged(true);
        emit stateChanged();
        connectSocket(joined.ticket);
        if (pendingMusicId > 0) {
            if (playlist().value(QStringLiteral("items")).toList().isEmpty()) {
                sendControl(QStringLiteral("playlist.add"),
                            [pendingMusicId](QJsonObject &body) {
                                body.insert(QStringLiteral("trackId"), pendingMusicId);
                            });
            }
            sendControl(QStringLiteral("playback.play"));
        }
        if (!m_pendingCreatedUrl.isEmpty()) {
            emit roomUrlReady(m_pendingCreatedUrl);
            m_pendingCreatedUrl.clear();
        }
    });
}

void TogetherRoomClient::leaveRoom()
{
    const QString token = m_roomToken;
    resetState();
    if (!token.isEmpty())
        m_api->leaveTogetherRoom(token, [](bool, const QString &) {});
}

void TogetherRoomClient::closeRoom()
{
    const QString token = m_roomToken;
    resetState();
    if (!token.isEmpty())
        m_api->closeTogetherRoom(token, [](bool, const QString &) {});
}

void TogetherRoomClient::playTrack(int musicId)
{
    if (musicId <= 0)
        return;
    sendControl(QStringLiteral("playback.playTrack"), [musicId](QJsonObject &body) {
        body.insert(QStringLiteral("trackId"), musicId);
    });
}

void TogetherRoomClient::playTracks(const QList<int> &musicIds, int startIndex)
{
    QList<int> ids;
    for (int id : musicIds) {
        if (id > 0 && !ids.contains(id))
            ids.append(id);
        if (ids.size() >= 200)
            break;
    }
    if (ids.isEmpty())
        return;
    const int boundedStart = qBound(0, startIndex, ids.size() - 1);
    QJsonArray trackIds;
    for (int id : ids)
        trackIds.append(id);
    sendControl(QStringLiteral("playlist.playList"), [trackIds, boundedStart](QJsonObject &body) {
        body.insert(QStringLiteral("trackIds"), trackIds);
        body.insert(QStringLiteral("startIndex"), boundedStart);
    });
}

void TogetherRoomClient::addTrack(int musicId)
{
    if (musicId <= 0)
        return;
    sendControl(QStringLiteral("playlist.add"), [musicId](QJsonObject &body) {
        body.insert(QStringLiteral("trackId"), musicId);
    });
}

void TogetherRoomClient::addTracks(const QList<int> &musicIds)
{
    QList<int> ids;
    for (int id : musicIds) {
        if (id > 0 && !ids.contains(id))
            ids.append(id);
        if (ids.size() >= 200)
            break;
    }
    for (int id : ids)
        addTrack(id);
}

void TogetherRoomClient::queueAsNext(int musicId)
{
    if (musicId <= 0)
        return;
    sendControl(QStringLiteral("playlist.addNext"), [musicId](QJsonObject &body) {
        body.insert(QStringLiteral("trackId"), musicId);
    });
}

void TogetherRoomClient::togglePlayPause()
{
    if (playback().value(QStringLiteral("playing")).toBool())
        pause();
    else
        resume();
}

void TogetherRoomClient::resume()
{
    sendControl(QStringLiteral("playback.play"), [this](QJsonObject &body) {
        const QString entryId = currentEntryId();
        if (!entryId.isEmpty())
            body.insert(QStringLiteral("entryId"), entryId);
    });
}

void TogetherRoomClient::pause()
{
    sendControl(QStringLiteral("playback.pause"));
}

void TogetherRoomClient::next()
{
    sendControl(QStringLiteral("playback.next"));
}

void TogetherRoomClient::previous()
{
    sendControl(QStringLiteral("playback.prev"));
}

void TogetherRoomClient::seekTo(qint64 positionMs)
{
    sendControl(QStringLiteral("playback.seek"), [positionMs](QJsonObject &body) {
        body.insert(QStringLiteral("positionMs"), qMax<qint64>(0, positionMs));
    });
}

void TogetherRoomClient::togglePlayMode()
{
    const QString mode = playlist().value(QStringLiteral("repeatMode")).toString();
    const QString nextMode = mode == QStringLiteral("list_repeat")
        ? QStringLiteral("single_repeat")
        : mode == QStringLiteral("single_repeat") ? QStringLiteral("shuffle")
                                                  : QStringLiteral("list_repeat");
    sendControl(QStringLiteral("playlist.mode"), [nextMode](QJsonObject &body) {
        body.insert(QStringLiteral("mode"), nextMode);
    });
}

void TogetherRoomClient::setPlayMode(const QString &mode)
{
    if (mode != QStringLiteral("list_repeat")
        && mode != QStringLiteral("single_repeat")
        && mode != QStringLiteral("shuffle")) {
        return;
    }
    sendControl(QStringLiteral("playlist.mode"), [mode](QJsonObject &body) {
        body.insert(QStringLiteral("mode"), mode);
    });
}

void TogetherRoomClient::selectEntry(const QString &entryId)
{
    if (entryId.isEmpty())
        return;
    sendControl(QStringLiteral("playback.select"), [entryId](QJsonObject &body) {
        body.insert(QStringLiteral("entryId"), entryId);
    });
}

void TogetherRoomClient::removeEntry(const QString &entryId)
{
    if (entryId.isEmpty())
        return;
    sendControl(QStringLiteral("playlist.remove"), [this, entryId](QJsonObject &body) {
        body.insert(QStringLiteral("entryId"), entryId);
        body.insert(QStringLiteral("playlistRevision"),
                    playlist().value(QStringLiteral("revision")).toLongLong());
    });
}

void TogetherRoomClient::clearPlaylist()
{
    sendControl(QStringLiteral("playlist.clear"));
}

void TogetherRoomClient::connectSocket(const QString &ticket)
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->deleteLater();
    }
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected, this, [this]() {
        m_connected = true;
        emit connectionChanged(true);
        flushPendingControls();
        sendClockPing();
        m_clockTimer->start();
    });
    connect(m_socket, &QWebSocket::disconnected, this, [this]() {
        m_connected = false;
        emit connectionChanged(false);
        m_clockTimer->stop();
        if (isActive() && !m_closing)
            m_reconnectTimer->start();
    });
    connect(m_socket, &QWebSocket::textMessageReceived,
            this, &TogetherRoomClient::handleTextMessage);
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_socket)
            emit errorOccurred(m_socket->errorString());
    });
    m_socket->open(QUrl(socketUrl(ticket)));
}

void TogetherRoomClient::reconnectSocket()
{
    if (!isActive() || m_closing)
        return;
    m_api->issueTogetherTicket(m_roomToken, [this](bool ok, const QString &message,
                                                   const QString &ticket) {
        if (!ok) {
            emit errorOccurred(message.isEmpty() ? QStringLiteral("一起听重连失败") : message);
            m_reconnectTimer->start();
            return;
        }
        connectSocket(ticket);
    });
}

void TogetherRoomClient::handleTextMessage(const QString &text)
{
    const QJsonObject obj = QJsonDocument::fromJson(text.toUtf8()).object();
    if (obj.isEmpty())
        return;
    applyEvent(obj.toVariantMap());
}

void TogetherRoomClient::applyEvent(const QVariantMap &event)
{
    const qint64 eventServerTime = event.value(QStringLiteral("serverTime")).toLongLong();
    if (eventServerTime > 0) {
        // 先给播放进度一个可用锚点；后续 clock 往返样本会用中点偏差做更精确校准。
        m_serverOffsetMs = eventServerTime - QDateTime::currentMSecsSinceEpoch();
    }
    const QString type = event.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("snapshot")) {
        m_room = event;
        const QString token = event.value(QStringLiteral("token")).toString();
        if (!token.isEmpty())
            m_roomToken = token;
        emit stateChanged();
        emit playbackChanged();
        return;
    }
    if (type == QStringLiteral("room.updated")) {
        const QStringList keys = {
            QStringLiteral("ownerUserId"), QStringLiteral("memberCount"),
            QStringLiteral("maxMembers"), QStringLiteral("members"),
            QStringLiteral("revision")
        };
        for (const QString &key : keys) {
            if (event.contains(key))
                m_room[key] = event.value(key);
        }
        emit stateChanged();
        return;
    }
    if (type == QStringLiteral("playlist.updated")) {
        m_room[QStringLiteral("playlist")] = event.value(QStringLiteral("playlist"));
        m_room[QStringLiteral("revision")] = event.value(QStringLiteral("revision"));
        emit stateChanged();
        return;
    }
    if (type == QStringLiteral("playback.updated")) {
        if (event.contains(QStringLiteral("playlist")))
            m_room[QStringLiteral("playlist")] = event.value(QStringLiteral("playlist"));
        m_room[QStringLiteral("playback")] = event.value(QStringLiteral("playback"));
        m_room[QStringLiteral("revision")] = event.value(QStringLiteral("revision"));
        emit stateChanged();
        emit playbackChanged();
        return;
    }
    if (type == QStringLiteral("clock")) {
        const QString requestId = event.value(QStringLiteral("requestId")).toString();
        const qint64 sentAt = m_clockSentAt.take(requestId);
        if (sentAt <= 0)
            return;
        const qint64 receivedAt = QDateTime::currentMSecsSinceEpoch();
        const qint64 rtt = qMax<qint64>(0, receivedAt - sentAt);
        const qint64 sampleOffset = event.value(QStringLiteral("serverTime")).toLongLong()
            - (sentAt + rtt / 2);
        if (rtt <= m_bestClockRttMs || rtt <= m_bestClockRttMs + 20) {
            m_bestClockRttMs = qMin(m_bestClockRttMs, rtt);
            m_serverOffsetMs = sampleOffset;
            emit playbackChanged();
        }
        return;
    }
    if (type == QStringLiteral("error")) {
        QString message = event.value(QStringLiteral("message")).toString();
        if (message.isEmpty())
            message = event.value(QStringLiteral("code")).toString();
        emit errorOccurred(message);
        return;
    }
    if (type == QStringLiteral("room.closed")) {
        resetState();
        emit roomClosed();
    }
}

void TogetherRoomClient::resetState()
{
    m_closing = true;
    m_reconnectTimer->stop();
    m_clockTimer->stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    const bool wasActive = isActive();
    m_roomToken.clear();
    m_room.clear();
    m_pendingCreatedUrl.clear();
    m_pendingControls.clear();
    m_connected = false;
    m_clockSentAt.clear();
    m_serverOffsetMs = 0;
    m_bestClockRttMs = std::numeric_limits<qint64>::max();
    m_closing = false;
    emit connectionChanged(false);
    if (wasActive)
        emit activeChanged(false);
    emit stateChanged();
}

void TogetherRoomClient::sendControl(const QString &action,
                                     const std::function<void(QJsonObject &)> &fill)
{
    QJsonObject body;
    body.insert(QStringLiteral("type"), QStringLiteral("control"));
    body.insert(QStringLiteral("action"), action);
    body.insert(QStringLiteral("opId"),
                QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (fill)
        fill(body);
    if (!m_connected || !m_socket) {
        // 加入房间后 WebSocket 往往仍处于握手阶段；创建房间时紧接着发出的
        // “加入当前歌曲 + 播放”不能在这里丢掉，等 connected 后按顺序补发。
        if (isActive() && !m_closing) {
            if (m_pendingControls.size() >= 64)
                m_pendingControls.removeFirst();
            m_pendingControls.append(body);
        }
        return;
    }
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)));
}

void TogetherRoomClient::flushPendingControls()
{
    if (!m_connected || !m_socket)
        return;
    const QList<QJsonObject> pending = m_pendingControls;
    m_pendingControls.clear();
    for (const QJsonObject &body : pending) {
        m_socket->sendTextMessage(
            QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)));
    }
}

void TogetherRoomClient::sendClockPing()
{
    if (!m_connected || !m_socket)
        return;
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const qint64 sentAt = QDateTime::currentMSecsSinceEpoch();
    m_clockSentAt.insert(requestId, sentAt);
    while (m_clockSentAt.size() > 8)
        m_clockSentAt.erase(m_clockSentAt.begin());
    QJsonObject body;
    body.insert(QStringLiteral("type"), QStringLiteral("clock"));
    body.insert(QStringLiteral("requestId"), requestId);
    body.insert(QStringLiteral("clientTime"), sentAt);
    m_socket->sendTextMessage(QString::fromUtf8(QJsonDocument(body).toJson(QJsonDocument::Compact)));
}

QString TogetherRoomClient::socketUrl(const QString &ticket) const
{
    QUrl url(QString::fromUtf8(Theme::kApiBase));
    url.setScheme(url.scheme() == QStringLiteral("https") ? QStringLiteral("wss")
                                                         : QStringLiteral("ws"));
    url.setPath(QStringLiteral("/api/listen/ws"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ticket"), ticket);
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}
