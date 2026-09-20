#pragma once

/**
 * @file usermanager.h
 * @brief 用户管理器 — 处理用户登录状态和 Token
 *
 * 单例模式，管理用户登录/登出、Token 存储、用户信息。
 * Token 保存在 QSettings 中，持久化存储。
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

    /// 更新本地缓存的昵称（服务端已修改成功后调用）
    void setNickname(const QString &nickname);

    /// 登出
    void logout();

signals:
    void loginStateChanged();
    void vipStatusChanged();

private:
    void saveToSettings();
    void loadFromSettings();

    QString m_token;
    QVariantMap m_userInfo;
    bool m_isVip = false;
    QString m_vipExpiresAt;
    QSettings *m_settings = nullptr;
};
