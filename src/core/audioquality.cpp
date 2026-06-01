#include "core/audioquality.h"

#include <QFile>
#include <QFileInfo>

namespace AudioQuality {

namespace {

quint32 readBe32(const uchar *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint32 readLe32(const uchar *p)
{
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint16 readLe16(const uchar *p)
{
    return quint16(p[0]) | (quint16(p[1]) << 8);
}

Tier tierFromFlac(int sampleRateHz, int bitsPerSample)
{
    if (sampleRateHz >= 96000 || bitsPerSample >= 24)
        return Tier::HiRes;
    return Tier::SQ;
}

Tier tierFromWav(int sampleRateHz, int bitsPerSample, int byteRate)
{
    if (byteRate > 0) {
        const Tier t = tierFromBitrateBps(byteRate * 8);
        if (t != Tier::Unknown)
            return t;
    }
    if (sampleRateHz >= 96000 || bitsPerSample >= 24)
        return Tier::HiRes;
    if (bitsPerSample >= 16)
        return Tier::SQ;
    return Tier::HQ;
}

bool parseFlac(const QByteArray &data, ProbeResult &out)
{
    if (data.size() < 42 || !data.startsWith("fLaC"))
        return false;
    const uchar *base = reinterpret_cast<const uchar *>(data.constData());
    // 元数据块头 4 字节 big-endian：高字节 bit7=最后一块，低 7 位=块类型；低 3 字节=数据长度
    const quint32 blockHeader = readBe32(base + 4);
    const int blockType = int((blockHeader >> 24) & 0x7Fu);
    const int blockSize = int(blockHeader & 0x00FFFFFFu);
    if (blockType != 0 || blockSize < 18 || 8 + blockSize > static_cast<int>(data.size()))
        return false;
    const uchar *si = base + 8;
    const int sampleRate = int((si[10] << 12) | (si[11] << 4) | (si[12] >> 4));
    const int bits = int((((si[12] & 0x01) << 4) | ((si[13] & 0xF0) >> 4)) + 1);
    if (sampleRate <= 0)
        return false;
    out.sampleRateHz = sampleRate;
    out.bitsPerSample = bits;
    out.tier = tierFromFlac(sampleRate, bits);
    out.bitrateBps = sampleRate * qMax(16, bits) * 2;
    return true;
}

bool parseWav(const QByteArray &data, ProbeResult &out)
{
    if (data.size() < 44 || !data.startsWith("RIFF") || data.mid(8, 4) != "WAVE")
        return false;
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
  int off = 12;
    while (off + 8 <= data.size()) {
        const QByteArray id = data.mid(off, 4);
        const quint32 chunkSize = readLe32(p + off + 4);
        off += 8;
        if (off + int(chunkSize) > data.size())
            break;
        if (id == "fmt " && chunkSize >= 16) {
            const int byteRate = int(readLe32(p + off + 8));
            const int sampleRate = int(readLe32(p + off + 4));
            const int bits = int(readLe16(p + off + 14));
            out.sampleRateHz = sampleRate;
            out.bitsPerSample = bits;
            if (byteRate > 0)
                out.bitrateBps = byteRate * 8;
            out.tier = tierFromWav(sampleRate, bits, byteRate);
            return true;
        }
        off += int(chunkSize);
        if (chunkSize % 2)
            ++off;
    }
    return false;
}

int mp3BitrateKbpsLayer3(const uchar b2, const uchar b3)
{
    static const int kTable[] = {0,  32,  40,  48,  56,  64,  80,  96,
                                 112, 128, 160, 192, 224, 256, 320, 0};
    const int bitrateIndex = (b2 >> 4) & 0x0F;
    const int samplingIndex = (b3 >> 2) & 0x03;
    if (bitrateIndex == 0 || bitrateIndex == 0x0F || samplingIndex == 3)
        return 0;
    return kTable[bitrateIndex];
}

bool parseMp3(const QByteArray &data, ProbeResult &out)
{
    const int n = qMin(data.size(), 256 * 1024);
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    for (int i = 0; i + 4 < n; ++i) {
        if (p[i] != 0xFF || (p[i + 1] & 0xE0) != 0xE0)
            continue;
        const int kbps = mp3BitrateKbpsLayer3(p[i + 2], p[i + 3]);
        if (kbps <= 0)
            continue;
        out.bitrateBps = kbps * 1000;
        out.tier = tierFromBitrateBps(out.bitrateBps);
        return true;
    }
    return false;
}

ProbeResult probeSuffixFallback(const QString &suffix)
{
    ProbeResult r;
    const QString s = suffix.toLower();
    if (s == QLatin1String("flac"))
        r.tier = Tier::SQ;
    else if (s == QLatin1String("wav"))
        r.tier = Tier::SQ;
    else if (s == QLatin1String("mp3") || s == QLatin1String("m4a") || s == QLatin1String("aac"))
        r.tier = Tier::HQ;
    else if (s == QLatin1String("ogg") || s == QLatin1String("oga") || s == QLatin1String("opus"))
        r.tier = Tier::MQ;
    else if (s == QLatin1String("wma"))
        r.tier = Tier::MQ;
    else
        r.tier = Tier::HQ;
    return r;
}

QByteArray audioPayloadAfterLeadingTags(const QByteArray &data)
{
    if (data.size() >= 10 && data.startsWith("ID3")) {
        const int tagSize = ((uchar(data[6]) & 0x7f) << 21) | ((uchar(data[7]) & 0x7f) << 14)
                            | ((uchar(data[8]) & 0x7f) << 7) | (uchar(data[9]) & 0x7f);
        const int off = 10 + tagSize;
        if (off > 0 && off < data.size())
            return data.mid(off);
    }
    return data;
}

} // namespace

int normalizeBitrateBps(int rawBps)
{
    if (rawBps <= 0)
        return 0;
    // 已是 bps（常见 128000、1411200）
    if (rawBps >= 10000)
        return rawBps;
    // Qt/标签里的 kbps（64–4000）；勿把 960 等误当成 960kbps→Hi-Res
    if (rawBps >= 64 && rawBps <= 4000)
        return rawBps * 1000;
    return rawBps;
}

Tier tierFromBitrateBps(int bitrateBps)
{
    bitrateBps = normalizeBitrateBps(bitrateBps);
    if (bitrateBps <= 0)
        return Tier::Unknown;
    // 仅极高码率标 Hi-Res（96kHz/24bit FLAC 通常 >2Mbps）；避免 960*1000 误判
    if (bitrateBps >= 2000000)
        return Tier::HiRes;
    if (bitrateBps >= 441000)
        return Tier::SQ;
    if (bitrateBps >= 320000)
        return Tier::HQ;
    if (bitrateBps >= 160000)
        return Tier::MQ;
    return Tier::LQ;
}

ProbeResult probeBuffer(const QByteArray &head, const QString &suffixHint)
{
    ProbeResult out;
    if (head.isEmpty()) {
        if (!suffixHint.isEmpty())
            return probeSuffixFallback(QFileInfo(suffixHint).suffix());
        return out;
    }

    const QByteArray payload = audioPayloadAfterLeadingTags(head);
    if (parseFlac(payload, out) || parseWav(payload, out) || parseMp3(payload, out))
        return ensureVisibleTier(out);

    const QString suf = suffixHint.isEmpty() ? QString() : QFileInfo(suffixHint).suffix();
    return ensureVisibleTier(probeSuffixFallback(suf));
}

ProbeResult probeFile(const QString &filePath)
{
    ProbeResult out;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return out;

    constexpr int kMax = 64 * 1024;
    const QByteArray head = f.read(kMax);
    out = probeBuffer(head, filePath);
    if (out.tier != Tier::Unknown)
        return ensureVisibleTier(out);
    return ensureVisibleTier(probeSuffixFallback(QFileInfo(filePath).suffix()));
}

ProbeResult probeHttpHint(const QString &contentType, qint64 contentLengthBytes, int durationSec)
{
    ProbeResult out;
    const QString ct = contentType.toLower();

    if (contentLengthBytes > 0 && durationSec > 0) {
        const qint64 bps = (contentLengthBytes * 8LL) / qint64(durationSec);
        if (bps > 0 && bps < 50'000'000) {
            out.bitrateBps = int(bps);
            out.tier = tierFromBitrateBps(out.bitrateBps);
            return ensureVisibleTier(out);
        }
    }

  // 无 Content-Length 时勿把 flac/wav 标成 SQ，留给 Range 读头判断 Hi-Res
    if (ct.contains(QLatin1String("flac")) || ct.contains(QLatin1String("wav")))
        return out;

    if (ct.contains(QLatin1String("mpeg")) || ct.contains(QLatin1String("mp3")))
        out.tier = Tier::HQ;
    else if (ct.contains(QLatin1String("aac")) || ct.contains(QLatin1String("mp4")))
        out.tier = Tier::MQ;
    return ensureVisibleTier(out);
}

QString tierIconName(Tier tier)
{
    switch (tier) {
    case Tier::HiRes:
        return QStringLiteral("HiRes");
    case Tier::SQ:
        return QStringLiteral("SQ");
    case Tier::HQ:
        return QStringLiteral("HQ");
    case Tier::MQ:
        return QStringLiteral("MQ");
    case Tier::LQ:
        return QStringLiteral("LQ");
    case Tier::Unknown:
    default:
        return QStringLiteral("HQ");
    }
}

ProbeResult ensureVisibleTier(ProbeResult result)
{
    if (result.tier == Tier::Unknown)
        result.tier = Tier::HQ;
    return result;
}

bool isDefinitiveProbe(const ProbeResult &result)
{
    if (result.tier == Tier::Unknown)
        return false;
    if (result.sampleRateHz > 0 || result.bitsPerSample > 0 || result.bitrateBps > 0)
        return true;
    // 只有后缀/兜底猜到的档位通常没有任何数值信息（rate/bits/bitrate 都是 0）。
    // 这类“猜测 HQ/MQ/LQ”不算确定档位，必须让后续 HTTP Range/缓存探测去升级。
    return result.tier >= Tier::SQ;
}

ProbeResult guessInitialTier(bool isLocalFile, const QString &localPath)
{
    if (isLocalFile && !localPath.isEmpty())
        return probeFile(localPath);
    return {};
}

QString tierShortName(Tier tier)
{
    switch (tier) {
    case Tier::LQ:
        return QStringLiteral("标准");
    case Tier::MQ:
        return QStringLiteral("高清");
    case Tier::HQ:
        return QStringLiteral("极高");
    case Tier::SQ:
        return QStringLiteral("无损");
    case Tier::HiRes:
        return QStringLiteral("Hi-Res");
    default:
        return {};
    }
}

QString tierTooltip(Tier tier, const ProbeResult &result)
{
    const QString name = tierShortName(tier);
    if (name.isEmpty())
        return {};
    QString tip = name;
    if (result.bitrateBps > 0)
        tip += QStringLiteral(" · %1 kbps").arg(result.bitrateBps / 1000);
    if (result.sampleRateHz > 0)
        tip += QStringLiteral(" · %1 kHz").arg(result.sampleRateHz / 1000.0, 0, 'f', 1);
    if (result.bitsPerSample > 0)
        tip += QStringLiteral(" · %1 bit").arg(result.bitsPerSample);
    return tip;
}

} // namespace AudioQuality
