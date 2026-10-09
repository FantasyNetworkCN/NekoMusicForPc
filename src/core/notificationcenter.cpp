/**
 * @file notificationcenter.cpp
 * @brief 站内消息中心实现
 */

#include "core/notificationcenter.h"
#include "core/apiclient.h"
#include "core/usermanager.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QtGlobal>

namespace {
/** 重连间隔：与服务端下发的 retry 提示（3 秒）保持一致 */
constexpr int kReconnectDelayMs = 3000;
/** 令牌失效 / 未登录：重连没有意义，直接停掉 */
constexpr int kUnauthorizedStatus = 401;
/** 连接数超限：服务端给了 Retry-After，照它等，别猛打 */
constexpr int kTooManyRequestsStatus = 429;
/** 断线补拉一次最多取这么多条，够用又不至于把列表接口拖垮 */
constexpr int kBackfillLimit = 50;
} // namespace

NotificationCenter &NotificationCenter::instance()
{
    static NotificationCenter center;
    return center;
}

NotificationCenter::NotificationCenter(QObject *parent) : QObject(parent)
{
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &NotificationCenter::startStream);

    connect(&UserManager::instance(), &UserManager::loginStateChanged, this,
            &NotificationCenter::onLoginStateChanged);
}

NotificationCenter::~NotificationCenter()
{
    stopStream();
}

void NotificationCenter::setApiClient(ApiClient *api)
{
    m_api = api;
    if (m_api && UserManager::instance().isLoggedIn())
        startStream();
}

void NotificationCenter::applyUnread(int unread)
{
    const int next = qMax(0, unread);
    if (next == m_unread)
        return;
    m_unread = next;
    emit unreadChanged(m_unread);
}

void NotificationCenter::onLoginStateChanged()
{
    if (UserManager::instance().isLoggedIn()) {
        startStream();
        return;
    }
    stopStream();
    applyUnread(0);
    // 换账号后重新记基线，避免把上个账号的游标带过去
    m_hasBaseline = false;
    m_lastSeenId = 0;
}

void NotificationCenter::scheduleReconnect(int delayMs)
{
    if (!m_api || !UserManager::instance().isLoggedIn())
        return;
    m_reconnectTimer->start(delayMs > 0 ? delayMs : kReconnectDelayMs);
}

void NotificationCenter::stopStream()
{
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    if (!m_stream)
        return;

    QNetworkReply *reply = m_stream;
    m_stream = nullptr;
    // 先断开回调再中止：避免 finished 回调里再触发一次重连
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void NotificationCenter::startStream()
{
    if (!m_api || !UserManager::instance().isLoggedIn())
        return;
    stopStream();

    QNetworkReply *reply = m_api->streamNotifications(
        [this](int unread, int latestId) {
            onStreamReady(unread, latestId);
        },
        [this](const QVariantMap &item) {
            if (!item.value(QStringLiteral("read")).toBool())
                applyUnread(m_unread + 1);
            m_lastSeenId = qMax(m_lastSeenId, item.value(QStringLiteral("id")).toInt());
            emit messageReceived(item);
        });
    if (!reply) {
        scheduleReconnect();
        return;
    }
    m_stream = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_stream == reply)
            m_stream = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = false;
        const int retryAfter = reply->rawHeader("Retry-After").toInt(&ok);
        reply->deleteLater();
        if (status == kUnauthorizedStatus)
            return; // 令牌失效 / 已登出，重连只会白打
        if (status == kTooManyRequestsStatus && ok && retryAfter > 0) {
            scheduleReconnect(retryAfter * 1000);
            return;
        }
        scheduleReconnect();
    });
}

/**
 * 每次 `ready` 都校准游标：比本地新就说明断线窗口里落了消息，补拉一次。
 *
 * 首次连上只记基线——否则一登录就会把历史消息全弹成系统通知。
 */
void NotificationCenter::onStreamReady(int unread, int latestId)
{
    applyUnread(unread);

    const bool firstConnect = !m_hasBaseline;
    m_hasBaseline = true;
    if (firstConnect) {
        m_lastSeenId = qMax(m_lastSeenId, latestId);
        emit readyReceived(unread, latestId);
        return;
    }

    if (latestId > m_lastSeenId)
        backfillMissed(m_lastSeenId);
    m_lastSeenId = qMax(m_lastSeenId, latestId);
    emit readyReceived(unread, latestId);
}

void NotificationCenter::backfillMissed(int sinceId)
{
    if (!m_api)
        return;
    m_api->fetchNotifications(sinceId, 0, kBackfillLimit,
                              [this](bool ok, const QString &, const QVariantMap &data) {
                                  if (!ok)
                                      return;
                                  const QVariantList items =
                                      data.value(QStringLiteral("items")).toList();
                                  if (items.isEmpty())
                                      return;
                                  for (const QVariant &entry : items) {
                                      m_lastSeenId = qMax(
                                          m_lastSeenId,
                                          entry.toMap().value(QStringLiteral("id")).toInt());
                                  }
                                  emit missedMessages(items);
                              });
}
