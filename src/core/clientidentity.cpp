/**
 * @file clientidentity.cpp
 * @brief PC 客户端实例身份：注册、续签、请求签名与恢复实现
 */

#include "core/clientidentity.h"

#include "core/clientidentitykey.h"
#include "core/nekonetworkaccessmanager.h"
#include "theme/theme.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>

namespace {

constexpr qint64 kTokenRefreshAheadSeconds = 24 * 60 * 60;
constexpr qint64 kTokenFallbackSeconds = 5 * 60;
constexpr int kIdentityLoadTimeoutMs = 30 * 1000;

constexpr const char *kHeaderInstance = "X-Neko-Client-Instance";
constexpr const char *kHeaderToken = "X-Neko-Client-Token";
constexpr const char *kHeaderTimestamp = "X-Neko-Client-Timestamp";
constexpr const char *kHeaderSignature = "X-Neko-Client-Signature";
constexpr const char *kHeaderClientStatus = "X-Neko-Client-Status";

qint64 nowSeconds()
{
    return QDateTime::currentSecsSinceEpoch();
}

QString publicKeySetting(const QByteArray &publicKeySpki)
{
    return QString::fromLatin1(ClientIdentityProtocol::base64Url(publicKeySpki));
}

void saveIdentitySettings(const QString &instanceId, const QString &token, qint64 expiresAt,
                          const QByteArray &publicKeySpki)
{
    QSettings settings;
    settings.setValue(QStringLiteral("clientIdentity/instanceId"), instanceId);
    settings.setValue(QStringLiteral("clientIdentity/token"), token);
    settings.setValue(QStringLiteral("clientIdentity/expiresAt"), expiresAt);
    settings.setValue(QStringLiteral("clientIdentity/publicKey"),
                      publicKeySetting(publicKeySpki));
    settings.sync();
}

void clearIdentitySettings()
{
    QSettings settings;
    settings.remove(QStringLiteral("clientIdentity/instanceId"));
    settings.remove(QStringLiteral("clientIdentity/token"));
    settings.remove(QStringLiteral("clientIdentity/expiresAt"));
    settings.remove(QStringLiteral("clientIdentity/publicKey"));
    settings.sync();
}

QString responseError(QNetworkReply *reply, const QJsonObject &root, int status)
{
    QString message = root.value(QStringLiteral("message")).toString();
    if (message.isEmpty())
        message = reply->errorString();
    if (message.isEmpty())
        message = QStringLiteral("服务端返回 HTTP %1").arg(status);
    return message;
}

bool parseSuccessfulReply(QNetworkReply *reply, QJsonObject *data, int *status, QString *error)
{
    *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    if (reply->error() != QNetworkReply::NoError
        || *status < 200 || *status >= 300
        || !root.value(QStringLiteral("success")).toBool()) {
        *error = responseError(reply, root, *status);
        return false;
    }
    *data = root.value(QStringLiteral("data")).toObject();
    return true;
}

bool signWithKey(const std::shared_ptr<ClientIdentityKey> &key, const QString &canonical,
                 QByteArray *signature, QString *error)
{
    if (!key) {
        if (error)
            *error = QStringLiteral("客户端身份私钥不可用");
        return false;
    }
    const QByteArray der = key->signSha256(canonical.toUtf8(), error);
    if (der.isEmpty())
        return false;
    *signature = ClientIdentityProtocol::base64Url(der);
    return true;
}

} // namespace

