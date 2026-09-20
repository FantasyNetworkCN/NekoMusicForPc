#pragma once

#include <QObject>
#include <QList>

#include "core/musicinfo.h"

class ApiClient;
class QNetworkAccessManager;
class QNetworkReply;
class QFile;

/** 用户主动下载：保存到系统下载目录/NekoMusic，并记录到本地数据库 */
class MusicDownloadManager : public QObject
{
    Q_OBJECT

public:
    static MusicDownloadManager &instance();

    void setApiClient(ApiClient *apiClient);
    QString downloadDir() const;
    bool isDownloaded(int musicId) const;
    bool isPending(int musicId) const;
    bool hasPendingDownloads() const;
    QList<MusicInfo> pendingDownloads() const;
    bool isActiveDownload(int musicId) const;
    qint64 progressReceived(int musicId) const;
    qint64 progressTotal(int musicId) const;
    bool downloadMusic(const MusicInfo &music);
    /**
     * 「下载全部」批量入队：一次去重、一次 downloadsChanged。
     *
     * 返回真正新入队的数量；已下载 / 已在队列中的会被跳过。
     * 逐首调用 downloadMusic() 时每首都会发一次 downloadsChanged，
     * 几千首就会触发几千次列表重建，是「下载全部」卡死的根因。
     */
    int enqueueAll(const QList<MusicInfo> &songs);
    void cancelDownload(int musicId);
    void cancelCurrent();

signals:
    void downloadProgress(int musicId, qint64 bytesReceived, qint64 bytesTotal);
    void downloadCompleted(int musicId);
    void downloadFailed(int musicId, const QString &error);
    void downloadCancelled(int musicId);
    void downloadsChanged();

private:
    explicit MusicDownloadManager(QObject *parent = nullptr);
    ~MusicDownloadManager() override;

    void startNext();
    void abortCurrentTransfer();
    void finishCurrent(bool success, const QString &error = {});
    void copyCachedToDownload(const MusicInfo &music, const QString &cachePath);
    void startNetworkDownload(const MusicInfo &music);
    void onNetworkProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onNetworkFinished();
    bool finalizeDownload(const MusicInfo &music, const QString &sourcePath,
                          const QString &contentType = {});
    void saveLyrics(const MusicInfo &music);

    ApiClient *m_apiClient = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_file = nullptr;

    QList<MusicInfo> m_queue;
    MusicInfo m_current;
    QString m_tempPath;
    bool m_busy = false;
    qint64 m_progressReceived = 0;
    qint64 m_progressTotal = 0;
};
