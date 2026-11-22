#pragma once

#include <QByteArray>
#include <QColor>
#include <QString>

namespace AudioQuality {

/** 对齐 SPlayer QualityType（客户端快速推断，非服务端权威） */
enum class Tier {
    Unknown = 0,
    LQ,
    MQ,
    HQ,
    SQ,
    HiRes,
    Dolby,
};

struct ProbeResult {
    Tier tier = Tier::Unknown;
    int bitrateBps = 0;
    int sampleRateHz = 0;
    int bitsPerSample = 0;
};

/** 统一为 bps（部分后端上报 kbps） */
int normalizeBitrateBps(int rawBps);
Tier tierFromBitrateBps(int bitrateBps);
/** 后端 maxQuality 字符串（standard/hq/sq/hires/dolby）转档位；未知返回 Tier::Unknown */
Tier tierFromApiQuality(const QString &apiQuality);

/** 同步读取文件头（≤64KB）推断音质，本地/缓存曲用 */
ProbeResult probeFile(const QString &filePath);

/** 根据已读文件头推断（在线 Range 或内存缓冲） */
ProbeResult probeBuffer(const QByteArray &head, const QString &suffixHint = {});

/** HTTP 提示：Content-Type + 文件大小与时长粗算码率 */
ProbeResult probeHttpHint(const QString &contentType, qint64 contentLengthBytes, int durationSec);

QString tierShortName(Tier tier);
QString tierTooltip(Tier tier, const ProbeResult &result);
/** icons/ 资源名（无 .svg），对齐 SPlayer QualityType */
QString tierIconName(Tier tier);
/** 保证有可见档位（未知时默认极高 HQ，避免标识消失） */
ProbeResult ensureVisibleTier(ProbeResult result);
/** 文件头/缓存探测结果是否足以展示，无需再发 HTTP 探测 */
bool isDefinitiveProbe(const ProbeResult &result);
/** 切歌时先显示的预估档位 */
ProbeResult guessInitialTier(bool isLocalFile, const QString &localPath);

/** 音质标识配色（与 Web 端 QualityBadge 同一套色相）；Unknown 返回无效颜色 */
QColor tierColor(Tier tier, bool dark);

} // namespace AudioQuality
