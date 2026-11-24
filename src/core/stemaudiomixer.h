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
class QTemporaryFile;

/**
 * 单路 PCM 人声 / 伴奏混音器。
 *
 * 原曲仍由 PlayerEngine 解码和推进时钟。只有可输出分离 PCM 时才
 * 接管音频，缺片时继续使用播放器的原声输出。
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
    /** 服务端缓存命中时可能只返回全曲双轨；解码到临时文件，按播放位置读小窗口。 */
    void loadFullTracks(const QString &vocalsPath, const QString &accompanimentPath);
    void resetOutput();
    void clearChunks();

public slots:
    /** 接收 PlayerEngine 解码出的原曲 PCM。enginePositionMs 仅在缓冲区无时间戳时兜底。 */
    void processOriginalBuffer(const QAudioBuffer &buffer, qint64 enginePositionMs);

signals:
    void chunkDecoding(int index);
    void playbackActive(int index);
    void mixOutputChanged(bool active);
    void playbackFallback();
    void fullTracksReady();
    void fullTracksFailed(const QString &message);
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
    struct FullTrack {
        std::shared_ptr<QTemporaryFile> pcm;
        int sampleRate = 0;
        qint64 frames = 0;
        qint64 firstFrame = -1;
        QVector<float> window;

        bool sample(double seconds, float &left, float &right);
    };
    struct FullTracks {
        FullTrack vocals;
        FullTrack accompaniment;
        bool ready = false;
    };

    void ensureChunkLoaded(int index);
    void finishLoadIfReady(const std::shared_ptr<ChunkLoad> &load);
    void decodeFile(const QString &path, DecodeFileCb callback);
    void decodeAudio(const QString &path, std::function<bool(const QAudioBuffer &)> consume,
                     std::function<void(bool, const QString &)> finished);
    void ensureSink(const QAudioFormat &format);
    void stopSink();
    void evictChunksAround(int index);
    const DecodedChunk *chunkAt(double timeSec) const;
    int specIndexAt(double timeSec) const;
    int nextChunkIndex(int index) const;

    QMap<int, ChunkSpec> m_specs;
    QHash<int, std::shared_ptr<DecodedChunk>> m_chunks;
    QHash<int, std::shared_ptr<ChunkLoad>> m_loads;
    QSet<int> m_failedLoads;
    std::shared_ptr<FullTracks> m_fullTracks;
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
    bool m_mixOutputActive = false;
    bool m_outputFailureReported = false;
};
