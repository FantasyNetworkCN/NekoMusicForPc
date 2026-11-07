#pragma once

/**
 * @file logindialog.h
 * @brief 登录/注册对话框
 */

#include <QDialog>

class QLabel;
class QPushButton;
class QWidget;
class QCheckBox;
class ApiClient;
class QTimer;
class QNetworkReply;

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog() override;

private:
    void setupUi();
    void applyDialogTheme();
    void updateDialogSize();
    void refreshQrSession();
    void startQrWatch(const QString &sessionId, const QString &deliveryKey, int generation);
    void stopQrSession();
    void doSsoLogin();
    void doOpenRegister();
    void doOpenAccountCenter();
    /** 校验用户已勾选同意协议；未勾选时提示并返回 false。 */
    bool ensureConsent();
    /** 同步单点登录按钮与协议勾选状态（加载中不覆盖禁用）。 */
    void refreshSubmitEnabled();
    void setMsg(const QString &text, const QColor &color);

    QWidget *m_card = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QPushButton *m_ssoBtn = nullptr;
    QPushButton *m_switchBtn = nullptr;
    QPushButton *m_forgotBtn = nullptr;
    QLabel *m_msgLabel = nullptr;
    QLabel *m_qrImageLabel = nullptr;
    QLabel *m_qrTipLabel = nullptr;
    QCheckBox *m_consentCheck = nullptr;
    QLabel *m_consentText = nullptr;
    ApiClient *m_api = nullptr;
    QNetworkReply *m_qrReply = nullptr;
    /** 递增即作废在途的二维码请求/SSE 回调 */
    int m_qrGeneration = 0;
    bool m_loading = false;
};
