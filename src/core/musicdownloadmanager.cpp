#include "musicdownloadmanager.h"
#include "apiclient.h"
#include "musicdownloader.h"
#include "playlistdb.h"
#include "theme/theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace {

QString sanitizeFileComponent(const QString &text)
{
    return QString(text).replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")), QStringLiteral("_"));
}

QString extensionFromBuffer(const QByteArray &head)
{
    if (head.startsWith("fLaC"))
        return QStringLiteral("flac");
    if (head.startsWith("RIFF") && head.size() >= 12 && head.mid(8, 4) == "WAVE")
        return QStringLiteral("wav");
    if (head.startsWith("OggS"))
        return QStringLiteral("ogg");
    if (head.startsWith("ID3"))
        return QStringLiteral("mp3");
    if (head.size() >= 2) {
        const auto b0 = static_cast<unsigned char>(head[0]);
        const auto b1 = static_cast<unsigned char>(head[1]);
        if (b0 == 0xFF && (b1 & 0xE0) == 0xE0)
            return QStringLiteral("mp3");
    }
    return QStringLiteral("mp3");
}

QString extensionFromContentType(const QString &contentType)
{
    const QString ct = contentType.toLower();
    if (ct.contains(QStringLiteral("flac")))
        return QStringLiteral("flac");
    if (ct.contains(QStringLiteral("wav")))
        return QStringLiteral("wav");
    if (ct.contains(QStringLiteral("ogg")))
        return QStringLiteral("ogg");
    if (ct.contains(QStringLiteral("aac")))
        return QStringLiteral("aac");
    if (ct.contains(QStringLiteral("m4a")) || ct.contains(QStringLiteral("mp4")))
        return QStringLiteral("m4a");
    if (ct.contains(QStringLiteral("mpeg")) || ct.contains(QStringLiteral("mp3")))
        return QStringLiteral("mp3");
    return QStringLiteral("mp3");
}

QString buildAudioFileName(const MusicInfo &music, const QString &extension)
{
    const QString base = sanitizeFileComponent(
        QStringLiteral("%1 - %2").arg(music.artist, music.title));
    return base + QLatin1Char('.') + extension;
}

QString buildLyricsFileName(const MusicInfo &music)
{
    return sanitizeFileComponent(QStringLiteral("%1 - %2.lrc").arg(music.artist, music.title));
}

QString detectExtension(const QString &path, const QString &contentType = {})
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        const QString ext = extensionFromBuffer(f.read(16));
        if (!ext.isEmpty())
            return ext;
    }
    if (!contentType.isEmpty())
        return extensionFromContentType(contentType);
    return QStringLiteral("mp3");
}

} // namespace

MusicDownloadManager &MusicDownloadManager::instance()
{
    static MusicDownloadManager inst;
    return inst;
}

MusicDownloadManager::MusicDownloadManager(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

MusicDownloadManager::~MusicDownloadManager()
{
    cancelCurrent();
}

void MusicDownloadManager::setApiClient(ApiClient *apiClient)
{
    m_apiClient = apiClient;
}

QString MusicDownloadManager::downloadDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString dir = base + QStringLiteral("/NekoMusic");
    QDir().mkpath(dir);
    return dir;
}

bool MusicDownloadManager::isDownloaded(int musicId) const
{
    const QString path = PlaylistDatabase::instance().getDownloadFilePath(musicId);
    return !path.isEmpty() && QFile::exists(path);
}

bool MusicDownloadManager::isPending(int musicId) const
{
    if (musicId <= 0)
        return false;
    if (m_busy && m_current.id == musicId)
        return true;
    for (const MusicInfo &queued : m_queue) {
        if (queued.id == musicId)
            return true;
    }
    return false;
}

bool MusicDownloadManager::hasPendingDownloads() const
{
    return (m_busy && m_current.id > 0) || !m_queue.isEmpty();
}

QList<MusicInfo> MusicDownloadManager::pendingDownloads() const
{
    QList<MusicInfo> list;
    if (m_busy && m_current.id > 0 && !isDownloaded(m_current.id))
        list.append(m_current);
    for (const MusicInfo &queued : m_queue) {
        if (!isDownloaded(queued.id))
            list.append(queued);
    }
    return list;
}