namespace ClientIdentityProtocol {

QString canonicalRegister(const QString &clientId, const QString &platform,
                          const QString &instanceId, const QString &publicKeySpki,
                          const QString &challenge, qint64 timestampSeconds)
{
    return QStringList{
        QStringLiteral("neko-client-register-v1"), clientId, platform, instanceId,
        publicKeySpki, challenge, QString::number(timestampSeconds),
    }.join(QLatin1Char('\n'));
}

QString canonicalToken(const QString &clientId, const QString &platform,
                       const QString &instanceId, const QString &challenge,
                       qint64 timestampSeconds)
{
    return QStringList{
        QStringLiteral("neko-client-token-v1"), clientId, platform, instanceId,
        challenge, QString::number(timestampSeconds),
    }.join(QLatin1Char('\n'));
}

QString canonicalRequest(const QString &method, const QString &path, const QString &query,
                         const QString &clientId, const QString &instanceId,
                         const QString &platform, qint64 timestampSeconds,
                         const QString &bodyHash, const QString &nonce)
{
    return QStringList{
        QString::fromLatin1(kProtocol), method.toUpper(), path, query, clientId, instanceId,
        platform, QString::number(timestampSeconds), bodyHash, nonce,
    }.join(QLatin1Char('\n'));
}

QByteArray sha256Hex(const QByteArray &body)
{
    return QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex();
}

QByteArray base64Url(const QByteArray &value)
{
    return value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

} // namespace ClientIdentityProtocol

struct ClientIdentityStore::Impl
{
    struct Waiter {
        QPointer<NekoNetworkAccessManager> nam;
        Callback callback;
    };

    mutable QMutex stateMutex;
    mutable QMutex keyMutex;
    std::shared_ptr<ClientIdentityKey> key;

    bool hasCached = false;
    ClientIdentityCredentials cached;
    quint64 generation = 0;

    bool loading = false;
    bool loadingRecovery = false;
    bool restartAfterLoad = false;
    quint64 loadSerial = 0;
    QPointer<NekoNetworkAccessManager> loader;
    QList<Waiter> waiters;

    std::shared_ptr<ClientIdentityKey> ensureKey(QString *error)
    {
        QMutexLocker locker(&keyMutex);
        if (key)
            return key;
        key = ClientIdentityKey::loadOrCreate(error);
        return key;
    }

    void resetKey()
    {
        QMutexLocker locker(&keyMutex);
        key.reset();
    }

    bool isCurrent(quint64 serial) const
    {
        QMutexLocker locker(&stateMutex);
        return loading && loadSerial == serial;
    }

    void dispatch(const QList<Waiter> &waiters, bool ok,
                  const ClientIdentityCredentials &credentials, const QString &error)
    {
        for (const auto &waiter : waiters) {
            if (!waiter.nam)
                continue;
            const Callback callback = waiter.callback;
            if (QThread::currentThread() == waiter.nam->thread()) {
                callback(ok, credentials, error);
            } else {
                NekoNetworkAccessManager *nam = waiter.nam.data();
                QMetaObject::invokeMethod(nam, [callback, ok, credentials, error]() {
                    callback(ok, credentials, error);
                }, Qt::QueuedConnection);
            }
        }
    }

    void finish(ClientIdentityStore &owner, quint64 serial, bool ok,
                const ClientIdentityCredentials &credentials, const QString &error)
    {
        bool restart = false;
        quint64 nextSerial = 0;
        QPointer<NekoNetworkAccessManager> nextNam;
        QList<Waiter> pending;
        ClientIdentityCredentials delivered = credentials;
        {
            QMutexLocker locker(&stateMutex);
            if (!loading || loadSerial != serial)
                return;
            if (restartAfterLoad && loader) {
                restartAfterLoad = false;
                loadingRecovery = true;
                nextSerial = ++loadSerial;
                nextNam = loader;
                restart = true;
            } else {
                restartAfterLoad = false;
                loading = false;
                loadingRecovery = false;
                loader.clear();
                if (ok) {
                    cached = credentials;
                    cached.generation = ++generation;
                    hasCached = true;
                    delivered = cached;
                } else if (credentials.generation == 0 && hasCached
                           && cached.expiresAt > nowSeconds() + kTokenFallbackSeconds) {
                    // 网络故障时继续使用尚未过期的旧令牌；明确的 401 由 recover 路径处理。
                    delivered = cached;
                    ok = true;
                }
                pending = waiters;
                waiters.clear();
            }
        }
        if (restart) {
            beginLoad(owner, nextSerial, nextNam, true);
            return;
        }
        dispatch(pending, ok, delivered, error);
    }

