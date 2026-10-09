#pragma once

/**
 * @file systemnotifier.h
 * @brief 系统级通知（Linux 走 org.freedesktop.Notifications）
 *
 * Linux 上 Qt 的 QSystemTrayIcon::showMessage 只发不收，拿不到点击事件；这里直接走桌面
 * 通知服务并监听 ActionInvoked，点击通知即可唤起应用。其它平台 isSupported() 为 false，
 * 沿用托盘气泡通知（QSystemTrayIcon::messageClicked 在 Windows/macOS 有回调）。
 */

#include <QObject>
#include <QString>

class SystemNotifier : public QObject
{
    Q_OBJECT

public:
    static SystemNotifier &instance();

    /** 当前平台是否有可用的系统通知后端 */
    static bool isSupported();

    /** 弹一条系统通知；返回是否成功下发（失败可回退托盘气泡） */
    bool notify(const QString &title, const QString &body);

signals:
    /** 用户点击了系统通知 */
    void clicked();

private slots:
    /** 桌面环境回报「点了通知上的默认动作」 */
    void handleActionInvoked(uint id, const QString &action);

private:
    explicit SystemNotifier(QObject *parent = nullptr);
    ~SystemNotifier() override;

    /** 最近一条通知的 id：连续多条只保留最新，避免弹窗刷屏 */
    quint32 m_lastId = 0;
};