bool MusicDownloadManager::isActiveDownload(int musicId) const
{
    return m_busy && m_current.id == musicId;
}

qint64 MusicDownloadManager::progressReceived(int musicId) const
{
    if (isActiveDownload(musicId))
        return m_progressReceived;
    return 0;
}

qint64 MusicDownloadManager::progressTotal(int musicId) const
{
    if (isActiveDownload(musicId))
        return m_progressTotal;
    return 0;
}

bool MusicDownloadManager::downloadMusic(const MusicInfo &music)
{
    if (music.id <= 0 || music.isLocalFile())
        return false;

    if (isDownloaded(music.id)) {
        emit downloadCompleted(music.id);
        emit downloadsChanged();
        return false;
    }

    for (const MusicInfo &queued : m_queue) {
        if (queued.id == music.id)
            return false;
    }
    if (m_busy && m_current.id == music.id)
        return false;

    m_queue.append(music);
    emit downloadsChanged();
    if (!m_busy)
        startNext();
    return true;
}

int MusicDownloadManager::enqueueAll(const QList<MusicInfo> &songs)
{
    if (songs.isEmpty())
        return 0;

    // 队列内去重
    QSet<int> known;
    known.reserve(m_queue.size() + songs.size() + 1);
    if (m_busy && m_current.id > 0)
        known.insert(m_current.id);
    for (const MusicInfo &queued : m_queue)
        known.insert(queued.id);

    // 已下载的一次性读出来，避免逐首 isDownloaded() 查库 + stat
    QSet<int> downloadedIds;
    for (const MusicInfo &row : PlaylistDatabase::instance().getDownloads()) {
        if (!row.localPath.isEmpty() && QFile::exists(row.localPath))
            downloadedIds.insert(row.id);
    }

    int queuedCount = 0;
    for (const MusicInfo &music : songs) {
        if (music.id <= 0 || music.isLocalFile())
            continue;
        if (downloadedIds.contains(music.id) || known.contains(music.id))
            continue;
        known.insert(music.id);
        m_queue.append(music);
        ++queuedCount;
    }

    if (queuedCount <= 0)
        return 0;

    emit downloadsChanged();
    if (!m_busy)
        startNext();
    return queuedCount;
}

void MusicDownloadManager::abortCurrentTransfer()
{
    if (m_reply) {
        m_reply->disconnect();
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file) {
        if (m_file->isOpen())
            m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    if (!m_tempPath.isEmpty())
        QFile::remove(m_tempPath);
    m_tempPath.clear();
    m_progressReceived = 0;
    m_progressTotal = 0;
}

void MusicDownloadManager::cancelCurrent()
{
    abortCurrentTransfer();
    m_busy = false;
    m_current = {};
}

void MusicDownloadManager::cancelDownload(int musicId)
{
    if (musicId <= 0)
        return;

    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].id == musicId) {
            m_queue.removeAt(i);
            emit downloadCancelled(musicId);
            emit downloadsChanged();
            return;
        }
    }

    if (!m_busy || m_current.id != musicId)
        return;

    abortCurrentTransfer();
    m_current = {};
    m_busy = false;
    emit downloadCancelled(musicId);
    emit downloadsChanged();
    startNext();
}

void MusicDownloadManager::startNext()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        m_current = {};
        return;
    }

    m_busy = true;
    m_current = m_queue.takeFirst();
    m_progressReceived = 0;
    m_progressTotal = 0;
    emit downloadsChanged();

    if (isDownloaded(m_current.id)) {
        finishCurrent(true);
        return;
    }

    const QString cachePath = MusicDownloader::cachedAudioFilePath(m_current.id);
    if (QFile::exists(cachePath)) {
        copyCachedToDownload(m_current, cachePath);
        return;
    }

    startNetworkDownload(m_current);
}

