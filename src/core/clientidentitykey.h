#pragma once

/**
 * @file clientidentitykey.h
 * @brief PC 客户端实例私钥的生成、持久化与签名
 */

#include <QByteArray>
#include <QString>

#include <memory>

/**
 * @brief 当前安装实例的 EC P-256 私钥。
 *
 * 私钥不随安装包分发，也不会进入请求体。Linux 使用权限为 0600 的 PEM 文件，
 * Windows 使用 CNG 持久化密钥，macOS 使用 Keychain。公钥统一导出为 SPKI DER。
 */
class ClientIdentityKey
{
public:
    ~ClientIdentityKey();

    ClientIdentityKey(const ClientIdentityKey &) = delete;
    ClientIdentityKey &operator=(const ClientIdentityKey &) = delete;

    /** 读取已有密钥；不存在时生成并持久化。失败返回空指针并写入 error。 */
    static std::shared_ptr<ClientIdentityKey> loadOrCreate(QString *error);
    /** 删除当前实例密钥；用于服务端明确拒绝现有公钥后的轮换。 */
    static bool reset(QString *error);

    /** 公钥 SPKI DER。 */
    QByteArray publicKeySpkiDer() const;
    /** SHA256withECDSA 签名，返回 DER。 */
    QByteArray signSha256(const QByteArray &data, QString *error) const;

private:
    struct Impl;
    explicit ClientIdentityKey(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> d;
};
