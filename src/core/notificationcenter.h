#pragma once

/**
 * @file notificationcenter.h
 * @brief 站内消息中心 — SSE 实时推送与未读数
 *
 * 消息本体在服务端落库，SSE 只是「在线时提前告知」：连上先收一帧 `ready`（未读数 + 当前最大 id），
 * 此后每来一条新消息收一帧 `message`。服务端到点会主动断开长连接（最长 300 秒），本类按固定间隔
 * 自动重连。
 *
 * **断线期间的消息服务端不重放**：重连后 `ready` 给的 `latestId` 比本地游标新，就说明这段窗口里
 * 落了消息，本类会自己走列表接口补拉一次并 `missedMessages` 通知出去。首次连上只记基线不补历史，
 * 否则一登录就会把旧消息全弹一遍。
 *
 * 未读数**只有**三个来源：`ready` 帧、`message` 帧自增、标记已读后的响应。不做任何轮询，
 * 与 Web 端约定一致。登录即连、登出即断。
 */

#include <QObject>
#include <QList>
#include <QVariantList>
#include <QVariantMap>

class ApiClient;
class QNetworkReply;
class QTimer;

class NotificationCenter : public QObject
{
    Q_OBJECT

public:
    static NotificationCenter &instance();

    /** 由 MainWindow 在创建 ApiClient 后注入（与 MusicDownloadManager 同一约定） */
    void setApiClient(ApiClient *api);

    int unreadCount() const { return m_unread; }

    /** 用服务端返回的权威未读数校准（列表 / 标记已读响应） */
    void applyUnread(int unread);

signals:
    void unreadChanged(int unread);
    /**
     * 连接建立（含自动重连）时下发一次。
     *
     * `latestId` 落后于本地游标说明断线期间漏了消息，调用方需自行走列表接口补拉；
     * 服务端不做重放。
     */
    void readyReceived(int unread, int latestId);
    /** 收到一条新消息（字段与收件箱列表里的单条一致） */
    void messageReceived(const QVariantMap &item);
    /**
     * 断线重连后补拉到的漏推消息（新的在前）。
     *
     * 只覆盖「上一个连接断开 → 本次连接建立」之间的窗口；首次连接不发。
     */
    void missedMessages(const QVariantList &items);

private:
    explicit NotificationCenter(QObject *parent = nullptr);
    ~NotificationCenter() override;

    void onLoginStateChanged();
    void onStreamReady(int unread, int latestId);
    void backfillMissed(int sinceId);
    void startStream();
    void stopStream();
    void scheduleReconnect(int delayMs = 0);

    ApiClient *m_api = nullptr;
    QNetworkReply *m_stream = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    int m_unread = 0;
    /** 已见过的最大消息 id，用来判断重连后有没有漏推 */
    int m_lastSeenId = 0;
    /** 首次连上的 ready 只当基线，不补历史 */
    bool m_hasBaseline = false;
};
