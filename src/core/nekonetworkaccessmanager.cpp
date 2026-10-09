/**
 * @file nekonetworkaccessmanager.cpp
 * @brief 统一附加本站客户端标识与防重放 nonce 的网络管理器实现
 */

#include "core/nekonetworkaccessmanager.h"
#include "core/replaynonce.h"
#include "theme/theme.h"
#include "version.h"

#include <QBuffer>
#include <QNetworkReply>
#include <cstring>
#include <QSet>
#include <QStringList>
#include <QUrl>

namespace {

/** 标记「内部重发」请求，避免被防重放层二次包装。 */
constexpr QNetworkRequest::Attribute kReplayInternalAttribute =
        static_cast<QNetworkRequest::Attribute>(QNetworkRequest::User + 0x4E4B);

/** 允许携带 nonce 的请求方法（与后端一致：GET 读，POST/PUT/DELETE 写）。 */
bool isReplayMethodSupported(QNetworkAccessManager::Operation op)
{
    switch (op) {
    case QNetworkAccessManager::GetOperation:
    case QNetworkAccessManager::PostOperation:
    case QNetworkAccessManager::PutOperation:
    case QNetworkAccessManager::DeleteOperation:
        return true;
    default:
        return false;
    }
}

bool isWriteOperation(QNetworkAccessManager::Operation op)
{
    switch (op) {
    case QNetworkAccessManager::PostOperation:
    case QNetworkAccessManager::PutOperation:
    case QNetworkAccessManager::DeleteOperation:
        return true;
    default:
        return false;
    }
}

/** 请求是否发往本站后端（与 Theme::kApiBase 同主机，大小写不敏感）。 */
bool isBackendRequest(const QUrl &url)
{
    const QUrl base(QString::fromUtf8(Theme::kApiBase));
    if (!base.isValid() || base.host().isEmpty()) {
        return true; // 后端未配置：保守带上标识，交服务端自行判断
    }
    return url.host().compare(base.host(), Qt::CaseInsensitive) == 0;
}

/** 归一化路径：去掉尾部斜杠，避免 `/api/replay/nonce/` 之类写法绕过豁免。 */
QString normalizedPath(const QUrl &url)
{
    QString path = url.path();
    while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

/**
 * 是否需要防重放 nonce。
 *
 * 豁免清单必须与后端 `filter/ReplayProtectionFilter` 一致：不一致只会多领 nonce，不会误拦。
 */
bool isReplayProtected(const QNetworkRequest &request, QNetworkAccessManager::Operation op)
{
    if (!isReplayMethodSupported(op))
        return false;

    QString contentType = request.header(QNetworkRequest::ContentTypeHeader).toString();
    if (contentType.isEmpty())
        contentType = QString::fromUtf8(request.rawHeader("Content-Type"));
    if (contentType.startsWith(QLatin1String("multipart/"), Qt::CaseInsensitive))
        return false; // 上传类：转发前无法整体校验请求体

    const QString path = normalizedPath(request.url());
    if (path.isEmpty())
        return false;

    static const QSet<QString> exemptPaths = {
        QStringLiteral("/api/replay/challenge"), // 领 nonce 的第一步，同样必须自举
        QStringLiteral("/api/replay/nonce"),
        QStringLiteral("/api/music/latest"),
        QStringLiteral("/api/music/ranking"),
        QStringLiteral("/api/payment/zpay/notify"),
        QStringLiteral("/api/user/qrlogin/status"),
        QStringLiteral("/api/user/notifications/stream"), // 站内消息 SSE：长连接不能被整包缓冲
    };
    if (exemptPaths.contains(path))
        return false;

    static const QStringList exemptPrefixes = {
        QStringLiteral("/api/music/cover/"),
        QStringLiteral("/api/user/avatar/"),
    };
    for (const QString &prefix : exemptPrefixes) {
        if (path.startsWith(prefix))
            return false;
    }

    // /loser/*/pull 为 SSE 进度流（长连接）
    if (path.endsWith(QLatin1String("/pull")))
        return false;

    if (path == QLatin1String("/version"))
        return true;
    return path.startsWith(QLatin1String("/api/")) || path.startsWith(QLatin1String("/loser/"));
}

/**
 * @brief 防重放回复：一次逻辑请求 = 取 nonce 发送（必要时换新 nonce 重试一次）
 *
 * 请求体在构造时已缓冲（受保护接口都是小型 JSON），因此重试无需回绕调用方的 QIODevice。
 * 响应同样先缓冲、再一次性投递（readyRead + finished），调用方行为与普通回复一致：
 * 读 `error()`、`attribute(HttpStatusCodeAttribute)`、`readAll()` 均可用。
 */
class ReplayGuardedReply : public QNetworkReply
{
public:
    ReplayGuardedReply(NekoNetworkAccessManager *nam, QNetworkAccessManager::Operation op,
                       const QNetworkRequest &request, const QByteArray &body)
        : QNetworkReply(nam), m_nam(nam), m_op(op), m_request(request), m_body(body)
    {
        setOperation(op);
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        startAttempt();
    }

    ~ReplayGuardedReply() override
    {
        // 调用方可能在收到响应前就丢弃本回复（换歌 / 取消）：先断开回调再中止内层请求，
        // 避免在析构过程中回调到半销毁的自身。
        if (m_inner) {
            QNetworkReply *inner = m_inner;
            m_inner = nullptr;
            inner->disconnect(this);
            inner->abort();
            // QNetworkAccessManager 不持有回复所有权，中止后必须自行回收，否则直到管理器
            // 销毁前都不会释放（换歌 / 取消会不断产生这类请求）。
            inner->deleteLater();
        }
    }

    void abort() override
    {
        m_aborted = true;
        if (m_inner && !m_inner->isFinished()) {
            m_inner->abort(); // finished 回调里统一收尾
            return;
        }
        finalize();
    }

    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 remaining = m_body.size() - m_readOffset;
        if (remaining <= 0)
            return 0;
        const qint64 count = qMin(maxSize, remaining);
        std::memcpy(data, m_body.constData() + m_readOffset, static_cast<size_t>(count));
        m_readOffset += count;
        return count;
    }

    qint64 bytesAvailable() const override
    {
        return (m_body.size() - m_readOffset) + QIODevice::bytesAvailable();
    }

    bool isSequential() const override { return true; }

private:
    /** 发送一次：每次尝试都取一个全新的 nonce。 */
    void startAttempt()
    {
        QNetworkRequest req = m_request;
        req.setAttribute(kReplayInternalAttribute, true);

        const QString nonce = ReplayNonceStore::instance().take(isWriteOperation(m_op));
        if (!nonce.isEmpty())
            req.setRawHeader(ReplayNonceStore::nonceHeader(), nonce.toUtf8());

        QBuffer *buffer = nullptr;
        if (!m_body.isEmpty()) {
            buffer = new QBuffer(this);
            buffer->setData(m_body);
            buffer->open(QIODevice::ReadOnly);
        }

        m_inner = m_nam->issueInternal(m_op, req, buffer);
        if (!m_inner) {
            finalize();
            return;
        }
        connect(m_inner, &QNetworkReply::finished, this, [this]() { onInnerFinished(); });
    }

    void onInnerFinished()
    {
        QNetworkReply *inner = m_inner;
        if (!inner) {
            finalize();
            return;
        }

        const int status = inner->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray replayStatus = inner->rawHeader(ReplayNonceStore::statusHeader());
        const bool replayRejected = !m_aborted && status == 409
                && (replayStatus == QByteArrayLiteral("missing")
                    || replayStatus == QByteArrayLiteral("invalid"));

        if (replayRejected && m_attemptCount < kMaxAttempts) {
            m_attemptCount++;
            qInfo() << "[replay] 防重放校验失败，换新 nonce 重试:"
                    << normalizedPath(m_request.url());
            inner->deleteLater();
            m_inner = nullptr;
            ReplayNonceStore::instance().noteReplayRejected();
            startAttempt();
            return;
        }

        m_body = inner->readAll();
        m_readOffset = 0;
        copyFrom(inner);
        inner->deleteLater();
        m_inner = nullptr;
        finalize();
    }

    /** 把内层回复的状态、头部与错误搬到自己身上。 */
    void copyFrom(QNetworkReply *inner)
    {
        const QVariant status = inner->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (status.isValid())
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        const QVariant reason = inner->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
        if (reason.isValid())
            setAttribute(QNetworkRequest::HttpReasonPhraseAttribute, reason);

        const auto pairs = inner->rawHeaderPairs();
        for (const auto &pair : pairs)
            setRawHeader(pair.first, pair.second);

        for (int header = 0; header < QNetworkRequest::NumKnownHeaders; ++header) {
            const auto known = static_cast<QNetworkRequest::KnownHeaders>(header);
            const QVariant value = inner->header(known);
            if (value.isValid())
                setHeader(known, value);
        }

        if (m_aborted && inner->error() == QNetworkReply::NoError)
            setError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation canceled"));
        else if (inner->error() != QNetworkReply::NoError)
            setError(inner->error(), inner->errorString());
    }

    void finalize()
    {
        if (m_delivered)
            return;
        m_delivered = true;
        setFinished(true);
        emit metaDataChanged();
        emit readyRead();
        emit finished();
    }

    static constexpr int kMaxAttempts = 1;

    NekoNetworkAccessManager *m_nam = nullptr;
    QNetworkAccessManager::Operation m_op = QNetworkAccessManager::GetOperation;
    QNetworkRequest m_request;
    QNetworkReply *m_inner = nullptr;
    QByteArray m_body;
    qint64 m_readOffset = 0;
    int m_attemptCount = 0;
    bool m_aborted = false;
    bool m_delivered = false;
};

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
        // 强制统一 UA，不保留调用方自定义 UA，确保全链路客户端标识严格一致。
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("NekoMusic-PC/%1").arg(version));
        // X-Neko-Client 同样强制统一。
        req.setRawHeader("X-Neko-Client",
                         QStringLiteral("pc+%1").arg(version).toUtf8());
    }

    // 防重放：只包装发往本站的受保护路径；静态资源 / 豁免接口保持原样（流式、可缓存）。
    if (req.attribute(kReplayInternalAttribute).toBool()
        || !isBackendRequest(req.url())
        || !isReplayProtected(req, op)) {
        return QNetworkAccessManager::createRequest(op, req, outgoingData);
    }

    QByteArray body;
    if (outgoingData) {
        if (outgoingData->isSequential()) {
            // 流式上传无法安全重放，交回基类（不附带重试）
            return QNetworkAccessManager::createRequest(op, req, outgoingData);
        }
        body = outgoingData->readAll();
        outgoingData->seek(0);
    }
    return new ReplayGuardedReply(this, op, req, body);
}
