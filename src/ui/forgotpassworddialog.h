#pragma once

/**
 * @file forgotpassworddialog.h
 * @brief 找回密码对话框（统一跳转 NekoAuth）
 */

#include <QColor>
#include <QDialog>

class QLabel;
class QPushButton;
class QWidget;
class ApiClient;

class ForgotPasswordDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ForgotPasswordDialog(QWidget *parent = nullptr);
    ~ForgotPasswordDialog() override;

private:
    void setupUi();
    void applyDialogTheme();
    void updateDialogSize();
    void setMsg(const QString &text, const QColor &color);
    void openNekoAuthReset();

    QWidget *m_card = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QPushButton *m_openBtn = nullptr;
    QLabel *m_msgLabel = nullptr;
    ApiClient *m_api = nullptr;
};
