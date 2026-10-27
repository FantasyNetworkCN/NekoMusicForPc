/**
 * @file clientidentitykey.cpp
 * @brief PC 客户端实例私钥的生成、持久化与签名实现
 */

#include "core/clientidentitykey.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cstring>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <bcrypt.h>
#include <ncrypt.h>
#elif defined(Q_OS_MACOS)
#include <Security/Security.h>
#else
#include <openssl/bio.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#endif

namespace {

constexpr char kClientIdentityKeyTag[] = "com.nekomusic.pc.client-identity.v1";

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
/** 将 X9.63（04 || X || Y）公钥包装成固定 P-256 SPKI DER。 */
QByteArray rawPointToSpki(const QByteArray &raw)
{
    static const unsigned char prefix[] = {
        0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,
        0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00,
    };
    if (raw.size() != 65 || static_cast<unsigned char>(raw.at(0)) != 0x04)
        return {};
    QByteArray out(reinterpret_cast<const char *>(prefix), sizeof(prefix));
    out.append(raw);
    return out;
}

QByteArray derLength(int length)
{
    if (length < 0x80)
        return QByteArray(1, static_cast<char>(length));
    QByteArray encoded;
    while (length > 0) {
        encoded.prepend(static_cast<char>(length & 0xff));
        length >>= 8;
    }
    encoded.prepend(static_cast<char>(0x80 | encoded.size()));
    return encoded;
}

QByteArray derInteger(const QByteArray &value)
{
    int offset = 0;
    while (offset < value.size() - 1 && value.at(offset) == 0)
        ++offset;
    QByteArray trimmed = value.mid(offset);
    if (trimmed.isEmpty())
        trimmed = QByteArray(1, '\0');
    if (static_cast<unsigned char>(trimmed.at(0)) & 0x80)
        trimmed.prepend('\0');
    QByteArray out;
    out.append(static_cast<char>(0x02));
    out.append(derLength(trimmed.size()));
    out.append(trimmed);
    return out;
}

/** Windows CNG 使用 r || s；若后端已返回 DER 则原样使用。 */
QByteArray rawSignatureToDer(const QByteArray &raw)
{
    if (raw.size() == 64) {
        const QByteArray r = derInteger(raw.left(32));
        const QByteArray s = derInteger(raw.mid(32, 32));
        QByteArray body = r + s;
        QByteArray out;
        out.append(static_cast<char>(0x30));
        out.append(derLength(body.size()));
        out.append(body);
        return out;
    }
    return raw;
}
#endif

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
QString keyFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath(QStringLiteral("client-identity-p256.pem"));
}

QString opensslError()
{
    const unsigned long code = ERR_get_error();
    if (code == 0)
        return QStringLiteral("OpenSSL 未提供错误详情");
    char buffer[256] = {};
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return QString::fromLatin1(buffer);
}

bool isP256(const EVP_PKEY *key)
{
    if (!key || EVP_PKEY_base_id(key) != EVP_PKEY_EC || EVP_PKEY_bits(key) != 256)
        return false;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    char name[80] = {};
    size_t length = 0;
    if (EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME,
                                       name, sizeof(name), &length) != 1)
        return false;
    return std::strcmp(name, "prime256v1") == 0 || std::strcmp(name, "P-256") == 0;
#else
    const EC_KEY *ec = EVP_PKEY_get0_EC_KEY(key);
    return ec && EC_GROUP_get_curve_name(EC_KEY_get0_group(ec))
            == NID_X9_62_prime256v1;
#endif
}

EVP_PKEY *generateP256()
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr);
    if (!ctx)
        return nullptr;
    EVP_PKEY *key = nullptr;
    if (EVP_PKEY_keygen_init(ctx) != 1
        || EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) != 1
        || EVP_PKEY_keygen(ctx, &key) != 1) {
        key = nullptr;
    }
    EVP_PKEY_CTX_free(ctx);
    return key;
}

