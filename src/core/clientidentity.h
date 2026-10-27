#pragma once

/**
 * @file clientidentity.h
 * @brief PC 客户端实例身份：注册、续签、请求签名与恢复
 */

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>

class NekoNetworkAccessManager;

namespace ClientIdentityProtocol {

constexpr const char *kProtocol = "neko-client-v1";
constexpr const char *kClientId = "nkc_pub_pc_prod";
constexpr const char *kPlatform = "pc";

QString canonicalRegister(const QString &clientId, const QString &platform,
                          const QString &instanceId, const QString &publicKeySpki,
                          const QString &challenge, qint64 timestampSeconds);
QString canonicalToken(const QString &clientId, const QString &platform,
                       const QString &instanceId, const QString &challenge,
                       qint64 timestampSeconds);
QString canonicalRequest(const QString &method, const QString &path, const QString &query,
                         const QString &clientId, const QString &instanceId,
                         const QString &platform, qint64 timestampSeconds,
                         const QString &bodyHash, const QString &nonce);
QByteArray sha256Hex(const QByteArray &body);
QByteArray base64Url(const QByteArray &value);

} // namespace ClientIdentityProtocol

struct ClientIdentityCredentials
{
    QString instanceId;
    QString token;
    qint64 expiresAt = 0;
    quint64 generation = 0;
};

/**
 * @brief 全局共享的 PC 安装实例身份。
 *
 * 请求可能来自 GUI、媒体 I/O、下载等工作线程，因此本类只共享线程安全的状态，并把网络
 * 回调投递回各自发起请求的 [NekoNetworkAccessManager] 所属线程。同一时刻只执行一轮
 * 挑战 / 注册 / 续签，避免并发请求各自登记出多个实例。
 */
class ClientIdentityStore
{
public:
    using Callback = std::function<void(bool ok, const ClientIdentityCredentials &credentials,
                                        const QString &error)>;

    static ClientIdentityStore &instance();

    /** 获取可用实例身份；缓存有效时同步回调，否则合并到同一轮异步加载。 */
    void ensure(NekoNetworkAccessManager *nam, Callback callback);
    /** 服务端明确判定实例身份失效后恢复；generation 用于合并同批 401。 */
    void recover(quint64 failedGeneration, NekoNetworkAccessManager *nam, Callback callback);

    /** 用当前实例私钥签名 canonical，返回无填充 base64url 的 DER 签名。 */
    bool signCanonical(const QString &canonical, QByteArray *signature, QString *error) const;

    static const char *clientId() { return ClientIdentityProtocol::kClientId; }
    static const char *platform() { return ClientIdentityProtocol::kPlatform; }

private:
    ClientIdentityStore();
    ~ClientIdentityStore();

    struct Impl;
    std::unique_ptr<Impl> d;
};
