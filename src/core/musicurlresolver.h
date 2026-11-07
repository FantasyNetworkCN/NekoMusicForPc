#pragma once

/**
 * @file musicurlresolver.h
 * @brief 音质解析：`/api/music/file/{id}?quality=` → 站内固定媒体地址
 *
 * 后端该接口已由 `302` 重定向改为 `200` + JSON `data.url`，因此播放 /
 * 下载都不能再把接口地址直接交给播放器或下载器：必须先用普通请求换取真实媒体
 * 地址。真实媒体地址固定且可被 CDN 缓存，解析结果在进程内缓存一段时间，避免每次 seek /
 * 重试都回源。
 *
 * 所有请求都走 [NekoNetworkAccessManager]，User-Agent 由网络层统一注入。
 */

#include <QHash>
#include <QObject>
#include <QUrl>
#include <functional>

#include "core/nekonetworkaccessmanager.h"

class QNetworkReply;

class MusicUrlResolver : public QObject
{
    Q_OBJECT
public:
    /** ok=false 时 resolvedUrl 为原地址，调用方沿用既有错误路径即可。 */
    using Callback = std::function<void(bool ok, const QUrl &resolvedUrl)>;

    static MusicUrlResolver &instance();

    /** 是否为需要解析的音质接口地址。 */
    static bool isMusicFileApiUrl(const QUrl &url);

    /**
     * 解析（带缓存）。
     *
     * 命中缓存或无需解析时同步回调；否则发起请求，完成后回调。`context` 为回调的上下文对象
     * （通常是发起方），在其销毁后不再回调，避免悬垂指针。
     */
    void resolve(const QUrl &apiUrl, QObject *context, Callback cb);

private:
    explicit MusicUrlResolver(QObject *parent = nullptr);

    struct CacheEntry {
        QUrl url;
        qint64 expiresAt = 0;
    };

    NekoNetworkAccessManager m_nam;
    QHash<QString, CacheEntry> m_cache;
};