void MusicDownloadManager::finishCurrent(bool success, const QString &error)
{
    const int musicId = m_current.id;
    m_current = {};
    m_busy = false;
    m_progressReceived = 0;
    m_progressTotal = 0;

    if (success)
        emit downloadCompleted(musicId);
    else if (musicId > 0)
        emit downloadFailed(musicId, error);

    emit downloadsChanged();
    startNext();
}

void MusicDownloadManager::copyCachedToDownload(const MusicInfo &music, const QString &cachePath)
{
    const QString ext = detectExtension(cachePath);
    const QString finalPath = downloadDir() + QLatin1Char('/') + buildAudioFileName(music, ext);

    if (QFile::exists(finalPath))
        QFile::remove(finalPath);
    if (!QFile::copy(cachePath, finalPath)) {
        finishCurrent(false, QStringLiteral("复制下载文件失败"));
        return;
    }

    if (!finalizeDownload(music, finalPath)) {
        finishCurrent(false, QStringLiteral("保存下载文件失败"));
        return;
    }
    finishCurrent(true);
}

void MusicDownloadManager::startNetworkDownload(const MusicInfo &music)
{
    m_tempPath = downloadDir() + QLatin1Char('/') + QString::number(music.id) + QStringLiteral(".part");
    QFile::remove(m_tempPath);

    m_file = new QFile(m_tempPath, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        finishCurrent(false, QStringLiteral("无法创建下载文件"));
        return;
    }

    const QUrl url(QString::fromUtf8("%1/api/music/file/%2")
                       .arg(QString::fromUtf8(Theme::kApiBase))
                       .arg(music.id));
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &MusicDownloadManager::onNetworkProgress);
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_reply && m_file && m_file->isOpen())
            m_file->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::finished, this, &MusicDownloadManager::onNetworkFinished);
}

void MusicDownloadManager::onNetworkProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (m_current.id <= 0)
        return;
    m_progressReceived = bytesReceived;
    m_progressTotal = bytesTotal;
    emit downloadProgress(m_current.id, bytesReceived, bytesTotal);
}

void MusicDownloadManager::onNetworkFinished()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->errorString();
        reply->deleteLater();
        if (m_file) {
            m_file->close();
            delete m_file;
            m_file = nullptr;
        }
        QFile::remove(m_tempPath);
        m_tempPath.clear();
        finishCurrent(false, err);
        return;
    }

    if (m_file && m_file->isOpen()) {
        m_file->write(reply->readAll());
        m_file->close();
    }
    reply->deleteLater();

    if (!m_file) {
        finishCurrent(false, QStringLiteral("下载文件写入失败"));
        return;
    }
    delete m_file;
    m_file = nullptr;

    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const bool ok = finalizeDownload(m_current, m_tempPath, contentType);
    QFile::remove(m_tempPath);
    m_tempPath.clear();
    finishCurrent(ok, ok ? QString() : QStringLiteral("保存下载文件失败"));
}

bool MusicDownloadManager::finalizeDownload(const MusicInfo &music, const QString &sourcePath,
                                            const QString &contentType)
{
    const QString ext = detectExtension(sourcePath, contentType);
    const QString finalPath = downloadDir() + QLatin1Char('/') + buildAudioFileName(music, ext);

    if (QFile::exists(finalPath))
        QFile::remove(finalPath);
    if (sourcePath != finalPath) {
        if (!QFile::rename(sourcePath, finalPath)) {
            if (!QFile::copy(sourcePath, finalPath))
                return false;
            QFile::remove(sourcePath);
        }
    }

    PlaylistDatabase::instance().recordDownload(music, finalPath);
    saveLyrics(music);
    emit downloadsChanged();
    return true;
}

void MusicDownloadManager::saveLyrics(const MusicInfo &music)
{
    if (!m_apiClient)
        return;

    m_apiClient->fetchLyrics(music.id, [music, this](bool ok, const QString &lyrics) {
        if (!ok || lyrics.trimmed().isEmpty())
            return;

        const QString path = downloadDir() + QLatin1Char('/') + buildLyricsFileName(music);
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        out.write(lyrics.toUtf8());
        out.commit();
    });
}