    void requestChallenge(ClientIdentityStore &owner, quint64 serial,
                          QPointer<NekoNetworkAccessManager> nam, const QString &purpose,
                          std::function<void(bool, const QString &, int, const QString &)> callback);
    void requestRefresh(ClientIdentityStore &owner, quint64 serial,
                        QPointer<NekoNetworkAccessManager> nam,
                        const std::shared_ptr<ClientIdentityKey> &key,
                        const QString &instanceId, const QString &oldToken,
                        qint64 oldExpiresAt, bool forceRecovery);
    void requestRegister(ClientIdentityStore &owner, quint64 serial,
                         QPointer<NekoNetworkAccessManager> nam,
                         const std::shared_ptr<ClientIdentityKey> &key, bool allowRotate);
    void rotateAndRegister(ClientIdentityStore &owner, quint64 serial,
                           QPointer<NekoNetworkAccessManager> nam);
    void beginLoad(ClientIdentityStore &owner, quint64 serial,
                   QPointer<NekoNetworkAccessManager> nam, bool forceRecovery);
};

ClientIdentityStore &ClientIdentityStore::instance()
{
    static ClientIdentityStore store;
    return store;
}

ClientIdentityStore::ClientIdentityStore()
    : d(std::make_unique<Impl>())
{
}

ClientIdentityStore::~ClientIdentityStore() = default;

void ClientIdentityStore::ensure(NekoNetworkAccessManager *nam, Callback callback)
{
    if (!nam) {
        callback(false, {}, QStringLiteral("客户端身份网络管理器为空"));
        return;
    }

    bool start = false;
    quint64 serial = 0;
    {
        QMutexLocker locker(&d->stateMutex);
        // 发起加载的 NAM 可能在请求途中被销毁；下一次请求到达时丢弃那份失效状态，
        // 让仍存活的等待者直接加入新一轮加载，避免永久卡在 loading=true。
        if (d->loading && !d->loader) {
            d->loading = false;
            d->loadingRecovery = false;
            d->restartAfterLoad = false;
            ++d->loadSerial;
        }
        if (d->hasCached && d->cached.expiresAt
                > nowSeconds() + kTokenRefreshAheadSeconds) {
            const ClientIdentityCredentials credentials = d->cached;
            locker.unlock();
            callback(true, credentials, {});
            return;
        }
        d->waiters.append(Impl::Waiter{QPointer<NekoNetworkAccessManager>(nam),
                                       std::move(callback)});
        if (!d->loading) {
            d->loading = true;
            d->loadingRecovery = false;
            d->restartAfterLoad = false;
            d->loader = nam;
            serial = ++d->loadSerial;
            start = true;
        }
    }
    if (start)
        d->beginLoad(*this, serial, QPointer<NekoNetworkAccessManager>(nam), false);
}

void ClientIdentityStore::recover(quint64 failedGeneration, NekoNetworkAccessManager *nam,
                                  Callback callback)
{
    if (!nam) {
        callback(false, {}, QStringLiteral("客户端身份网络管理器为空"));
        return;
    }

    bool start = false;
    quint64 serial = 0;
    {
        QMutexLocker locker(&d->stateMutex);
        if (d->loading && !d->loader) {
            d->loading = false;
            d->loadingRecovery = false;
            d->restartAfterLoad = false;
            ++d->loadSerial;
        }
        if (d->hasCached && d->cached.generation != failedGeneration) {
            const ClientIdentityCredentials credentials = d->cached;
            locker.unlock();
            callback(true, credentials, {});
            return;
        }
        if (d->hasCached && d->cached.generation == failedGeneration)
            d->hasCached = false;
        d->waiters.append(Impl::Waiter{QPointer<NekoNetworkAccessManager>(nam),
                                       std::move(callback)});
        if (!d->loading) {
            d->loading = true;
            d->loadingRecovery = true;
            d->restartAfterLoad = false;
            d->loader = nam;
            serial = ++d->loadSerial;
            start = true;
        } else if (!d->loadingRecovery) {
            d->restartAfterLoad = true;
        }
    }
    if (start)
        d->beginLoad(*this, serial, QPointer<NekoNetworkAccessManager>(nam), true);
}

bool ClientIdentityStore::signCanonical(const QString &canonical, QByteArray *signature,
                                        QString *error) const
{
    QString keyError;
    const auto key = d->ensureKey(&keyError);
    if (!key) {
        if (error)
            *error = keyError;
        return false;
    }
    return signWithKey(key, canonical, signature, error);
}

void ClientIdentityStore::Impl::beginLoad(ClientIdentityStore &owner, quint64 serial,
                                          QPointer<NekoNetworkAccessManager> nam,
                                          bool forceRecovery)
{
    if (!nam || !isCurrent(serial)) {
        finish(owner, serial, false, {}, QStringLiteral("客户端身份网络管理器已销毁"));
        return;
    }
    if (QCoreApplication *app = QCoreApplication::instance()) {
        QTimer::singleShot(kIdentityLoadTimeoutMs, app, [this, &owner, serial]() {
            if (!isCurrent(serial))
                return;
            finish(owner, serial, false, {},
                   QStringLiteral("客户端身份初始化超时，请检查网络连接"));
        });
    }
    QString keyError;
    const auto currentKey = ensureKey(&keyError);
    if (!currentKey) {
        finish(owner, serial, false, {}, keyError);
        return;
    }

    QSettings settings;
    QString instanceId = settings.value(QStringLiteral("clientIdentity/instanceId")).toString();
    const QString token = settings.value(QStringLiteral("clientIdentity/token")).toString();
    const qint64 expiresAt = settings.value(QStringLiteral("clientIdentity/expiresAt"), 0).toLongLong();
    const QString storedPublicKey = settings.value(QStringLiteral("clientIdentity/publicKey")).toString();
    const QString currentPublicKey = publicKeySetting(currentKey->publicKeySpkiDer());
    const bool sameKey = storedPublicKey.isEmpty() || storedPublicKey == currentPublicKey;

    if (!sameKey) {
        instanceId.clear();
        clearIdentitySettings();
    }

    if (!forceRecovery && !instanceId.isEmpty()
        && expiresAt > nowSeconds() + kTokenRefreshAheadSeconds) {
        finish(owner, serial, true,
               ClientIdentityCredentials{instanceId, token, expiresAt, 0}, {});
        return;
    }

    if (!instanceId.isEmpty()) {
        requestRefresh(owner, serial, nam, currentKey, instanceId, token, expiresAt,
                       forceRecovery);
        return;
    }
    requestRegister(owner, serial, nam, currentKey, true);
}

void ClientIdentityStore::Impl::requestChallenge(
        ClientIdentityStore &owner, quint64 serial, QPointer<NekoNetworkAccessManager> nam,
        const QString &purpose,
        std::function<void(bool, const QString &, int, const QString &)> callback)
{
    if (!nam || !isCurrent(serial)) {
        callback(false, {}, 0, QStringLiteral("客户端身份请求已取消"));
        return;
    }
    QUrl url(QString::fromUtf8(Theme::kApiBase) + QStringLiteral("/api/client/challenge"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(ClientIdentityProtocol::kClientId));
    query.addQueryItem(QStringLiteral("platform"), QString::fromLatin1(ClientIdentityProtocol::kPlatform));
    query.addQueryItem(QStringLiteral("purpose"), purpose);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setRawHeader("Cache-Control", "no-store");
    QNetworkReply *reply = nam->get(request);
    QObject::connect(reply, &QNetworkReply::finished, nam.data(),
                     [reply, callback, &owner, serial]() {
        int status = 0;
        QJsonObject data;
        QString error;
        const bool ok = parseSuccessfulReply(reply, &data, &status, &error);
        reply->deleteLater();
        if (!ok) {
            callback(false, {}, status, error);
            return;
        }
        const QString protocol = data.value(QStringLiteral("protocol")).toString();
        const QString challenge = data.value(QStringLiteral("challenge")).toString();
        if (protocol != QString::fromLatin1(ClientIdentityProtocol::kProtocol)
            || challenge.isEmpty()) {
            callback(false, {}, status, QStringLiteral("客户端挑战响应格式不正确"));
            return;
        }
        Q_UNUSED(owner);
        callback(true, challenge, status, {});
    });
}

void ClientIdentityStore::Impl::requestRefresh(
        ClientIdentityStore &owner, quint64 serial, QPointer<NekoNetworkAccessManager> nam,
        const std::shared_ptr<ClientIdentityKey> &currentKey, const QString &instanceId,
        const QString &oldToken, qint64 oldExpiresAt, bool forceRecovery)
{
    requestChallenge(owner, serial, nam, QStringLiteral("token"),
                     [this, &owner, serial, nam, currentKey, instanceId, oldToken,
                      oldExpiresAt, forceRecovery](bool ok, const QString &challenge,
                                                   int status, const QString &error) {
        if (!isCurrent(serial))
            return;
        if (!ok) {
            if (!forceRecovery && !oldToken.isEmpty()
                && oldExpiresAt > nowSeconds() + kTokenFallbackSeconds) {
                finish(owner, serial, true,
                       ClientIdentityCredentials{instanceId, oldToken, oldExpiresAt, 0}, {});
            } else {
                finish(owner, serial, false, {}, error);
            }
            return;
        }

        const qint64 timestamp = nowSeconds();
        QByteArray signature;
        QString signError;
        const QString canonical = ClientIdentityProtocol::canonicalToken(
                QString::fromLatin1(ClientIdentityProtocol::kClientId),
                QString::fromLatin1(ClientIdentityProtocol::kPlatform),
                instanceId, challenge, timestamp);
        if (!signWithKey(currentKey, canonical, &signature, &signError)) {
            finish(owner, serial, false, {}, signError);
            return;
        }
        QJsonObject body;
        body.insert(QStringLiteral("client_id"),
                    QString::fromLatin1(ClientIdentityProtocol::kClientId));
        body.insert(QStringLiteral("platform"),
                    QString::fromLatin1(ClientIdentityProtocol::kPlatform));
        body.insert(QStringLiteral("instance_id"), instanceId);
        body.insert(QStringLiteral("challenge"), challenge);
        body.insert(QStringLiteral("timestamp"), timestamp);
        body.insert(QStringLiteral("signature"), QString::fromLatin1(signature));

        QNetworkRequest request(QUrl(QString::fromUtf8(Theme::kApiBase)
                                     + QStringLiteral("/api/client/token")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                             QNetworkRequest::AlwaysNetwork);
        request.setRawHeader("Cache-Control", "no-store");
        QNetworkReply *reply = nam->post(request,
                                         QJsonDocument(body).toJson(QJsonDocument::Compact));
        QObject::connect(reply, &QNetworkReply::finished, nam.data(),
                         [this, &owner, serial, nam, currentKey, instanceId, reply]() {
            if (!isCurrent(serial)) {
                reply->deleteLater();
                return;
            }
            int status = 0;
            QJsonObject data;
            QString error;
            const bool ok = parseSuccessfulReply(reply, &data, &status, &error);
            reply->deleteLater();
            if (!ok) {
                if (status == 401) {
                    requestRegister(owner, serial, nam, currentKey, true);
                } else {
                    finish(owner, serial, false, {}, error);
                }
                return;
            }
            const QString token = data.value(QStringLiteral("token")).toString();
            const qint64 expiresAt = data.value(QStringLiteral("expires_at")).toVariant().toLongLong();
            if (token.isEmpty() || expiresAt <= 0) {
                finish(owner, serial, false, {},
                       QStringLiteral("客户端续签响应格式不正确"));
                return;
            }
            saveIdentitySettings(instanceId, token, expiresAt, currentKey->publicKeySpkiDer());
            finish(owner, serial, true,
                   ClientIdentityCredentials{instanceId, token, expiresAt, 0}, {});
        });
    });
}

void ClientIdentityStore::Impl::requestRegister(
        ClientIdentityStore &owner, quint64 serial, QPointer<NekoNetworkAccessManager> nam,
        const std::shared_ptr<ClientIdentityKey> &currentKey, bool allowRotate)
{
    requestChallenge(owner, serial, nam, QStringLiteral("register"),
                     [this, &owner, serial, nam, currentKey, allowRotate](
                             bool ok, const QString &challenge, int status,
                             const QString &error) {
        if (!isCurrent(serial))
            return;
        if (!ok) {
            finish(owner, serial, false, {}, error);
            return;
        }

        const QString instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString publicKey = QString::fromLatin1(
                ClientIdentityProtocol::base64Url(currentKey->publicKeySpkiDer()));
        const qint64 timestamp = nowSeconds();
        const QString canonical = ClientIdentityProtocol::canonicalRegister(
                QString::fromLatin1(ClientIdentityProtocol::kClientId),
                QString::fromLatin1(ClientIdentityProtocol::kPlatform),
                instanceId, publicKey, challenge, timestamp);
        QByteArray signature;
        QString signError;
        if (!signWithKey(currentKey, canonical, &signature, &signError)) {
            finish(owner, serial, false, {}, signError);
            return;
        }

        QJsonObject body;
        body.insert(QStringLiteral("client_id"),
                    QString::fromLatin1(ClientIdentityProtocol::kClientId));
        body.insert(QStringLiteral("platform"),
                    QString::fromLatin1(ClientIdentityProtocol::kPlatform));
        body.insert(QStringLiteral("instance_id"), instanceId);
        body.insert(QStringLiteral("public_key"), publicKey);
        body.insert(QStringLiteral("challenge"), challenge);
        body.insert(QStringLiteral("timestamp"), timestamp);
        body.insert(QStringLiteral("signature"), QString::fromLatin1(signature));

        QNetworkRequest request(QUrl(QString::fromUtf8(Theme::kApiBase)
                                     + QStringLiteral("/api/client/register")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                             QNetworkRequest::AlwaysNetwork);
        request.setRawHeader("Cache-Control", "no-store");
        QNetworkReply *reply = nam->post(request,
                                         QJsonDocument(body).toJson(QJsonDocument::Compact));
        QObject::connect(reply, &QNetworkReply::finished, nam.data(),
                         [this, &owner, serial, nam, currentKey, instanceId, allowRotate, reply]() {
            if (!isCurrent(serial)) {
                reply->deleteLater();
                return;
            }
            int status = 0;
            QJsonObject data;
            QString error;
            const bool ok = parseSuccessfulReply(reply, &data, &status, &error);
            reply->deleteLater();
            if (!ok) {
                if (status == 401 && allowRotate)
                    rotateAndRegister(owner, serial, nam);
                else
                    finish(owner, serial, false, {}, error);
                return;
            }
            const QString storedInstance = data.value(QStringLiteral("instance_id")).toString();
            const QString token = data.value(QStringLiteral("token")).toString();
            const qint64 expiresAt = data.value(QStringLiteral("expires_at")).toVariant().toLongLong();
            if (storedInstance != instanceId || token.isEmpty() || expiresAt <= 0) {
                finish(owner, serial, false, {},
                       QStringLiteral("客户端注册响应格式不正确"));
                return;
            }
            saveIdentitySettings(instanceId, token, expiresAt, currentKey->publicKeySpkiDer());
            finish(owner, serial, true,
                   ClientIdentityCredentials{instanceId, token, expiresAt, 0}, {});
        });
    });
}

void ClientIdentityStore::Impl::rotateAndRegister(
        ClientIdentityStore &owner, quint64 serial, QPointer<NekoNetworkAccessManager> nam)
{
    QString error;
    resetKey();
    if (!ClientIdentityKey::reset(&error)) {
        finish(owner, serial, false, {}, error);
        return;
    }
    clearIdentitySettings();
    const auto rotated = ensureKey(&error);
    if (!rotated) {
        finish(owner, serial, false, {}, error);
        return;
    }
    requestRegister(owner, serial, nam, rotated, false);
}
