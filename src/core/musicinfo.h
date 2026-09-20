#ifndef MUSICINFO_H
#define MUSICINFO_H

#include <QtGlobal>
#include <QString>
#include <QFileInfo>

struct MusicInfo {
    int id = 0;
    QString title;
    QString artist;
    QString album;
    int duration = 0;
    QString coverUrl;
    /** 非空表示本机外部音频文件（绝对路径），此时 id 为负数占位，不走在线接口 */
    QString localPath;
    /** 热门榜播放量；<0 表示未设置 */
    int playCount = -1;
    /** 最新音乐上传时间（Unix 毫秒）；0 表示未设置 */
    qint64 uploadedAtMs = 0;
    /** 搜索 API：该曲是否有有效歌词（仅 query 搜索会设置） */
    bool lrc = false;

    bool isLocalFile() const { return !localPath.isEmpty(); }
};

/**
 * 曲目稳定标识：本地文件用规范化路径、在线曲目用 id。
 *
 * 供播放队列去重与随机播放洗牌袋使用——用 id 而不是队列下标，
 * 这样播放列表增删后，已经洗好的顺序不会指向错误的歌。
 */
inline QString musicKeyOf(const MusicInfo &info) {
    if (info.isLocalFile()) {
        const QString canonical = QFileInfo(info.localPath).canonicalFilePath();
        return QStringLiteral("L:") + (canonical.isEmpty() ? info.localPath : canonical);
    }
    return QStringLiteral("R:") + QString::number(info.id);
}

/**
 * 批量去重用的稳定标识。
 *
 * 与 musicKeyOf 的区别：只有确实能唯一标识一首曲目的条目才返回 true——
 * 在线曲目要求 id > 0，本地文件要求路径非空；既没有 id 又不是本地文件的条目
 * 无法去重（保持旧行为，允许重复入队）。
 *
 * 本地文件会做一次 canonicalFilePath()（磁盘 stat），所以调用方应当
 * 每首曲目只调用一次，然后放进 QSet 里复用，切勿在循环比较里反复调用。
 */
inline bool musicDedupeKey(const MusicInfo &info, QString *key) {
    if (!key)
        return false;
    if (info.isLocalFile()) {
        const QString canonical = QFileInfo(info.localPath).canonicalFilePath();
        *key = QStringLiteral("L:") + (canonical.isEmpty() ? info.localPath : canonical);
        return true;
    }
    if (info.id <= 0)
        return false;
    *key = QStringLiteral("R:") + QString::number(info.id);
    return true;
}

#endif // MUSICINFO_H
