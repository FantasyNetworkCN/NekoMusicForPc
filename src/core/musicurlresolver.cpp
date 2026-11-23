/**
 * @file musicurlresolver.cpp
 * @brief 音质解析实现
 */

#include "core/musicurlresolver.h"
#include "theme/theme.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

namespace {

/** 媒体地址缓存时长：地址固定，过期只是重新解析一次。 */
constexpr qint64 kCacheTtlMs = 10 * 60 * 1000;

constexpr const char *kMusicFileApiPath = "/api/music/file/";

/** 后端返回的 `data.url` 可能是站内相对路径，这里补成绝对地址。 */
QUrl absoluteUrl(const QString &path)
{
    if (path.startsWith(QLatin1String("http://")) || path.startsWith(QLatin1String("https://")))
        return QUrl(path);
    const QString base = QString::fromUtf8(Theme::kApiBase);
    if (path.startsWith(QLatin1Char('/')))
        return QUrl(base + path);
    return QUrl(base + QLatin1Char('/') + path);
}

} // namespace

MusicUrlResolver &MusicUrlResolver::instance()
{
    static MusicUrlResolver resolver;
    return resolver;
}

MusicUrlResolver::MusicUrlResolver(QObject *parent)
    : QObject(parent)
{
}

bool MusicUrlResolver::isMusicFileApiUrl(const QUrl &url)
{
    return url.path().contains(QLatin1String(kMusicFileApiPath));
}

void MusicUrlResolver::resolve(const QUrl &apiUrl, QObject *context, Callback cb)
{
    if (!isMusicFileApiUrl(apiUrl)) {
        cb(true, apiUrl, 0, {});
        return;
    }

    const QString key = apiUrl.toString();
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.constEnd() && cached->expiresAt > QDateTime::currentMSecsSinceEpoch()) {
        cb(true, cached->url, 200, {});
        return;
    }

    QNetworkRequest req(apiUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setRawHeader("Cache-Control", "no-store");

    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, context ? context : this,
            [this, reply, key, apiUrl, cb]() {
        reply->deleteLater();

        const QByteArray body = reply->readAll();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject root = QJsonDocument::fromJson(body).object();
        const QString message = root.value(QStringLiteral("message")).toString();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            qWarning() << "[music-url] 解析音质地址失败:" << apiUrl.toString()
                       << status << reply->errorString();
            cb(false, apiUrl, status,
               message.isEmpty() ? reply->errorString() : message);
            return;
        }

        const QString path = root.value(QStringLiteral("data")).toObject()
                                 .value(QStringLiteral("url")).toString();
        if (!root.value(QStringLiteral("success")).toBool() || path.isEmpty()) {
            qWarning() << "[music-url] 解析音质地址失败:" << apiUrl.toString();
            cb(false, apiUrl, status,
               message.isEmpty() ? QStringLiteral("播放地址解析失败") : message);
            return;
        }

        const QUrl resolved = absoluteUrl(path);
        m_cache.insert(key, CacheEntry{resolved, QDateTime::currentMSecsSinceEpoch() + kCacheTtlMs});
        cb(true, resolved, status, {});
    });
}
