#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QFile>
#include <QUrl>

class MusicDownloader : public QObject
{
    Q_OBJECT
public:
    static MusicDownloader& instance();

    /** 与按 musicId 下载时的落盘路径一致（无扩展名，由 FFmpeg 嗅探格式）。 */
    static QString cachedAudioFilePath(int musicId);

    /** 删除 nekomusic-cache 下旧版 URL-MD5 文件名缓存（仅识别 32 位小写十六进制基名）。 */
    static void purgeLegacyMd5CacheFiles();

    void download(const QUrl &url, int musicId = 0);
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
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onReplyFinished();
    void onReadyRead();
    void abortOversizeDownload();

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QFile *m_file = nullptr;
    QString m_tempPath;
    bool m_bufferEmitted = false;
    qint64 m_bytesReceived = 0;
    qint64 m_bytesTotal = 0;
};
