#pragma once

/**
 * @file titlebar.h
 * @brief 自定义标题栏 — 日系动漫风
 *
 * 56px 紧凑高度，毛玻璃背景。
 * 搜索框聚焦薄荷绿发光，用户头像樱花粉边框。
 */

#include <QWidget>

class QLineEdit;
class QLabel;
class QPushButton;
class QResizeEvent;
#include "core/nekonetworkaccessmanager.h"
class QNetworkReply;
class VipPillButton;

class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);
    ~TitleBar() override;
    void retranslate();
    QPoint avatarPos() const;
    /** 站内消息未读数：0 隐藏红点，>99 显示 99+ */
    void setUnreadCount(int unread);

signals:
    void searchRequested(const QString &query);
    void settingsClicked();
    void avatarClicked();
    void vipClicked();
    void notificationsClicked();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void updateAvatar();
    void updateVipPill();
    void elideNickname();
    void updateChevronPixmap();
    void refreshSearchGlyph();
    void updateNotifyBadge();
    void loadAvatarAsync(const QString &url, int userId);

    NekoNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_avatarReply = nullptr;
    QWidget *m_searchWrap = nullptr;
    QLabel *m_searchGlyph = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_logo = nullptr;
    QLabel *m_name = nullptr;
    QWidget *m_avatarWidget = nullptr;
    QLabel *m_avatarIcon = nullptr;
    QLabel *m_nicknameLabel = nullptr;
    QLabel *m_dropdownIcon = nullptr;
    VipPillButton *m_vipPill = nullptr;
    QPushButton *m_notifyBtn = nullptr;
    QLabel *m_notifyBadge = nullptr;
    QPushButton *m_settingsBtn = nullptr;
    int m_unreadCount = 0;
};