EVP_PKEY *readP256(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.exists())
        return nullptr;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取客户端身份密钥：%1").arg(file.errorString());
        return nullptr;
    }
    const QFileDevice::Permissions securePermissions =
            QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (file.permissions() != securePermissions
        && !QFile::setPermissions(path, securePermissions)) {
        if (error)
            *error = QStringLiteral("无法收紧客户端身份密钥文件权限");
        return nullptr;
    }
    const QByteArray pem = file.readAll();
    BIO *bio = BIO_new_mem_buf(pem.constData(), pem.size());
    if (!bio) {
        if (error)
            *error = opensslError();
        return nullptr;
    }
    EVP_PKEY *key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!isP256(key)) {
        if (key)
            EVP_PKEY_free(key);
        if (error)
            *error = QStringLiteral("客户端身份密钥不是 EC P-256");
        return nullptr;
    }
    return key;
}

bool writeP256(const QString &path, EVP_PKEY *key, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio || PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        if (bio)
            BIO_free(bio);
        if (error)
            *error = opensslError();
        return false;
    }
    char *data = nullptr;
    const long size = BIO_get_mem_data(bio, &data);
    QSaveFile file(path);
    if (size <= 0 || !file.open(QIODevice::WriteOnly)
        || file.write(data, size) != size || !file.commit()) {
        BIO_free(bio);
        if (error)
            *error = QStringLiteral("无法保存客户端身份密钥：%1").arg(file.errorString());
        return false;
    }
    BIO_free(bio);
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        if (error)
            *error = QStringLiteral("无法收紧客户端身份密钥文件权限");
        return false;
    }
    return true;
}
#endif

#if defined(Q_OS_WIN)
QString winError(const char *operation, NTSTATUS status)
{
    return QStringLiteral("%1 失败（0x%2）")
            .arg(QString::fromLatin1(operation))
            .arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0'));
}

QByteArray exportCngPublicKey(NCRYPT_KEY_HANDLE key, QString *error)
{
    DWORD size = 0;
    SECURITY_STATUS status = NCryptExportKey(key, 0, BCRYPT_ECCPUBLIC_BLOB,
                                             nullptr, nullptr, 0, &size, 0);
    if (status != ERROR_SUCCESS) {
        if (error)
            *error = winError("导出 CNG 公钥", status);
        return {};
    }
    QByteArray blob(static_cast<int>(size), '\0');
    status = NCryptExportKey(key, 0, BCRYPT_ECCPUBLIC_BLOB, nullptr,
                             reinterpret_cast<PBYTE>(blob.data()), size, &size, 0);
    if (status != ERROR_SUCCESS) {
        if (error)
            *error = winError("导出 CNG 公钥", status);
        return {};
    }
    if (blob.size() < static_cast<int>(sizeof(BCRYPT_ECCKEY_BLOB))) {
        if (error)
            *error = QStringLiteral("CNG 公钥长度无效");
        return {};
    }
    const auto *header = reinterpret_cast<const BCRYPT_ECCKEY_BLOB *>(blob.constData());
    const int keyBytes = static_cast<int>(header->cbKey);
    if (keyBytes <= 0 || keyBytes > 32
        || blob.size() < static_cast<int>(sizeof(BCRYPT_ECCKEY_BLOB)) + keyBytes * 2) {
        if (error)
            *error = QStringLiteral("CNG P-256 公钥格式无效");
        return {};
    }
    const char *xy = blob.constData() + sizeof(BCRYPT_ECCKEY_BLOB);
    QByteArray point(65, '\0');
    point[0] = 0x04;
    std::memcpy(point.data() + 1 + (32 - keyBytes), xy, static_cast<size_t>(keyBytes));
    std::memcpy(point.data() + 33 + (32 - keyBytes), xy + keyBytes,
                static_cast<size_t>(keyBytes));
    return rawPointToSpki(point);
}

void closeCngKey(NCRYPT_KEY_HANDLE &key)
{
    if (key) {
        NCryptFreeObject(key);
        key = 0;
    }
}

