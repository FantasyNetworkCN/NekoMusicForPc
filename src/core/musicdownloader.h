#pragma once

#include <QObject>
#include "core/nekonetworkaccessmanager.h"
#include <QFile>
#include <QUrl>

class MusicDownloader : public QObject
{
    Q_OBJECT
public:
    static MusicDownloader& instance();

    /** 与按 musicId 下载时的落盘路径一致（无扩展名，由 FFmpeg 嗅探格式）。 */
    static QString cachedAudioFilePath(int musicId, const QString &quality = QStringLiteral("hq"));
    /** 清理某首在线曲目的临时音频缓存，不触碰用户保存的下载文件。 */
    static void purgeCachedAudio(int musicId);

    /** 删除 nekomusic-cache 下旧版 URL-MD5 文件名缓存（仅识别 32 位小写十六进制基名）。 */
    static void purgeLegacyMd5CacheFiles();

    void download(const QUrl &url, int musicId = 0, const QString &quality = QStringLiteral("hq"));
    void cancel();
    
private:
    explicit MusicDownloader(QObject *parent = nullptr);
    ~MusicDownloader() override;

signals:
    void bufferReady(const QString &localPath);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(const QString &localPath);
    void downloadError(const QString &error);

private:
    /** 真正的下载流程；[download] 会先把音质接口地址解析成固定媒体地址。 */
    void startDownload(const QUrl &url, int musicId, const QString &quality);

    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onReplyFinished();
    void onReadyRead();
    void abortOversizeDownload();

    NekoNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QFile *m_file = nullptr;
    QString m_tempPath;
    QString m_quality = QStringLiteral("hq");
    bool m_bufferEmitted = false;
    qint64 m_bytesReceived = 0;
    qint64 m_bytesTotal = 0;
};
