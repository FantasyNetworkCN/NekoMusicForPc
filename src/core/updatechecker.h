#pragma once

/**
 * @file updatechecker.h
 * @brief 版本更新检查器
 *
 * 从 API 获取 /version，对比当前版本，提供下载链接。
 */

#include <QObject>
#include "core/nekonetworkaccessmanager.h"
#include <QNetworkReply>
#include <QString>
#include <QStringList>

enum class UpdateInstallKind {
    /** 下载安装包（Windows exe、Linux deb 等） */
    DownloadInstaller,
    /** Arch：通过 yay / paru / pacman 更新 AUR 包 */
    ArchAurHelper,
    /** 仅打开网页（无可用包管理器时） */
    OpenWebPage,
};

struct UpdateInfo {
    bool hasUpdate = false;
    QString version;
    QString downloadUrl;
    UpdateInstallKind installKind = UpdateInstallKind::DownloadInstaller;
};

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(const QString &currentVersion, QObject *parent = nullptr);

    void checkForUpdates();
    /** 为指定版本加载更新日志；依次尝试 GitHub API、Raw 与 jsDelivr 镜像。 */
    void fetchChangelog(const QString &version);
    void downloadUpdate(const QString &url);

    QString currentVersion() const { return m_currentVersion; }

signals:
    void updateAvailable(const UpdateInfo &info);
    void changelogReady(const QString &version, const QString &markdown);
    void changelogFailed(const QString &version, const QString &error);
    void noUpdate();
    void checkFailed(const QString &error);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(const QString &filePath);
    void downloadFailed(const QString &error);

private:
    QString getOSType() const;
    QString getPlatformKey() const;
    UpdateInstallKind resolveInstallKind(const QString &platformKey,
                                         const QString &downloadUrl) const;
    void fetchChangelogSource(const QString &version, const QStringList &sources, int index,
                              const QStringList &errors);
    static QString extractChangelogSection(const QString &markdown, const QString &version);

    QString m_currentVersion;
    NekoNetworkAccessManager m_nam;
};
