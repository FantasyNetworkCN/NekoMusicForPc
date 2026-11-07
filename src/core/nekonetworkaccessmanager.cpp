/**
 * @file nekonetworkaccessmanager.cpp
 * @brief 统一附加 User-Agent 与用户令牌的网络管理器实现
 */

#include "core/nekonetworkaccessmanager.h"
#include "core/usermanager.h"
#include "theme/theme.h"
#include "version.h"

#include <QUrl>

namespace {

bool isBackendRequest(const QUrl &url)
{
    const QUrl base(QString::fromUtf8(Theme::kApiBase));
    if (!base.isValid() || base.host().isEmpty())
        return true;
    return url.host().compare(base.host(), Qt::CaseInsensitive) == 0;
}

} // namespace

NekoNetworkAccessManager::NekoNetworkAccessManager(QObject *parent)
    : QNetworkAccessManager(parent)
{
}

QNetworkReply *NekoNetworkAccessManager::createRequest(Operation op,
                                                      const QNetworkRequest &request,
                                                      QIODevice *outgoingData)
{
    QNetworkRequest req(request);
    if (isBackendRequest(req.url())) {
        const QString version = QString::fromUtf8(APP_VERSION);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("NekoMusic-PC/%1").arg(version));
        const QString token = UserManager::instance().token();
        if (!token.isEmpty() && !req.hasRawHeader("Authorization")) {
            req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());
        }
    }
    return QNetworkAccessManager::createRequest(op, req, outgoingData);
}
