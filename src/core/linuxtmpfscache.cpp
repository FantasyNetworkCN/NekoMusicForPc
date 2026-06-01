#include "linuxtmpfscache.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

#ifdef Q_OS_LINUX

namespace {

QString cacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/nekomusic-cache");
}

QSet<QString> absolutePaths(const QStringList &paths)
{
    QSet<QString> out;
    for (const QString &path : paths) {
        if (path.isEmpty())
            continue;
        out.insert(QFileInfo(path).absoluteFilePath());
    }
    return out;
}

struct AudioEntry {
    QString path;
    qint64 size = 0;
    QDateTime accessed;
};

} // namespace

#endif

QString LinuxTmpfsCache::audioCacheDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/nekomusic-cache");
    QDir().mkpath(dir);
    return dir;
}

void LinuxTmpfsCache::runStartupMaintenance()
{
#ifndef Q_OS_LINUX
    return;
#else
    const QString root = cacheRoot();
    QDir dir(root);
    if (!dir.exists())
        return;

    QDir(root + QStringLiteral("/covers")).removeRecursively();

    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-1);
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (!fi.fileName().endsWith(QStringLiteral(".part"), Qt::CaseInsensitive))
            continue;
        if (fi.lastModified().toUTC() < cutoff)
            QFile::remove(fi.absoluteFilePath());
    }

    evictAudioCacheExcept({});
#endif
}

void LinuxTmpfsCache::evictAudioCacheExcept(const QStringList &protectedPaths)
{
#ifndef Q_OS_LINUX
    Q_UNUSED(protectedPaths);
    return;
#else
    const QString root = cacheRoot();
    QDir dir(root);
    if (!dir.exists())
        return;

    const QSet<QString> protectedAbs = absolutePaths(protectedPaths);
    QVector<AudioEntry> entries;
    qint64 total = 0;

    const auto files = dir.entryInfoList(QDir::Files);
    for (const QFileInfo &fi : files) {
        if (fi.fileName().endsWith(QStringLiteral(".part"), Qt::CaseInsensitive))
            continue;
        if (protectedAbs.contains(fi.absoluteFilePath()))
            continue;
        entries.append({fi.absoluteFilePath(), fi.size(), fi.lastModified()});
        total += fi.size();
    }

    if (total <= kMaxAudioCacheTotalBytes)
        return;

    std::sort(entries.begin(), entries.end(), [](const AudioEntry &a, const AudioEntry &b) {
        return a.accessed < b.accessed;
    });

    for (const AudioEntry &entry : entries) {
        if (total <= kMaxAudioCacheTotalBytes)
            break;
        if (QFile::remove(entry.path))
            total -= entry.size;
    }
#endif
}

void LinuxTmpfsCache::touchAudioCacheFile(const QString &path)
{
#ifndef Q_OS_LINUX
    Q_UNUSED(path);
    return;
#else
    if (path.isEmpty())
        return;
    QFile file(path);
    if (file.exists())
        file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileAccessTime);
#endif
}

bool LinuxTmpfsCache::isAudioDownloadOversize(qint64 bytesReceived, qint64 bytesTotal)
{
#ifdef Q_OS_LINUX
    if (bytesReceived > kMaxAudioFileBytes)
        return true;
    if (bytesTotal > 0 && bytesTotal > kMaxAudioFileBytes)
        return true;
#else
    Q_UNUSED(bytesReceived);
    Q_UNUSED(bytesTotal);
#endif
    return false;
}
