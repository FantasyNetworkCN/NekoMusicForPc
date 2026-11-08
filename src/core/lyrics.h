#pragma once

#include <QString>
#include <QVariantMap>
#include <QVector>

struct LyricWord {
    qint64 startMs = 0;
    qint64 endMs = 0;
    QString text;
};

struct LyricLine {
    qint64 time = 0;
    qint64 endTime = 0;
    QString text;
    QString translation;
    QString romanization;
    QVector<LyricWord> words;
    bool background = false;
};

namespace LyricsParser {

/** 优先解析后端 neko-v1 的 lyricsRich，缺失时回退 data.lyrics 的普通 LRC。 */
QVector<LyricLine> parsePayload(const QVariantMap &data);

QVector<LyricLine> parseLrc(const QString &lrc);

/** 当前播放位置落在哪个逐字片段；无逐字数据或尚未开始时返回 -1。 */
int wordIndexAt(const LyricLine &line, qint64 positionMs);

} // namespace LyricsParser
