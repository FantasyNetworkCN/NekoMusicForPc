/**
 * @file covercache.cpp
 * @brief 封面图片缓存实现
 *
 * Linux：进程内内存 LRU，避免 tmpfs 持续增长。
 * 其他平台：<Temp>/nekomusic-cache/covers/
 */

#include "covercache.h"
#include "theme/theme.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

CoverCache *CoverCache::instance()
{
    static CoverCache inst;
    return &inst;
}

CoverCache::CoverCache(QObject *parent) : QObject(parent)
{
    m_nam.setTransferTimeout(10000);
}

QString CoverCache::musicIdFromCoverUrl(const QString &coverUrl)
{
    const int slash = coverUrl.lastIndexOf(QLatin1Char('/'));
    if (slash < 0)
        return {};
    QString id = coverUrl.mid(slash + 1);
    const int q = id.indexOf(QLatin1Char('?'));
    if (q >= 0)
        id.truncate(q);
    return id;
}

QString CoverCache::resolveCoverUrl(const QString &rawUrl)
{
    const QString u = rawUrl.trimmed();
    if (u.isEmpty())
        return {};

    if (u.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        return u;
    if (u.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || u.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        return u;
    }
    if (u.startsWith(QLatin1String("//"))) {
        return QStringLiteral("https:") + u;
    }

    QString base = QString::fromUtf8(Theme::kApiBase);
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (u.startsWith(QLatin1Char('/')))
        return base + u;
    return base + QLatin1Char('/') + u;
}

QString CoverCache::cacheDir() const
{
#ifndef Q_OS_LINUX
    if (!m_cacheDirInitialized) {
        QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        m_cacheDir = base + QStringLiteral("/nekomusic-cache/covers");
        m_cacheDirInitialized = true;
    }
    return m_cacheDir;
#else
    return {};
#endif
}

#ifdef Q_OS_LINUX

qint64 CoverCache::pixmapBytes(const QPixmap &pixmap)
{
    if (pixmap.isNull())
        return 0;
    return qint64(pixmap.width()) * pixmap.height() * 4;
}

QPixmap CoverCache::getLinuxMem(const QString &musicId) const
{
    if (musicId.isEmpty() || !m_linuxMemCache.contains(musicId))
        return {};
    m_linuxMemLru.removeAll(musicId);
    m_linuxMemLru.append(musicId);
    return m_linuxMemCache.value(musicId);
}

void CoverCache::setLinuxMem(const QString &musicId, const QPixmap &pixmap)
{
    if (musicId.isEmpty() || pixmap.isNull())
        return;

    if (m_linuxMemCache.contains(musicId)) {
        m_linuxMemBytes -= pixmapBytes(m_linuxMemCache.value(musicId));
        m_linuxMemLru.removeAll(musicId);
    }

    m_linuxMemCache.insert(musicId, pixmap);
    m_linuxMemLru.append(musicId);
    m_linuxMemBytes += pixmapBytes(pixmap);
    trimLinuxMemCache();
}

void CoverCache::trimLinuxMemCache()
{
    while (m_linuxMemBytes > kLinuxCoverMemLimitBytes && !m_linuxMemLru.isEmpty()) {
        const QString oldest = m_linuxMemLru.takeFirst();
        const QPixmap removed = m_linuxMemCache.take(oldest);
        m_linuxMemBytes -= pixmapBytes(removed);
    }
}

#endif

void CoverCache::ensureCacheDir() const
{
#ifndef Q_OS_LINUX
    QDir dir;
    dir.mkpath(cacheDir());
#endif
}

QPixmap CoverCache::get(const QString &musicId) const
{
    if (musicId.isEmpty()) return QPixmap();
#ifdef Q_OS_LINUX
    return getLinuxMem(musicId);
#else
    QString path = cacheDir() + QLatin1Char('/') + musicId + QStringLiteral(".jpg");
    if (QFile::exists(path)) {
        QPixmap pix;
        if (pix.load(path)) return pix;
        QFile::remove(path);
    }
    return QPixmap();
#endif
}

void CoverCache::set(const QString &musicId, const QPixmap &pixmap)
{
    if (musicId.isEmpty() || pixmap.isNull()) return;
#ifdef Q_OS_LINUX
    setLinuxMem(musicId, pixmap);
#else
    ensureCacheDir();
    QString path = cacheDir() + QLatin1Char('/') + musicId + QStringLiteral(".jpg");
    pixmap.save(path, "JPEG", 85);
#endif
}

void CoverCache::fetchCover(const QString &musicId, const QString &coverUrl)
{
    if (musicId.isEmpty())
        return;
    const QString absolute = resolveCoverUrl(coverUrl);
    if (absolute.isEmpty())
        return;

    if (absolute.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        QPixmap pix;
        if (pix.load(QUrl(absolute).toLocalFile())) {
            set(musicId, pix);
            emit coverLoaded(musicId, pix);
        }
        return;
    }

    QPixmap cached = get(musicId);
    if (!cached.isNull()) {
        emit coverLoaded(musicId, cached);
        return;
    }

    if (m_inFlight.contains(musicId))
        return;

    QNetworkRequest req;
    req.setUrl(QUrl(absolute));
    req.setTransferTimeout(10000);
    QNetworkReply *reply = m_nam.get(req);
    m_inFlight.insert(musicId, reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, musicId]() {
        m_inFlight.remove(musicId);
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        QPixmap pix;
        if (pix.loadFromData(reply->readAll())) {
            set(musicId, pix);
            emit coverLoaded(musicId, pix);
        }
    });
}

void CoverCache::clear()
{
    const QList<QNetworkReply *> reps = m_inFlight.values();
    m_inFlight.clear();
    for (QNetworkReply *reply : reps) {
        if (reply) {
            reply->disconnect();
            reply->abort();
            reply->deleteLater();
        }
    }

#ifdef Q_OS_LINUX
    m_linuxMemCache.clear();
    m_linuxMemLru.clear();
    m_linuxMemBytes = 0;
#else
    ensureCacheDir();
    QDir dir(cacheDir());
    const auto entries = dir.entryInfoList({QStringLiteral("*.jpg")}, QDir::Files);
    for (const auto &entry : entries) {
        QFile::remove(entry.absoluteFilePath());
    }
#endif
}
