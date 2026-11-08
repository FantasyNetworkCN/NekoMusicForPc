#pragma once

/**
 * @file updatedialog.h
 * @brief 版本更新通知对话框
 */

#include <QDialog>

#include "core/updatechecker.h"

class QLabel;
class QPushButton;
class QProgressBar;
class QTextBrowser;

class UpdateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UpdateDialog(const QString &currentVersion, const QString &newVersion,
                          const QString &downloadUrl,
                          UpdateInstallKind installKind = UpdateInstallKind::DownloadInstaller,
                          QWidget *parent = nullptr);

signals:
    void downloadRequested(const QString &url);
    void remindLater();

public slots:
    void showDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void showDownloadFinished(const QString &filePath);
    void showDownloadFailed(const QString &error);
    void setChangelog(const QString &version, const QString &markdown);
    void setChangelogFailed(const QString &version);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void setupUi();
    void setupDownloadingUi();
    void setupFinishedUi(const QString &filePath);
    void setupFailedUi(const QString &error);

    QString m_currentVersion;
    QString m_newVersion;
    QString m_downloadUrl;
    UpdateInstallKind m_installKind = UpdateInstallKind::DownloadInstaller;

    QLabel *m_titleLbl = nullptr;
    QLabel *m_currentVerLbl = nullptr;
    QLabel *m_newVerLbl = nullptr;
    QLabel *m_changelogTitle = nullptr;
    QTextBrowser *m_changelogBrowser = nullptr;
    QPushButton *m_remindLaterBtn = nullptr;
    QPushButton *m_updateBtn = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_statusLbl = nullptr;
};
