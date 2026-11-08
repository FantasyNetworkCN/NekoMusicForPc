/**
 * @file updatechecker.cpp
 * @brief 版本更新检查器实现
 */

#include "updatechecker.h"
#include "linuxinstallbackend.h"
#include "theme/theme.h"
#include "version.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>

UpdateChecker::UpdateChecker(const QString &currentVersion, QObject *parent)
    : QObject(parent), m_currentVersion(currentVersion)
{
}

void UpdateChecker::checkForUpdates()
{
    QUrl url(QString::fromUtf8("%1/version").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    // 禁用缓存
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setRawHeader("Cache-Control", "no-cache");

    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit checkFailed(reply->errorString());
            return;
        }

        auto doc = QJsonDocument::fromJson(reply->readAll());
        auto root = doc.object();

        // 解析 PC 版本信息
        if (!root.contains("pc")) {
            emit checkFailed("Invalid response format");
            return;
        }

        auto pcObj = root.value("pc").toObject();
        QString remoteVersion = pcObj.value("pc_ver").toString();

        if (remoteVersion.isEmpty()) {
            emit checkFailed("No version info found");
            return;
        }

        // 比较版本号
        if (remoteVersion != m_currentVersion) {
            UpdateInfo info;
            info.hasUpdate = true;
            info.version = remoteVersion;

            const QString platformKey = getPlatformKey();
            QString urlTemplate = pcObj.value(platformKey).toString();
            if (urlTemplate.isEmpty() && platformKey != QStringLiteral("windows"))
                urlTemplate = pcObj.value(QStringLiteral("linux")).toString();
            if (urlTemplate.isEmpty())
                urlTemplate = pcObj.value(QStringLiteral("windows")).toString();

            info.installKind = resolveInstallKind(platformKey, urlTemplate);
            if (info.installKind == UpdateInstallKind::ArchAurHelper) {
                // /version 仅有 windows / mac / linux，Arch 走 AUR，不下载 pc.linux 的 deb
                info.downloadUrl = aurPackagePageUrl();
            } else {
                info.downloadUrl = urlTemplate.replace(QStringLiteral("{pc_ver}"), remoteVersion);
            }
            emit updateAvailable(info);
        } else {
            emit noUpdate();
        }
    });
}

void UpdateChecker::fetchChangelog(const QString &version)
{
    const QStringList sources = {
        QStringLiteral("https://api.github.com/repos/FantasyNetworkCN/NekoMusicForPc/contents/CHANGELOG.md?ref=qt6"),
        QStringLiteral("https://raw.githubusercontent.com/FantasyNetworkCN/NekoMusicForPc/qt6/CHANGELOG.md?v=%1")
            .arg(version),
        QStringLiteral("https://cdn.jsdelivr.net/gh/FantasyNetworkCN/NekoMusicForPc@qt6/CHANGELOG.md?v=%1")
            .arg(version)
    };
    fetchChangelogSource(version, sources, 0, {});
}

void UpdateChecker::fetchChangelogSource(const QString &version, const QStringList &sources,
                                         int index, const QStringList &errors)
{
    if (index < 0 || index >= sources.size()) {
        emit changelogFailed(version, errors.join(QStringLiteral(" | ")));
        return;
    }

    const QUrl sourceUrl(sources.at(index));
    QNetworkRequest req(sourceUrl);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    req.setRawHeader("Cache-Control", "no-cache");
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("NekoMusic-PC/%1").arg(m_currentVersion));
    const bool githubApi = sourceUrl.host().compare(QStringLiteral("api.github.com"),
                                                    Qt::CaseInsensitive) == 0;
    req.setRawHeader("Accept", githubApi ? "application/vnd.github.raw+json" : "text/plain");
    req.setTransferTimeout(10000);

    auto *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, version, sourceUrl, sources, index, errors, githubApi]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QStringList nextErrors = errors;
            nextErrors.append(QStringLiteral("%1 (%2): %3")
                                  .arg(sourceUrl.toString())
                                  .arg(status)
                                  .arg(reply->errorString()));
            qWarning() << "[更新日志] 源加载失败:" << sourceUrl << status << reply->errorString();
            fetchChangelogSource(version, sources, index + 1, nextErrors);
            return;
        }
        const QByteArray raw = reply->readAll();
        QString markdown = QString::fromUtf8(raw).trimmed();
        if (githubApi && markdown.startsWith(QLatin1Char('{'))) {
            const QJsonObject root = QJsonDocument::fromJson(raw).object();
            const QString encoded = root.value(QStringLiteral("content")).toString();
            if (!encoded.isEmpty()) {
                QByteArray decoded = QByteArray::fromBase64(encoded.toLatin1());
                markdown = QString::fromUtf8(decoded).trimmed();
            }
        }
        if (markdown.isEmpty()) {
            QStringList nextErrors = errors;
            nextErrors.append(QStringLiteral("%1: empty changelog").arg(sourceUrl.toString()));
            fetchChangelogSource(version, sources, index + 1, nextErrors);
            return;
        }
        qInfo() << "[更新日志] 已加载:" << sourceUrl;
        const QString section = extractChangelogSection(markdown, version);
        emit changelogReady(version, section.isEmpty()
                                          ? markdown.left(16000)
                                          : section.left(16000));
    });
}

