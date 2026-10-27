#pragma once

/**
 * @file nekonetworkaccessmanager.h
 * @brief 统一附加本站客户端标识、实例身份、请求签名与防重放 nonce 的网络管理器
 */

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>

class QIODevice;
class QNetworkReply;

/**
 * @brief 为发往**本站后端**的请求统一附加客户端标识、实例身份与防重放 nonce
 *
 * 拦截本管理器的全部出站请求，对与 `Theme::kApiBase` 同主机的请求强制注入：
 *   - User-Agent:     NekoMusic-PC/<APP_VERSION>
 *   - X-Neko-Client:  pc+<APP_VERSION>
 *   - Authorization:  `Bearer <用户令牌>`（已登录时补空缺；调用方显式设过的不覆盖）
 *   - 实例身份：       `X-Neko-Client-{Id,Instance,Token,Timestamp,Signature}`
 *   - X-Neko-Nonce:   一次性 nonce（仅同时受防重放保护的路径）
 *
 * 两个标识标头都是**强制统一**的：调用方即使自己设置过 User-Agent 也会被覆盖，
 * 保证全链路客户端标识与后端放行规则严格一致；第三方主机（更新检查 / CDN /
 * 天气等）不注入，避免身份外泄。版本取 CMake `PROJECT_VERSION`（`version.h`），
 * 非硬编码。
 *
 * 实例身份：动态接口由 [ProtectedReply] 先确保本安装实例已注册 / 续签，再用实例私钥对
 * 方法、规范化路径、原始查询串、client_id、instance_id、平台、时间戳、请求体摘要与
 * nonce 签名。服务端返回 `401 + X-Neko-Client-Status: invalid` 时，会合并并发恢复并
 * 重试一次；multipart 上传只签名固定流式体摘要，不参与失败后的自动重试。
 *
 * 防重放：受保护路径（`/api/` 前缀、`/loser/` 前缀、`/version`，豁免清单与后端
 * `filter/ReplayProtectionFilter` 一致）每次尝试都从 [ReplayNonceStore] 取一个新
 * nonce，若被服务端判定为重放（409 + `X-Neko-Replay-Status`）则换新 nonce 重试一次。
 * 静态资源（`/media/`、`/assets/`、安装包）不包装，保持流式与 CDN 语义不变。
 *
 * `/version`（客户端版本检查）当前按同一约定提前携带 nonce，为服务端后续纳管做好兼容：
 * 服务端尚未强制校验时多带一个 nonce 无副作用。
 *
 * 全端所有 `QNetworkAccessManager` 统一改用本类，避免逐处遗漏。
 */
class NekoNetworkAccessManager : public QNetworkAccessManager
{
    Q_OBJECT
public:
    explicit NekoNetworkAccessManager(QObject *parent = nullptr);

    /**
     * 内部使用：绕过防重放包装直接发起请求。
     *
     * 仅供 [ProtectedReply] 发送内部请求时调用；调用方需自行完成身份签名与 nonce 注入。
     */
    QNetworkReply *issueInternal(Operation op, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr)
    {
        return QNetworkAccessManager::createRequest(op, request, outgoingData);
    }

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override;
};
