#include "musicdownloader.h"
#include "linuxtmpfscache.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>

MusicDownloader& MusicDownloader::instance()
{
    static MusicDownloader instance;
    return instance;
}

MusicDownloader::MusicDownloader(QObject *parent) : QObject(parent) 
{
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

MusicDownloader::~MusicDownloader()
{
    cancel();
}

QString MusicDownloader::cachedAudioFilePath(int musicId)
{
#ifdef Q_OS_LINUX
    return LinuxTmpfsCache::audioCacheDir() + QLatin1Char('/') + QString::number(musicId);
#else
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/nekomusic-cache");
    QDir().mkpath(cacheDir);
    return cacheDir + QLatin1Char('/') + QString::number(musicId);
#endif
}

void MusicDownloader::purgeLegacyMd5CacheFiles()
{
#ifdef Q_OS_LINUX
    const QString cacheDir = LinuxTmpfsCache::audioCacheDir();
#else
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/nekomusic-cache");
#endif
    if (!QDir(cacheDir).exists())
        return;
    static const QRegularExpression md5Name(QStringLiteral("^[0-9a-f]{32}$"));
    QDirIterator it(cacheDir, QDir::Files);
    while (it.hasNext()) {
        it.next();
        QString name = it.fileInfo().fileName();
        if (name.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive))
            name.chop(5);
        if (!md5Name.match(name).hasMatch())
            continue;
        QFile::remove(it.filePath());
    }
}

void MusicDownloader::cancel()
{
    qDebug() << "[MusicDownloader] cancel called";
    if (m_reply) {
        m_reply->disconnect();
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file) {
        if (m_file->isOpen()) {
            m_file->close();
        }
        delete m_file;
        m_file = nullptr;
    }
    m_bufferEmitted = false;
}

void MusicDownloader::abortOversizeDownload()
{
    const QString partPath = m_tempPath + QStringLiteral(".part");
    cancel();
    QFile::remove(partPath);
#ifdef Q_OS_LINUX
    emit downloadError(QStringLiteral("音频体积超过 500 MiB，已取消缓存"));
#else
    emit downloadError(QStringLiteral("下载已取消"));
#endif
}

void MusicDownloader::download(const QUrl &url, int musicId)
{
    cancel();
    m_bufferEmitted = false;

#ifdef Q_OS_LINUX
    const QString cacheDir = LinuxTmpfsCache::audioCacheDir();
#else
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/nekomusic-cache");
    QDir().mkpath(cacheDir);
#endif
    if (musicId > 0) {
        m_tempPath = cachedAudioFilePath(musicId);
    } else {
        const QString hash = QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Md5).toHex();
        m_tempPath = cacheDir + QLatin1Char('/') + hash;
    }

    if (QFile::exists(m_tempPath)) {
        emit downloadFinished(m_tempPath);
        return;
    }

    const QString partPath = m_tempPath + QStringLiteral(".part");
    QFile::remove(partPath);

#ifdef Q_OS_LINUX
    LinuxTmpfsCache::evictAudioCacheExcept({m_tempPath, partPath});
#endif

    m_file = new QFile(partPath, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        emit downloadError(QStringLiteral("无法创建临时文件"));
        return;
    }

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    m_reply = m_nam.get(req);

    connect(m_reply, &QNetworkReply::downloadProgress, this, &MusicDownloader::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished, this, &MusicDownloader::onReplyFinished);
    connect(m_reply, &QNetworkReply::readyRead, this, &MusicDownloader::onReadyRead);
}

void MusicDownloader::onReadyRead()
{
    if (!m_reply || !m_file)
        return;

    const QByteArray chunk = m_reply->readAll();
#ifdef Q_OS_LINUX
    if (m_file->size() + chunk.size() > LinuxTmpfsCache::kMaxAudioFileBytes) {
        abortOversizeDownload();
        return;
    }
#endif
    m_file->write(chunk);
}

void MusicDownloader::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (!m_reply || !m_file)
        return;

#ifdef Q_OS_LINUX
    if (LinuxTmpfsCache::isAudioDownloadOversize(bytesReceived, bytesTotal)) {
        abortOversizeDownload();
        return;
    }
#endif

    m_bytesReceived = bytesReceived;
    m_bytesTotal = bytesTotal;
    emit downloadProgress(bytesReceived, bytesTotal);
    
    if (bytesTotal > 0 && !m_bufferEmitted) {
        double progress = static_cast<double>(bytesReceived) / bytesTotal;
        if (progress >= 0.3) {
            QString partPath = m_tempPath + QStringLiteral(".part");
            if (QFile::exists(partPath)) {
                m_bufferEmitted = true;
                emit bufferReady(partPath);
            }
        }
    }
}

void MusicDownloader::onReplyFinished()
{
    if (!m_reply) return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit downloadError(reply->errorString());
        reply->deleteLater();
        return;
    }

    if (m_file && m_file->isOpen()) {
        const QByteArray tail = reply->readAll();
#ifdef Q_OS_LINUX
        if (m_file->size() + tail.size() > LinuxTmpfsCache::kMaxAudioFileBytes) {
            abortOversizeDownload();
            reply->deleteLater();
            return;
        }
#endif
        m_file->write(tail);
        m_file->close();
    }

    const QString partPath = m_tempPath + QStringLiteral(".part");
    if (QFile::exists(partPath)) {
#ifdef Q_OS_LINUX
        if (QFileInfo(partPath).size() > LinuxTmpfsCache::kMaxAudioFileBytes) {
            abortOversizeDownload();
            reply->deleteLater();
            return;
        }
#endif
        if (QFile::exists(m_tempPath))
            QFile::remove(m_tempPath);
        if (!QFile::copy(partPath, m_tempPath)) {
            emit downloadError(QStringLiteral("写入完整缓存失败"));
            reply->deleteLater();
            if (m_file) {
                delete m_file;
                m_file = nullptr;
            }
            return;
        }
        QFile::remove(partPath);
    }

#ifdef Q_OS_LINUX
    LinuxTmpfsCache::evictAudioCacheExcept({m_tempPath});
#endif

    emit downloadFinished(m_tempPath);

    reply->deleteLater();
    if (m_file) {
        delete m_file;
        m_file = nullptr;
    }
}
