#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QHash>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVector>

#include <functional>
#include <memory>

class QAudioBuffer;
class QAudioDecoder;
class QAudioSink;
class QIODevice;

/**
 * 单路 PCM 人声 / 伴奏混音器。
 *
 * 原曲仍由 PlayerEngine 解码和推进时钟。启用后主音频输出静音，
 * 本类接管同一条 PCM 流：有当前分片时直接混入人声与伴奏，缺片时原样输出
 * 原曲。不会创建第二、第三个播放器，也不会重新打开媒体源。
 */
class StemAudioMixer final : public QObject
{
    Q_OBJECT
public:
    explicit StemAudioMixer(QObject *parent = nullptr);
    ~StemAudioMixer() override;

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    void setOutputDevice(const QAudioDevice &device);
    void setGains(qreal master, qreal vocals, qreal accompaniment);

    /** 登记一段可混音缓存；真正解码在当前播放位置附近按需进行。 */
    void addChunk(int index, double startSec, double endSec,
                  const QString &vocalsPath, const QString &accompanimentPath);
    void clearChunks();

public slots:
    /** 接收 PlayerEngine 解码出的原曲 PCM。enginePositionMs 仅在缓冲区无时间戳时兜底。 */
    void processOriginalBuffer(const QAudioBuffer &buffer, qint64 enginePositionMs);

signals:
    void chunkDecoding(int index);
    void playbackActive(int index);
    void playbackFallback();
    void chunkDecodeFailed(int index, const QString &message);
    void outputUnavailable(const QString &message);

private:
    struct ChunkSpec {
        int index = -1;
        double startSec = 0.0;
        double endSec = 0.0;
        QString vocalsPath;
        QString accompanimentPath;
    };

    struct DecodedChunk {
        int index = -1;
        double startSec = 0.0;
        double endSec = 0.0;
        int sampleRate = 44100;
        QVector<float> vocals;
        QVector<float> accompaniment;

        int frames() const { return qMin(vocals.size(), accompaniment.size()) / 2; }
    };

    struct ChunkLoad {
        int index = -1;
        QVector<float> vocals;
        QVector<float> accompaniment;
        int vocalsRate = 0;
        int accompanimentRate = 0;
        bool vocalsDone = false;
        bool accompanimentDone = false;
        bool failed = false;
        QString error;
    };

    using DecodeFileCb =
        std::function<void(bool ok, const QVector<float> &samples, int sampleRate,
                           const QString &message)>;

    void ensureChunkLoaded(int index);
    void finishLoadIfReady(const std::shared_ptr<ChunkLoad> &load);
    void decodeFile(const QString &path, DecodeFileCb callback);
    void ensureSink(const QAudioFormat &format);
    void restartSink(const QAudioFormat &format);
    void stopSink();
    void evictChunksAround(int index);
    const DecodedChunk *chunkAt(double timeSec) const;
    int specIndexAt(double timeSec) const;
    int nextChunkIndex(int index) const;

    QMap<int, ChunkSpec> m_specs;
    QHash<int, std::shared_ptr<DecodedChunk>> m_chunks;
    QHash<int, std::shared_ptr<ChunkLoad>> m_loads;
    QSet<int> m_failedLoads;
    QAudioDevice m_device;
    QAudioSink *m_sink = nullptr;
    QIODevice *m_sinkDevice = nullptr;
    QAudioFormat m_sinkFormat;
    qreal m_master = 1.0;
    qreal m_vocals = 0.8;
    qreal m_accompaniment = 0.8;
    qint64 m_lastEndUs = -1;
    int m_lastActiveIndex = -1;
    bool m_enabled = false;
    bool m_outputFailureReported = false;
};