QString UpdateChecker::extractChangelogSection(const QString &markdown, const QString &version)
{
    if (markdown.isEmpty() || version.isEmpty())
        return {};
    const QString pattern = QStringLiteral("^##\\s+(?:\\[)?v?%1(?:\\])?\\b.*$")
                                .arg(QRegularExpression::escape(version));
    const QRegularExpression heading(pattern,
                                     QRegularExpression::MultilineOption
                                         | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = heading.match(markdown);
    if (!match.hasMatch())
        return {};

    const int bodyStart = match.capturedEnd();
    static const QRegularExpression nextHeading(QStringLiteral("^##\\s+.*$"),
                                                QRegularExpression::MultilineOption);
    const QRegularExpressionMatch next = nextHeading.match(markdown, bodyStart);
    const int bodyEnd = next.hasMatch() ? next.capturedStart() : markdown.size();
    return markdown.mid(bodyStart, bodyEnd - bodyStart).trimmed();
}

void UpdateChecker::downloadUpdate(const QString &url)
{
    QUrl fileUrl(url);
    QNetworkRequest req(fileUrl);
    auto *reply = m_nam.get(req);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 bytesReceived, qint64 bytesTotal) {
        emit downloadProgress(bytesReceived, bytesTotal);
    });

    connect(reply, &QNetworkReply::finished, this, [this, url]() {
        auto *reply = qobject_cast<QNetworkReply*>(sender());
        if (!reply) return;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit downloadFailed(reply->errorString());
            return;
        }

        // 保存到下载目录
        QString downloadPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        QDir dir(downloadPath);
        if (!dir.exists()) {
            dir.mkpath(".");
        }

        // 从 URL 提取文件名
        QUrl fileUrl(url);
        QString fileName = fileUrl.fileName();
        if (fileName.isEmpty()) {
            fileName = "NekoMusic_update";
        }
        QString filePath = dir.filePath(fileName);

        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            emit downloadFailed("Cannot open file for writing: " + filePath);
            return;
        }

        file.write(reply->readAll());
        file.close();

        emit downloadFinished(filePath);
    });
}

QString UpdateChecker::getOSType() const
{
#if defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_MACOS)
    return "mac";
#elif defined(Q_OS_LINUX)
    return "linux";
#else
    return "windows";
#endif
}

QString UpdateChecker::getPlatformKey() const
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("mac");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("linux");
#else
    return QStringLiteral("windows");
#endif
}

UpdateInstallKind UpdateChecker::resolveInstallKind(const QString &platformKey,
                                                     const QString &downloadUrl) const
{
#if defined(Q_OS_LINUX)
    Q_UNUSED(platformKey);
    if (detectLinuxInstallBackend() == LinuxInstallBackend::Arch)
        return UpdateInstallKind::ArchAurHelper;
    if (detectLinuxInstallBackend() == LinuxInstallBackend::Debian)
        return UpdateInstallKind::DownloadInstaller;
    if (downloadUrl.endsWith(QStringLiteral(".deb"), Qt::CaseInsensitive))
        return UpdateInstallKind::DownloadInstaller;
    return UpdateInstallKind::OpenWebPage;
#elif defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    Q_UNUSED(platformKey);
    Q_UNUSED(downloadUrl);
    return UpdateInstallKind::DownloadInstaller;
#else
    Q_UNUSED(platformKey);
    Q_UNUSED(downloadUrl);
    return UpdateInstallKind::OpenWebPage;
#endif
}
