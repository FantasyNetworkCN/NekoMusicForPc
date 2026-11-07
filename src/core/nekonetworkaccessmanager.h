#pragma once

/**
 * @file nekonetworkaccessmanager.h
 * @brief 统一附加本站请求标头与用户令牌的网络管理器
 */

#include <QNetworkAccessManager>
#include <QNetworkRequest>

class QIODevice;

/**
 * @brief 为发往**本站后端**的请求统一注入 User-Agent 与 Authorization
 */
class NekoNetworkAccessManager : public QNetworkAccessManager
{
    Q_OBJECT
public:
    explicit NekoNetworkAccessManager(QObject *parent = nullptr);

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override;
};
