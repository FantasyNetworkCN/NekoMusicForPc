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
}

void NotificationCenter::scheduleReconnect()
{
    if (!m_api || !UserManager::instance().isLoggedIn())
        return;
    m_reconnectTimer->start(kReconnectDelayMs);
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
            applyUnread(unread);
            emit readyReceived(unread, latestId);
        },
        [this](const QVariantMap &item) {
            if (!item.value(QStringLiteral("read")).toBool())
                applyUnread(m_unread + 1);
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
        reply->deleteLater();
        if (status == kUnauthorizedStatus)
            return; // 令牌失效 / 已登出，重连只会白打
        scheduleReconnect();
    });
}
