/**
 * @file usermanager.cpp
 * @brief 用户管理器实现
 */

#include "usermanager.h"
#include <QSettings>

UserManager::UserManager(QObject *parent)
    : QObject(parent)
    , m_settings(new QSettings("NekoMusic", "NekoMusic", this))
{
    loadFromSettings();
}

UserManager &UserManager::instance()
{
    static UserManager inst;
    return inst;
}

void UserManager::setLoginInfo(const QString &token, const QVariantMap &userInfo)
{
    m_token = token;
    m_userInfo = userInfo;
    m_isVip = userInfo.value(QStringLiteral("isVip")).toBool();
    m_vipExpiresAt = userInfo.value(QStringLiteral("vipExpiresAt")).toString();
    saveToSettings();
    emit loginStateChanged();
    emit vipStatusChanged();
}

void UserManager::setNickname(const QString &nickname)
{
    if (nickname.isEmpty())
        return;
    if (m_userInfo.value(QStringLiteral("nickname")).toString() == nickname)
        return;
    m_userInfo[QStringLiteral("nickname")] = nickname;
    saveToSettings();
    emit loginStateChanged();
}

void UserManager::applyUserInfo(const QVariantMap &userInfo)
{
    if (userInfo.isEmpty())
        return;

    const QVariantMap before = m_userInfo;
    const bool vipBefore = m_isVip;
    const QString vipExpiresBefore = m_vipExpiresAt;

    m_userInfo = userInfo;
    m_isVip = userInfo.value(QStringLiteral("isVip")).toBool();
    m_vipExpiresAt = userInfo.value(QStringLiteral("vipExpiresAt")).toString();

    if (before != m_userInfo)
        emit userInfoChanged();
    if (vipBefore != m_isVip || vipExpiresBefore != m_vipExpiresAt)
        emit vipStatusChanged();
}

void UserManager::setVipStatus(bool isVip)
{
    updateVipStatus(isVip, m_vipExpiresAt);
}

void UserManager::updateVipStatus(bool isVip, const QString &vipExpiresAt)
{
    const bool changed = (m_isVip != isVip) || (m_vipExpiresAt != vipExpiresAt);
    m_isVip = isVip;
    m_vipExpiresAt = vipExpiresAt;
    m_userInfo[QStringLiteral("isVip")] = isVip;
    if (!vipExpiresAt.isEmpty())
        m_userInfo[QStringLiteral("vipExpiresAt")] = vipExpiresAt;
    else
        m_userInfo.remove(QStringLiteral("vipExpiresAt"));
    if (changed) {
        saveToSettings();
        emit vipStatusChanged();
    }
}

void UserManager::logout()
{
    m_token.clear();
    m_userInfo.clear();
    m_isVip = false;
    m_vipExpiresAt.clear();
    saveToSettings();
    emit loginStateChanged();
    emit vipStatusChanged();
}

void UserManager::saveToSettings()
{
    // 只持久化 Token；昵称等资料不落地（启动时用 /api/user/info 拉最新值），
    // 顺便清掉旧版本残留的 userInfo 缓存。
    m_settings->setValue("auth/token", m_token);
    m_settings->remove("auth/userInfo");
    m_settings->sync();
}

void UserManager::loadFromSettings()
{
    m_token = m_settings->value("auth/token").toString();
    m_userInfo.clear();
    m_isVip = false;
    m_vipExpiresAt.clear();
    if (!m_token.isEmpty()) {
        emit loginStateChanged();
    }
}
