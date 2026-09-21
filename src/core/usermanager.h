#pragma once

/**
 * @file usermanager.h
 * @brief 用户管理器 — 处理用户登录状态和 Token
 *
 * 单例模式，管理用户登录/登出、Token 存储、用户信息。
 * 只有 Token 会写进 QSettings；昵称等资料只放在内存里，
 * 每次启动用 {@code GET /api/user/info} 拉取，避免本地缓存过期。
 */

#include <QObject>
#include <QVariantMap>

class QSettings;

class UserManager : public QObject
{
    Q_OBJECT

public:
    static UserManager &instance();

    explicit UserManager(QObject *parent = nullptr);

    /// 是否已登录
    bool isLoggedIn() const { return !m_token.isEmpty(); }

    /// 获取当前 Token
    QString token() const { return m_token; }

    /// 获取用户信息
    QVariantMap userInfo() const { return m_userInfo; }

    bool isVip() const { return m_isVip; }
    QString vipExpiresAt() const { return m_vipExpiresAt; }
    void setVipStatus(bool isVip);
    void updateVipStatus(bool isVip, const QString &vipExpiresAt = QString());

    /// 设置登录信息
    void setLoginInfo(const QString &token, const QVariantMap &userInfo);

    /// 更新内存中的昵称（服务端已修改成功后调用；不写本地配置）
    void setNickname(const QString &nickname);

    /// 用服务端返回的资料刷新内存中的用户信息（不写本地配置）
    void applyUserInfo(const QVariantMap &userInfo);

    /// 登出
    void logout();

signals:
    void loginStateChanged();
    void vipStatusChanged();
    /// 用户资料（昵称等）刷新完成；比 loginStateChanged 轻，不触发收藏/歌单等登录副作用
    void userInfoChanged();

private:
    void saveToSettings();
    void loadFromSettings();

    QString m_token;
    QVariantMap m_userInfo;
    bool m_isVip = false;
    QString m_vipExpiresAt;
    QSettings *m_settings = nullptr;
};
