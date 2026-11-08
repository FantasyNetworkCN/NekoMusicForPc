#include "lyrics.h"

#include <QRegularExpression>

#include <algorithm>

namespace {

int subsecondToMs(const QString &frac)
{
    if (frac.isEmpty())
        return 0;
    bool ok = false;
    const int value = frac.toInt(&ok);
    if (!ok || value < 0)
        return 0;
    const int length = frac.length();
    if (length < 1 || length > 5)
        return 0;
    static const qint64 powers[] = {1, 10, 100, 1000, 10000, 100000};
    const qint64 denominator = powers[length];
    return static_cast<int>((qint64{value} * 1000 + denominator / 2) / denominator);
}

QVector<LyricLine> parseRich(const QVariantMap &rich)
{
    QVector<LyricLine> out;
    const QVariantList rows = rich.value(QStringLiteral("lines")).toList();
    out.reserve(rows.size());
    for (const QVariant &rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        if (row.isEmpty())
            continue;

        LyricLine line;
        line.time = row.value(QStringLiteral("startMs")).toLongLong();
        line.endTime = row.value(QStringLiteral("endMs")).toLongLong();
        line.text = row.value(QStringLiteral("text")).toString();
        line.translation = row.value(QStringLiteral("translation")).toString();
        line.romanization = row.value(QStringLiteral("romanization")).toString();
        line.background = row.value(QStringLiteral("background")).toBool();

        const QVariantList wordRows = row.value(QStringLiteral("words")).toList();
        line.words.reserve(wordRows.size());
        for (const QVariant &wordValue : wordRows) {
            const QVariantMap wordMap = wordValue.toMap();
            LyricWord word;
            word.startMs = wordMap.value(QStringLiteral("startMs")).toLongLong();
            word.endMs = wordMap.value(QStringLiteral("endMs")).toLongLong();
            word.text = wordMap.value(QStringLiteral("text")).toString();
            if (!word.text.isEmpty())
                line.words.append(word);
        }
        if (!line.text.isEmpty() || !line.words.isEmpty())
            out.append(line);
    }
    std::sort(out.begin(), out.end(),
              [](const LyricLine &a, const LyricLine &b) { return a.time < b.time; });
    return out;
}

} // namespace

namespace LyricsParser {

QVector<LyricLine> parsePayload(const QVariantMap &data)
{
    const QVariantMap rich = data.value(QStringLiteral("lyricsRich")).toMap();
    if (!rich.isEmpty()) {
        QVector<LyricLine> parsed = parseRich(rich);
        if (!parsed.isEmpty())
            return parsed;
    }
    return parseLrc(data.value(QStringLiteral("lyrics")).toString());
}

QVector<LyricLine> parseLrc(const QString &lrc)
{
    QVector<LyricLine> out;
    // 支持 [m:s]、小数 1～5 位（与后端一致）、一行多时间轴；小数按 F/10^L 秒四舍五入为毫秒
    static const QRegularExpression timeRe(R"(\[(\d+):(\d{1,2})(?:\.(\d{1,5}))?\])");
    static const QRegularExpression transRe(R"(^\{["'](.+)["']\}$)");

    const QStringList lines = lrc.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty())
            continue;

        QRegularExpressionMatchIterator tagIt = timeRe.globalMatch(line);
        if (!tagIt.hasNext())
            continue;

        QString plain = line;
        plain.remove(timeRe);
        plain = plain.trimmed();

        QString translation;
        if (i + 1 < lines.size()) {
            const auto match = transRe.match(lines[i + 1].trimmed());
            if (match.hasMatch())
                translation = match.captured(1);
        }

        tagIt = timeRe.globalMatch(line);
        while (tagIt.hasNext()) {
            const auto match = tagIt.next();
            const qint64 minutes = match.captured(1).toLongLong();
            const qint64 seconds = match.captured(2).toLongLong();
            const int milliseconds = subsecondToMs(match.captured(3));
            LyricLine parsed;
            parsed.time = (minutes * 60 + seconds) * 1000 + milliseconds;
            parsed.text = plain;
            parsed.translation = translation;
            out.append(parsed);
        }
    }

    std::sort(out.begin(), out.end(),
              [](const LyricLine &a, const LyricLine &b) { return a.time < b.time; });
    return out;
}

int wordIndexAt(const LyricLine &line, qint64 positionMs)
{
    int index = -1;
    for (int i = 0; i < line.words.size(); ++i) {
        if (positionMs < line.words.at(i).startMs)
            break;
        index = i;
    }
    return index;
}

} // namespace LyricsParser
