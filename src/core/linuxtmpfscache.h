#pragma once

/**
 * @file linuxtmpfscache.h
 * @brief Linux tmpfs 缓存管控：限制体积、LRU 淘汰，避免长期占用内存导致 OOM
 */

#include <QString>
#include <QStringList>
#include <QtGlobal>

class LinuxTmpfsCache
{
public:
    /** 单首音频下载体积上限（500 MiB） */
    static constexpr qint64 kMaxAudioFileBytes = 500LL * 1024 * 1024;
    /** 音频 tmpfs 总缓存上限（768 MiB，仅 Linux 生效） */
    static constexpr qint64 kMaxAudioCacheTotalBytes = 768LL * 1024 * 1024;

    static QString audioCacheDir();

    /** 启动时清理陈旧 .part 并做一次 LRU 淘汰 */
    static void runStartupMaintenance();

    /** 按 LRU 淘汰音频缓存，保留 protectedPaths 中的文件 */
    static void evictAudioCacheExcept(const QStringList &protectedPaths = {});

    /** 播放命中缓存时刷新访问时间，便于 LRU */
    static void touchAudioCacheFile(const QString &path);

    static bool isAudioDownloadOversize(qint64 bytesReceived, qint64 bytesTotal);
};