NCRYPT_KEY_HANDLE openOrCreateCngKey(QString *error)
{
    const wchar_t *name = L"NekoMusic.PC.ClientIdentity.v1";
    const wchar_t *providers[] = {MS_PLATFORM_CRYPTO_PROVIDER, MS_KEY_STORAGE_PROVIDER};
    SECURITY_STATUS lastStatus = NTE_BAD_KEYSET;
    for (const wchar_t *providerName : providers) {
        NCRYPT_PROV_HANDLE provider = 0;
        if (NCryptOpenStorageProvider(&provider, providerName, 0) != ERROR_SUCCESS)
            continue;

        NCRYPT_KEY_HANDLE key = 0;
        SECURITY_STATUS status = NCryptOpenKey(provider, &key, name, 0, 0);
        if (status == ERROR_SUCCESS) {
            NCryptFreeObject(provider);
            return key;
        }
        status = NCryptCreatePersistedKey(provider, &key, NCRYPT_ECDSA_P256_ALGORITHM,
                                          name, 0, 0);
        if (status == ERROR_SUCCESS)
            status = NCryptFinalizeKey(key, 0);
        NCryptFreeObject(provider);
        if (status == ERROR_SUCCESS)
            return key;
        closeCngKey(key);
        lastStatus = status;
    }
    if (error)
        *error = winError("创建 Windows CNG 身份密钥", lastStatus);
    return 0;
}
#endif

#if defined(Q_OS_MACOS)
CFDataRef createKeyTag()
{
    return CFDataCreate(nullptr,
                        reinterpret_cast<const UInt8 *>(kClientIdentityKeyTag),
                        static_cast<CFIndex>(std::strlen(kClientIdentityKeyTag)));
}

QByteArray cfDataToByteArray(CFDataRef data)
{
    if (!data)
        return {};
    return QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
                      static_cast<int>(CFDataGetLength(data)));
}

QString macError(CFErrorRef error)
{
    if (!error)
        return QStringLiteral("未知 Keychain 错误");
    CFStringRef description = CFErrorCopyDescription(error);
    QString text = description
            ? QString::fromCFString(description)
            : QStringLiteral("未知 Keychain 错误");
    if (description)
        CFRelease(description);
    return text;
}

SecKeyRef findKey(CFDataRef tag, QString *error)
{
    Q_UNUSED(error);
    const void *keys[] = {
        kSecClass, kSecAttrKeyType, kSecAttrApplicationTag, kSecReturnRef, kSecMatchLimit,
    };
    const void *values[] = {
        kSecClassKey, kSecAttrKeyTypeECSECPrimeRandom, tag, kCFBooleanTrue, kSecMatchLimitOne,
    };
    CFDictionaryRef query = CFDictionaryCreate(
            nullptr, keys, values, 5, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
    CFTypeRef result = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);
    if (status != errSecSuccess || !result)
        return nullptr;
    return static_cast<SecKeyRef>(result);
}

