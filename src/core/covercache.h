#pragma once

/**
 * @file covercache.h
 * @brief 封面图片磁盘缓存
 *
 * 单例类，负责封面图片的磁盘缓存与网络回源。
 * Linux：封面仅保留在进程内存（不写 tmpfs/硬盘）；其他平台仍用磁盘缓存。
 */

#include <QObject>
#include <QPixmap>
#include <QHash>
#include <QNetworkAccessManager>

class QNetworkReply;

class CoverCache : public QObject
{
    Q_OBJECT

public:
    static CoverCache *instance();

    /** 从封面 URL 解析 musicId（路径最后一段，去掉查询串）。 */
    static QString musicIdFromCoverUrl(const QString &coverUrl);

    /** 将相对路径、协议相对 URL 补全为可请求的绝对地址（基于 Theme::kApiBase）。 */
    static QString resolveCoverUrl(const QString &rawUrl);

    /** 获取封面：先查缓存，未命中则从网络下载 */
    void fetchCover(const QString &musicId, const QString &coverUrl);

    /** 从磁盘加载缓存图片，失败返回空 */
    QPixmap get(const QString &musicId) const;

    /** 保存图片到磁盘缓存 */
    void set(const QString &musicId, const QPixmap &pixmap);

    /** 清理全部缓存 */
    void clear();

signals:
    void coverLoaded(const QString &musicId, const QPixmap &pixmap);

private:
    explicit CoverCache(QObject *parent = nullptr);
    QString cacheDir() const;
    void ensureCacheDir() const;

#ifdef Q_OS_LINUX
    QPixmap getLinuxMem(const QString &musicId) const;
    void setLinuxMem(const QString &musicId, const QPixmap &pixmap);
    void trimLinuxMemCache();
    static qint64 pixmapBytes(const QPixmap &pixmap);

    mutable QHash<QString, QPixmap> m_linuxMemCache;
    mutable QStringList m_linuxMemLru;
    mutable qint64 m_linuxMemBytes = 0;
    static constexpr qint64 kLinuxCoverMemLimitBytes = 32LL * 1024 * 1024;
#endif

    mutable QString m_cacheDir;
    mutable bool m_cacheDirInitialized = false;
    QNetworkAccessManager m_nam;
    /** 同一 musicId 只保留一个进行中的请求，避免播放栏与播放页重复拉取。 */
    QHash<QString, QNetworkReply *> m_inFlight;
};
