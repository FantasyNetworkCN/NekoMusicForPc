/**
 * @file nekonetworkaccessmanager.cpp
 * @brief 统一附加客户端身份、一次性 nonce 与请求签名的网络管理器实现
 */

#include "core/nekonetworkaccessmanager.h"
#include "core/clientidentity.h"
#include "core/replaynonce.h"
#include "core/usermanager.h"
#include "theme/theme.h"
#include "version.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QNetworkReply>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <cstring>

namespace {

/** 标记「内部发送」请求，避免被 nonce / 身份包装层二次接管。 */
constexpr QNetworkRequest::Attribute kInternalRequestAttribute =
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
    if (!base.isValid() || base.host().isEmpty())
        return true; // 后端未配置：保守带上标识，交服务端自行判断
    return url.host().compare(base.host(), Qt::CaseInsensitive) == 0;
}

/** 归一化路径：去掉尾部斜杠，避免 `/api/replay/nonce/` 之类写法绕过豁免。 */
QString normalizedPath(const QUrl &url)
{
    QString path = url.path(QUrl::FullyDecoded);
    while (path.size() > 1 && path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

/** 签名要用实际发送的路径；检测豁免才去掉尾部斜杠。 */
QString signingPath(const QUrl &url)
{
    const QString path = url.path(QUrl::FullyDecoded);
    return path.isEmpty() ? QStringLiteral("/") : path;
}

bool isMultipart(const QNetworkRequest &request)
{
    QString contentType = request.header(QNetworkRequest::ContentTypeHeader).toString();
    if (contentType.isEmpty())
        contentType = QString::fromUtf8(request.rawHeader("Content-Type"));
    return contentType.startsWith(QLatin1String("multipart/"), Qt::CaseInsensitive);
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
    if (isMultipart(request))
        return false; // 上传类：转发前无法整体校验请求体

    const QString path = normalizedPath(request.url());
    if (path.isEmpty())
        return false;

    static const QSet<QString> exemptPaths = {
        QStringLiteral("/api/client/challenge"),
        QStringLiteral("/api/client/register"),
        QStringLiteral("/api/client/token"),
        QStringLiteral("/api/replay/challenge"), // 领 nonce 的第一步，同样必须自举
        QStringLiteral("/api/replay/nonce"),
        QStringLiteral("/api/music/latest"),
        QStringLiteral("/api/music/ranking"),
        QStringLiteral("/api/payment/zpay/notify"),
        QStringLiteral("/api/user/qrlogin/status"),
        QStringLiteral("/api/user/notifications/stream"), // 站内消息 SSE：长连接不能被整包缓冲
        QStringLiteral("/api/admin/logs/access/stream"),
        QStringLiteral("/api/user/upload/preview"),
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

    if (path.endsWith(QLatin1String("/pull")))
        return false;

    if (path == QLatin1String("/version"))
        return true;
    return path.startsWith(QLatin1String("/api/")) || path.startsWith(QLatin1String("/loser/"));
}

/** 与后端 `filter/ClientIdentityFilter` 的豁免清单保持一致。 */
bool isClientIdentityProtected(const QNetworkRequest &request, QNetworkAccessManager::Operation op)
{
    if (!isReplayMethodSupported(op))
        return false;
    const QString path = normalizedPath(request.url());
    if (path.isEmpty())
        return false;

    static const QSet<QString> exemptPaths = {
        QStringLiteral("/api/client/challenge"),
        QStringLiteral("/api/client/register"),
        QStringLiteral("/api/client/token"),
        QStringLiteral("/api/replay/challenge"),
        QStringLiteral("/api/replay/nonce"),
        QStringLiteral("/api/music/latest"),
        QStringLiteral("/api/music/ranking"),
        QStringLiteral("/api/payment/zpay/notify"),
        QStringLiteral("/api/user/qrlogin/status"),
        QStringLiteral("/api/user/notifications/stream"),
        QStringLiteral("/api/admin/logs/access/stream"),
        QStringLiteral("/api/user/upload/preview"),
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
    if (path.endsWith(QLatin1String("/pull")))
        return false;
    return path.startsWith(QLatin1String("/api/")) || path.startsWith(QLatin1String("/loser/"));
}

QString operationName(QNetworkAccessManager::Operation op)
{
    switch (op) {
    case QNetworkAccessManager::GetOperation: return QStringLiteral("GET");
    case QNetworkAccessManager::PostOperation: return QStringLiteral("POST");
    case QNetworkAccessManager::PutOperation: return QStringLiteral("PUT");
    case QNetworkAccessManager::DeleteOperation: return QStringLiteral("DELETE");
    default: return QStringLiteral("GET");
    }
}

/**
 * @brief 受保护请求回复：实例身份 + 一次性 nonce
 *
 * 请求体在构造时已缓冲（受保护接口都是小型 JSON），因此重试无需回绕调用方的 QIODevice。
 * 响应先缓冲、再一次性投递（readyRead + finished），调用方行为与普通回复一致。
 */
class ProtectedReply : public QNetworkReply
{
public:
    ProtectedReply(NekoNetworkAccessManager *nam, QNetworkAccessManager::Operation op,
                   const QNetworkRequest &request, const QByteArray &requestBody,
                   const QByteArray &bodyHash, QIODevice *streamingBody,
                   bool replayProtected, bool identityProtected)
        : QNetworkReply(nam)
        , m_nam(nam)
        , m_op(op)
        , m_request(request)
        , m_requestBody(requestBody)
        , m_bodyHash(bodyHash)
        , m_streamingBody(streamingBody)
        , m_replayProtected(replayProtected)
        , m_identityProtected(identityProtected)
    {
        setOperation(op);
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        startAttempt();
    }

    ~ProtectedReply() override
    {
        ++m_attemptSerial;
        if (m_inner) {
            QNetworkReply *inner = m_inner;
            m_inner = nullptr;
            inner->disconnect(this);
            inner->abort();
            inner->deleteLater();
        }
    }

    void abort() override
    {
        m_aborted = true;
        ++m_attemptSerial;
        if (m_inner && !m_inner->isFinished()) {
            m_inner->abort(); // finished 回调里统一收尾
            return;
        }
        if (!m_delivered && !m_inner) {
            setError(QNetworkReply::OperationCanceledError, QStringLiteral("Operation canceled"));
            finalize();
        }
    }

    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 remaining = m_responseBody.size() - m_readOffset;
        if (remaining <= 0)
            return 0;
        const qint64 count = qMin(maxSize, remaining);
        std::memcpy(data, m_responseBody.constData() + m_readOffset, static_cast<size_t>(count));
        m_readOffset += count;
        return count;
    }

    qint64 bytesAvailable() const override
    {
        return (m_responseBody.size() - m_readOffset) + QIODevice::bytesAvailable();
    }

    bool isSequential() const override { return true; }

private:
    void startAttempt()
    {
        if (m_aborted || m_delivered)
            return;
        QNetworkRequest req = m_request;
        req.setAttribute(kInternalRequestAttribute, true);

        QString nonce;
        if (m_replayProtected) {
            nonce = ReplayNonceStore::instance().take(isWriteOperation(m_op));
            if (!nonce.isEmpty())
                req.setRawHeader(ReplayNonceStore::nonceHeader(), nonce.toUtf8());
        }

        const quint64 attempt = ++m_attemptSerial;
        if (!m_identityProtected) {
            sendAttempt(req, nonce, {}, attempt);
            return;
        }
        QPointer<ProtectedReply> self(this);
        ClientIdentityStore::instance().ensure(
                m_nam, [self, req, nonce, attempt](
                        bool ok, const ClientIdentityCredentials &credentials,
                        const QString &error) {
            if (!self || self->m_aborted || self->m_attemptSerial != attempt)
                return;
            if (!ok) {
                self->failLater(error);
                return;
            }
            self->sendAttempt(req, nonce, credentials, attempt);
        });
    }

    void sendAttempt(QNetworkRequest req, const QString &nonce,
                     const ClientIdentityCredentials &credentials, quint64 attempt)
    {
        if (m_aborted || m_delivered || m_attemptSerial != attempt)
            return;

        if (m_identityProtected) {
            const qint64 timestamp = QDateTime::currentSecsSinceEpoch();
            const QString canonical = ClientIdentityProtocol::canonicalRequest(
                    operationName(m_op), signingPath(req.url()),
                    req.url().query(QUrl::FullyEncoded),
                    QString::fromLatin1(ClientIdentityStore::clientId()),
                    credentials.instanceId,
                    QString::fromLatin1(ClientIdentityStore::platform()),
                    timestamp, QString::fromLatin1(m_bodyHash), nonce);
            QByteArray signature;
            QString error;
            if (!ClientIdentityStore::instance().signCanonical(canonical, &signature, &error)) {
                failLater(error);
                return;
            }
            req.setRawHeader("X-Neko-Client-Id", ClientIdentityStore::clientId());
            req.setRawHeader("X-Neko-Client-Instance", credentials.instanceId.toUtf8());
            req.setRawHeader("X-Neko-Client-Token", credentials.token.toUtf8());
            req.setRawHeader("X-Neko-Client-Timestamp", QByteArray::number(timestamp));
            req.setRawHeader("X-Neko-Client-Signature", signature);
            m_identityGeneration = credentials.generation;
        }

        if (m_innerBody) {
            m_innerBody->deleteLater();
            m_innerBody = nullptr;
        }
        QIODevice *body = m_streamingBody;
        if (!body && !m_requestBody.isEmpty()) {
            auto *buffer = new QBuffer(this);
            buffer->setData(m_requestBody);
            buffer->open(QIODevice::ReadOnly);
            m_innerBody = buffer;
            body = buffer;
        }
        m_inner = m_nam->issueInternal(m_op, req, body);
        if (!m_inner) {
            failLater(QStringLiteral("无法创建客户端请求"));
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
        const bool replayRejected = status == 409
                && (replayStatus == QByteArrayLiteral("missing")
                    || replayStatus == QByteArrayLiteral("invalid"));
        const QByteArray clientStatus = inner->rawHeader("X-Neko-Client-Status");

        if (!m_aborted && m_replayProtected && replayRejected
            && m_replayRetries < kMaxRetries) {
            ++m_replayRetries;
            qInfo() << "[replay] 防重放校验失败，换新 nonce 重试:"
                    << normalizedPath(m_request.url());
            releaseInner();
            ReplayNonceStore::instance().noteReplayRejected();
            startAttempt();
            return;
        }

        if (!m_aborted && m_identityProtected && status == 401
            && clientStatus == QByteArrayLiteral("invalid")
            && m_identityRetries < kMaxRetries && m_identityGeneration != 0
            && !m_streamingBody) {
            ++m_identityRetries;
            const quint64 failedGeneration = m_identityGeneration;
            releaseInner();
            qInfo() << "[client-identity] 实例身份失效，恢复后重试:"
                    << normalizedPath(m_request.url());
            QPointer<ProtectedReply> self(this);
            ClientIdentityStore::instance().recover(
                    failedGeneration, m_nam,
                    [self](bool ok, const ClientIdentityCredentials &, const QString &error) {
                if (!self || self->m_aborted)
                    return;
                if (!ok) {
                    self->failLater(QStringLiteral("客户端实例身份恢复失败：%1").arg(error));
                    return;
                }
                self->startAttempt();
            });
            return;
        }

        m_responseBody = inner->readAll();
        m_readOffset = 0;
        copyFrom(inner);
        releaseInner();
        finalize();
    }

    void releaseInner()
    {
        if (m_inner) {
            m_inner->deleteLater();
            m_inner = nullptr;
        }
        if (m_innerBody) {
            m_innerBody->deleteLater();
            m_innerBody = nullptr;
        }
    }

    void failLater(const QString &error)
    {
        if (m_aborted || m_delivered)
            return;
        QPointer<ProtectedReply> self(this);
        QTimer::singleShot(0, this, [self, error]() {
            if (!self || self->m_aborted || self->m_delivered)
                return;
            self->setError(QNetworkReply::AuthenticationRequiredError,
                           error.isEmpty() ? QStringLiteral("客户端身份初始化失败") : error);
            self->finalize();
        });
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

        // Qt 6.7 没有 NumKnownHeaders，使用保守上限
        for (int header = 0; header < 64; ++header) {
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

    static constexpr int kMaxRetries = 1;

    NekoNetworkAccessManager *m_nam = nullptr;
    QNetworkAccessManager::Operation m_op = QNetworkAccessManager::GetOperation;
    QNetworkRequest m_request;
    QNetworkReply *m_inner = nullptr;
    QByteArray m_requestBody;
    QByteArray m_bodyHash;
    QByteArray m_responseBody;
    QIODevice *m_streamingBody = nullptr;
    QBuffer *m_innerBody = nullptr;
    bool m_replayProtected = false;
    bool m_identityProtected = false;
    qint64 m_readOffset = 0;
    int m_replayRetries = 0;
    int m_identityRetries = 0;
    quint64 m_attemptSerial = 0;
    quint64 m_identityGeneration = 0;
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
    const bool backend = isBackendRequest(req.url());
    if (backend) {
        const QString version = QString::fromUtf8(APP_VERSION);
        // 强制统一 UA，不保留调用方自定义 UA，确保全链路客户端标识严格一致。
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("NekoMusic-PC/%1").arg(version));
        // X-Neko-Client 同样强制统一。
        req.setRawHeader("X-Neko-Client",
                         QStringLiteral("pc+%1").arg(version).toUtf8());
        // 已登录用户令牌：调用方不必逐个记着带（历史代码里有的写 `Bearer x`、有的直接塞裸 token），
        // 这里统一补成 `Bearer <token>`，访问日志才能把请求落到具体账号。
        // 调用方显式设过则不覆盖，避免踩到第三方 / 特殊令牌的调用点。
        const QString token = UserManager::instance().token();
        if (!token.isEmpty() && !req.hasRawHeader("Authorization")) {
            req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + token.toUtf8());
        }
    }

    const bool replayProtected = backend && isReplayProtected(req, op);
    const bool identityProtected = backend && isClientIdentityProtected(req, op);
    if (req.attribute(kInternalRequestAttribute).toBool()
        || !backend || (!replayProtected && !identityProtected)) {
        return QNetworkAccessManager::createRequest(op, req, outgoingData);
    }

    QByteArray body;
    QByteArray bodyHash;
    QIODevice *streamingBody = nullptr;
    const bool multipart = isMultipart(req);
    if (multipart) {
        bodyHash = QByteArrayLiteral("-");
        if (outgoingData)
            streamingBody = outgoingData;
    } else if (outgoingData) {
        if (outgoingData->isSequential()) {
            // 非 multipart 的流式请求无法预先计算摘要；保持原语义交给服务端拒绝，
            // 不擅自读取可能无限长的请求体。
            return QNetworkAccessManager::createRequest(op, req, outgoingData);
        }
        body = outgoingData->readAll();
        outgoingData->seek(0);
        bodyHash = QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex();
    } else {
        bodyHash = QCryptographicHash::hash(QByteArray(), QCryptographicHash::Sha256).toHex();
    }
    return new ProtectedReply(this, op, req, body, bodyHash, streamingBody,
                              replayProtected, identityProtected);
}