SecKeyRef createKey(CFDataRef tag, QString *error)
{
    int keySizeValue = 256;
    CFNumberRef keySize = CFNumberCreate(nullptr, kCFNumberIntType, &keySizeValue);
    const void *privateKeys[] = {
        kSecAttrIsPermanent, kSecAttrApplicationTag, kSecAttrAccessible,
    };
    const void *privateValues[] = {
        kCFBooleanTrue, tag, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
    };
    CFDictionaryRef privateAttrs = CFDictionaryCreate(
            nullptr, privateKeys, privateValues, 3, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
    const void *keys[] = {
        kSecAttrKeyType, kSecAttrKeySizeInBits, kSecPrivateKeyAttrs,
    };
    const void *values[] = {
        kSecAttrKeyTypeECSECPrimeRandom, keySize, privateAttrs,
    };
    CFDictionaryRef attrs = CFDictionaryCreate(
            nullptr, keys, values, 3, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
    CFErrorRef cfError = nullptr;
    SecKeyRef key = SecKeyCreateRandomKey(attrs, &cfError);
    if (privateAttrs)
        CFRelease(privateAttrs);
    if (attrs)
        CFRelease(attrs);
    if (keySize)
        CFRelease(keySize);
    if (!key && error)
        *error = macError(cfError);
    if (cfError)
        CFRelease(cfError);
    return key;
}
#endif

} // namespace

struct ClientIdentityKey::Impl
{
#if defined(Q_OS_WIN)
    NCRYPT_KEY_HANDLE key = 0;
#elif defined(Q_OS_MACOS)
    SecKeyRef key = nullptr;
    CFDataRef tag = nullptr;
#else
    EVP_PKEY *key = nullptr;
#endif
    QByteArray publicKeySpki;
};

ClientIdentityKey::ClientIdentityKey(std::unique_ptr<Impl> impl)
    : d(std::move(impl))
{
}

ClientIdentityKey::~ClientIdentityKey()
{
#if defined(Q_OS_WIN)
    closeCngKey(d->key);
#elif defined(Q_OS_MACOS)
    if (d->key)
        CFRelease(d->key);
    if (d->tag)
        CFRelease(d->tag);
#else
    if (d->key)
        EVP_PKEY_free(d->key);
#endif
}

std::shared_ptr<ClientIdentityKey> ClientIdentityKey::loadOrCreate(QString *error)
{
    auto impl = std::make_unique<Impl>();
#if defined(Q_OS_WIN)
    impl->key = openOrCreateCngKey(error);
    if (!impl->key)
        return {};
    impl->publicKeySpki = exportCngPublicKey(impl->key, error);
#elif defined(Q_OS_MACOS)
    impl->tag = createKeyTag();
    if (!impl->tag) {
        if (error)
            *error = QStringLiteral("无法创建 Keychain 身份标签");
        return {};
    }
    impl->key = findKey(impl->tag, error);
    if (!impl->key)
        impl->key = createKey(impl->tag, error);
    if (!impl->key)
        return {};
    CFErrorRef cfError = nullptr;
    SecKeyRef publicKey = SecKeyCopyPublicKey(impl->key);
    CFDataRef raw = publicKey
            ? SecKeyCopyExternalRepresentation(publicKey, &cfError) : nullptr;
    impl->publicKeySpki = rawPointToSpki(cfDataToByteArray(raw));
    if (raw)
        CFRelease(raw);
    if (publicKey)
        CFRelease(publicKey);
    if (impl->publicKeySpki.isEmpty() && error)
        *error = cfError ? macError(cfError) : QStringLiteral("无法导出 Keychain 公钥");
    if (cfError)
        CFRelease(cfError);
#else
    const QString path = keyFilePath();
    impl->key = readP256(path, error);
    if (!impl->key) {
        // 文件已存在却无法解析时绝不覆盖：只有服务端明确拒绝公钥后才轮换。
        if (QFile::exists(path))
            return {};
        impl->key = generateP256();
        if (!impl->key) {
            if (error)
                *error = QStringLiteral("生成客户端身份密钥失败：%1").arg(opensslError());
            return {};
        }
        if (!writeP256(path, impl->key, error))
            return {};
    }
    unsigned char *der = nullptr;
    const int length = i2d_PUBKEY(impl->key, &der);
    if (length <= 0 || !der) {
        if (error)
            *error = QStringLiteral("导出客户端身份公钥失败：%1").arg(opensslError());
        return {};
    }
    impl->publicKeySpki = QByteArray(reinterpret_cast<const char *>(der), length);
    OPENSSL_free(der);
#endif
    if (impl->publicKeySpki.isEmpty()) {
        if (error && error->isEmpty())
            *error = QStringLiteral("客户端身份公钥为空");
        return {};
    }
    return std::shared_ptr<ClientIdentityKey>(new ClientIdentityKey(std::move(impl)));
}

bool ClientIdentityKey::reset(QString *error)
{
#if defined(Q_OS_WIN)
    const wchar_t *name = L"NekoMusic.PC.ClientIdentity.v1";
    const wchar_t *providers[] = {MS_PLATFORM_CRYPTO_PROVIDER, MS_KEY_STORAGE_PROVIDER};
    QString firstError;
    for (const wchar_t *providerName : providers) {
        NCRYPT_PROV_HANDLE provider = 0;
        if (NCryptOpenStorageProvider(&provider, providerName, 0) != ERROR_SUCCESS)
            continue;
        NCRYPT_KEY_HANDLE key = 0;
        SECURITY_STATUS status = NCryptOpenKey(provider, &key, name, 0, 0);
        if (status == ERROR_SUCCESS) {
            status = NCryptDeleteKey(key, 0);
            if (status == ERROR_SUCCESS)
                key = 0; // NCryptDeleteKey 成功后句柄已释放
        }
        closeCngKey(key);
        NCryptFreeObject(provider);
        if (status != ERROR_SUCCESS && status != NTE_BAD_KEYSET && firstError.isEmpty())
            firstError = winError("删除 Windows CNG 身份密钥", status);
    }
    if (!firstError.isEmpty()) {
        if (error)
            *error = firstError;
        return false;
    }
    return true;
#elif defined(Q_OS_MACOS)
    CFDataRef tag = createKeyTag();
    if (!tag) {
        if (error)
            *error = QStringLiteral("无法创建 Keychain 身份标签");
        return false;
    }
    const void *keys[] = {kSecClass, kSecAttrApplicationTag};
    const void *values[] = {kSecClassKey, tag};
    CFDictionaryRef query = CFDictionaryCreate(
            nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
            &kCFTypeDictionaryValueCallBacks);
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    CFRelease(tag);
    if (status != errSecSuccess && status != errSecItemNotFound) {
        if (error)
            *error = QStringLiteral("删除 Keychain 身份密钥失败（%1）").arg(status);
        return false;
    }
    return true;
#else
    const QString path = keyFilePath();
    if (!QFile::exists(path))
        return true;
    if (QFile::remove(path))
        return true;
    if (error)
        *error = QStringLiteral("无法删除客户端身份密钥文件");
    return false;
#endif
}

QByteArray ClientIdentityKey::publicKeySpkiDer() const
{
    return d->publicKeySpki;
}

QByteArray ClientIdentityKey::signSha256(const QByteArray &data, QString *error) const
{
#if defined(Q_OS_WIN)
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        if (error)
            *error = QStringLiteral("无法打开 Windows SHA-256 实现");
        return {};
    }
    unsigned char hash[32] = {};
    const NTSTATUS hashStatus = BCryptHash(
            algorithm, nullptr, 0,
            reinterpret_cast<PUCHAR>(const_cast<char *>(data.constData())),
            static_cast<ULONG>(data.size()), hash, sizeof(hash));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (hashStatus != 0) {
        if (error)
            *error = winError("计算请求摘要", hashStatus);
        return {};
    }
    DWORD size = 0;
    SECURITY_STATUS status = NCryptSignHash(d->key, nullptr, hash, sizeof(hash),
                                            nullptr, 0, &size, 0);
    if (status != ERROR_SUCCESS) {
        if (error)
            *error = winError("签名请求", status);
        return {};
    }
    QByteArray signature(static_cast<int>(size), '\0');
    status = NCryptSignHash(d->key, nullptr, hash, sizeof(hash),
                            reinterpret_cast<PBYTE>(signature.data()), size, &size, 0);
    if (status != ERROR_SUCCESS) {
        if (error)
            *error = winError("签名请求", status);
        return {};
    }
    signature.resize(static_cast<int>(size));
    return rawSignatureToDer(signature);
#elif defined(Q_OS_MACOS)
    CFDataRef input = CFDataCreate(nullptr,
                                   reinterpret_cast<const UInt8 *>(data.constData()),
                                   static_cast<CFIndex>(data.size()));
    CFErrorRef cfError = nullptr;
    CFDataRef signature = SecKeyCreateSignature(
            d->key, kSecKeyAlgorithmECDSASignatureMessageX962SHA256, input, &cfError);
    QByteArray result = cfDataToByteArray(signature);
    if (input)
        CFRelease(input);
    if (signature)
        CFRelease(signature);
    if (result.isEmpty() && error)
        *error = macError(cfError);
    if (cfError)
        CFRelease(cfError);
    return result;
#else
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        if (error)
            *error = opensslError();
        return {};
    }
    size_t length = 0;
    QByteArray signature;
    if (EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, d->key) != 1
        || EVP_DigestSignUpdate(ctx, data.constData(), static_cast<size_t>(data.size())) != 1
        || EVP_DigestSignFinal(ctx, nullptr, &length) != 1) {
        if (error)
            *error = QStringLiteral("签名请求失败：%1").arg(opensslError());
        EVP_MD_CTX_free(ctx);
        return {};
    }
    signature.resize(static_cast<int>(length));
    if (EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char *>(signature.data()), &length) != 1) {
        if (error)
            *error = QStringLiteral("签名请求失败：%1").arg(opensslError());
        EVP_MD_CTX_free(ctx);
        return {};
    }
    signature.resize(static_cast<int>(length));
    EVP_MD_CTX_free(ctx);
    return signature;
#endif
}
