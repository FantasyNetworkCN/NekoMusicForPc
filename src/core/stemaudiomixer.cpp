#include "stemaudiomixer.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioSink>
#include <QDebug>
#include <QFileInfo>
#include <QIODevice>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int kStemSampleRate = 44100;
constexpr int kMp3DelayAt44k = 1105;
constexpr qint64 kSeekResetThresholdUs = 300000;

float normalizedSample(const uchar *base, qsizetype index,
                       QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8:
        return (qreal(base[index]) - 128.0) / 127.0;
    case QAudioFormat::Int16:
        return reinterpret_cast<const qint16 *>(base)[index] / 32768.0f;
    case QAudioFormat::Int32:
        return reinterpret_cast<const qint32 *>(base)[index] / 2147483648.0f;
    case QAudioFormat::Float:
        return reinterpret_cast<const float *>(base)[index];
    default:
        return 0.0f;
    }
}

void writeNormalized(uchar *base, qsizetype index, QAudioFormat::SampleFormat format,
                     float value)
{
    value = qBound(-1.0f, value, 1.0f);
    switch (format) {
    case QAudioFormat::UInt8:
        base[index] = uchar(qRound(value * 127.0f) + 128);
        break;
    case QAudioFormat::Int16:
        reinterpret_cast<qint16 *>(base)[index] =
            qint16(qBound(-32768, qRound(value * 32767.0f), 32767));
        break;
    case QAudioFormat::Int32:
        reinterpret_cast<qint32 *>(base)[index] =
            qint32(qBound<qint64>(-2147483648LL,
                                  qRound64(qreal(value) * 2147483647.0),
                                  2147483647LL));
        break;
    case QAudioFormat::Float:
        reinterpret_cast<float *>(base)[index] = value;
        break;
    default:
        break;
    }
}

void appendStereo(const QAudioBuffer &buffer, QVector<float> &output, int *sampleRate)
{
    const QAudioFormat format = buffer.format();
    const int channels = format.channelCount();
    const int rate = format.sampleRate();
    if (!buffer.isValid() || channels <= 0 || rate <= 0
        || format.sampleFormat() == QAudioFormat::Unknown) {
        return;
    }
    if (*sampleRate <= 0)
        *sampleRate = rate;
    if (*sampleRate != rate)
        return;

    const qsizetype frames = buffer.frameCount();
    const qsizetype bytesPerFrame = format.bytesPerFrame();
    const uchar *source = buffer.constData<uchar>();
    const qsizetype oldSize = output.size();
    output.resize(oldSize + frames * 2);
    for (qsizetype frame = 0; frame < frames; ++frame) {
        const uchar *frameData = source + frame * bytesPerFrame;
        const float left = normalizedSample(
            frameData, 0, format.sampleFormat());
        const float right = channels > 1
                ? normalizedSample(frameData, 1, format.sampleFormat())
                : left;
        output[oldSize + frame * 2] = left;
        output[oldSize + frame * 2 + 1] = right;
    }
}

QVector<float> resampleTrack(const QVector<float> &input, int sourceRate,
                             int startTrimFrames, int sourceFrames,
                             int targetRate, int targetFrames)
{
    if (sourceFrames <= 1 || targetFrames <= 0 || input.size() < 2)
        return {};
    const int decodedFrames = input.size() / 2;
    startTrimFrames = qBound(0, startTrimFrames, qMax(0, decodedFrames - 1));
    sourceFrames = qMin(sourceFrames, decodedFrames - startTrimFrames);
    if (sourceFrames <= 1)
        return {};
    targetFrames = qMax(1, qRound(qreal(sourceFrames) * targetRate / sourceRate));

    QVector<float> output(targetFrames * 2);
    const qreal step = qreal(sourceFrames) / targetFrames;
    for (int frame = 0; frame < targetFrames; ++frame) {
        const qreal position = frame * step;
        const int first = qMin(sourceFrames - 1, int(position));
        const int second = qMin(sourceFrames - 1, first + 1);
        const qreal fraction = position - first;
        for (int channel = 0; channel < 2; ++channel) {
            const float a = input[(startTrimFrames + first) * 2 + channel];
            const float b = input[(startTrimFrames + second) * 2 + channel];
            output[frame * 2 + channel] = float(a + (b - a) * fraction);
        }
    }
    return output;
}

} // namespace

StemAudioMixer::StemAudioMixer(QObject *parent)
    : QObject(parent)
{
}

StemAudioMixer::~StemAudioMixer()
{
    stopSink();
}

void StemAudioMixer::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_lastEndUs = -1;
    m_lastActiveIndex = -1;
    m_outputFailureReported = false;
    if (enabled)
        m_failedLoads.clear();
    else
        stopSink();
}

void StemAudioMixer::setOutputDevice(const QAudioDevice &device)
{
    if (m_device == device)
        return;
    m_device = device;
    if (m_enabled)
        stopSink();
}

void StemAudioMixer::setGains(qreal master, qreal vocals, qreal accompaniment)
{
    m_master = qBound(0.0, master, 1.0);
    m_vocals = qBound(0.0, vocals, 1.0);
    m_accompaniment = qBound(0.0, accompaniment, 1.0);
    if (m_sink)
        m_sink->setVolume(m_master);
}

void StemAudioMixer::addChunk(int index, double startSec, double endSec,
                              const QString &vocalsPath,
                              const QString &accompanimentPath)
{
    if (index < 0 || endSec <= startSec
        || !QFileInfo(vocalsPath).isFile()
        || !QFileInfo(accompanimentPath).isFile()) {
        return;
    }
    ChunkSpec spec;
    spec.index = index;
    spec.startSec = startSec;
    spec.endSec = endSec;
    spec.vocalsPath = vocalsPath;
    spec.accompanimentPath = accompanimentPath;
    const ChunkSpec previous = m_specs.value(index);
    m_specs.insert(index, spec);
    if (previous.vocalsPath != vocalsPath
        || previous.accompanimentPath != accompanimentPath) {
        m_chunks.remove(index);
        m_loads.remove(index);
        m_failedLoads.remove(index);
    }

    const qint64 nowUs = m_lastEndUs >= 0
            ? m_lastEndUs
            : qint64(startSec * 1000000.0);
    if (m_enabled
        && nowUs >= qint64(startSec * 1000000.0) - 2000000LL
        && nowUs < qint64(endSec * 1000000.0) + 30000000LL) {
        ensureChunkLoaded(index);
    }
}

void StemAudioMixer::clearChunks()
{
    m_chunks.clear();
    m_loads.clear();
    m_failedLoads.clear();
    m_specs.clear();
    m_lastActiveIndex = -1;
}

void StemAudioMixer::ensureChunkLoaded(int index)
{
    if (index < 0 || m_chunks.contains(index) || m_loads.contains(index)
        || m_failedLoads.contains(index)
        || !m_specs.contains(index)) {
        return;
    }
    const ChunkSpec spec = m_specs.value(index);
    if (!QFileInfo(spec.vocalsPath).isFile()
        || !QFileInfo(spec.accompanimentPath).isFile()) {
        return;
    }

    auto load = std::make_shared<ChunkLoad>();
    load->index = index;
    m_loads.insert(index, load);
    emit chunkDecoding(index);

    auto finishOne = [this, load](bool vocals, bool ok,
                                  const QVector<float> &samples,
                                  int sampleRate, const QString &message) {
        if (!m_loads.contains(load->index)
            || m_loads.value(load->index) != load) {
            return;
        }
        if (!ok) {
            load->failed = true;
            if (load->error.isEmpty())
                load->error = message;
        } else if (vocals) {
            load->vocals = samples;
            load->vocalsRate = sampleRate;
            load->vocalsDone = true;
        } else {
            load->accompaniment = samples;
            load->accompanimentRate = sampleRate;
            load->accompanimentDone = true;
        }
        finishLoadIfReady(load);
    };

    decodeFile(spec.vocalsPath,
               [finishOne](bool ok, const QVector<float> &samples, int sampleRate,
                           const QString &message) {
                   finishOne(true, ok, samples, sampleRate, message);
               });
    decodeFile(spec.accompanimentPath,
               [finishOne](bool ok, const QVector<float> &samples, int sampleRate,
                           const QString &message) {
                   finishOne(false, ok, samples, sampleRate, message);
               });
}

void StemAudioMixer::finishLoadIfReady(const std::shared_ptr<ChunkLoad> &load)
{
    if (!load->failed && (!load->vocalsDone || !load->accompanimentDone))
        return;
    m_loads.remove(load->index);
    const ChunkSpec spec = m_specs.value(load->index);
    if (load->failed || spec.index < 0 || load->vocalsRate <= 0
        || load->accompanimentRate <= 0) {
        m_failedLoads.insert(load->index);
        emit chunkDecodeFailed(load->index,
                               load->error.isEmpty()
                                   ? QStringLiteral("分离分片解码失败")
                                   : load->error);
        return;
    }

    auto finalize = [&spec](const QVector<float> &samples, int sourceRate) {
        const int decodedFrames = samples.size() / 2;
        const int expectedSourceFrames = qMax(
            1, qRound((spec.endSec - spec.startSec) * sourceRate));
        const int extraFrames = qMax(0, decodedFrames - expectedSourceFrames);
        const int delayFrames = qRound(kMp3DelayAt44k * qreal(sourceRate)
                                       / kStemSampleRate);
        const int startTrim = qMin(delayFrames, extraFrames);
        const int targetFrames = qMax(
            1, qRound((spec.endSec - spec.startSec) * kStemSampleRate));
        return resampleTrack(samples, sourceRate, startTrim,
                             expectedSourceFrames, kStemSampleRate, targetFrames);
    };

    auto decoded = std::make_shared<DecodedChunk>();
    decoded->index = spec.index;
    decoded->startSec = spec.startSec;
    decoded->endSec = spec.endSec;
    decoded->sampleRate = kStemSampleRate;
    decoded->vocals = finalize(load->vocals, load->vocalsRate);
    decoded->accompaniment =
        finalize(load->accompaniment, load->accompanimentRate);
    if (decoded->frames() <= 1) {
        emit chunkDecodeFailed(spec.index, QStringLiteral("分离分片采样为空"));
        return;
    }
    m_chunks.insert(spec.index, decoded);
}

void StemAudioMixer::decodeFile(const QString &path, DecodeFileCb callback)
{
    auto *decoder = new QAudioDecoder(this);
    QAudioFormat requested;
    requested.setSampleRate(kStemSampleRate);
    requested.setChannelCount(2);
    requested.setSampleFormat(QAudioFormat::Float);
    decoder->setAudioFormat(requested);

    auto samples = std::make_shared<QVector<float>>();
    auto sampleRate = std::make_shared<int>(0);
    auto completed = std::make_shared<bool>(false);
    auto complete = [decoder, callback, completed, samples, sampleRate]
            (bool ok, const QString &message) {
        if (*completed)
            return;
        *completed = true;
        decoder->deleteLater();
        callback(ok, *samples, *sampleRate,
                 message.isEmpty() && !ok
                     ? QStringLiteral("分离音频解码失败")
                     : message);
    };

    connect(decoder, &QAudioDecoder::bufferReady, this,
            [decoder, samples, sampleRate]() {
                while (decoder->bufferAvailable()) {
                    const QAudioBuffer buffer = decoder->read();
                    if (buffer.isValid())
                        appendStereo(buffer, *samples, sampleRate.get());
                }
            });
    connect(decoder, &QAudioDecoder::finished, this,
            [decoder, samples, complete]() {
                if (decoder->error() != QAudioDecoder::NoError || samples->isEmpty()) {
                    complete(false, decoder->errorString());
                    return;
                }
                complete(true, {});
            });
    connect(decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error),
            this, [decoder, complete](QAudioDecoder::Error) {
                complete(false, decoder->errorString());
            });
    decoder->setSource(QUrl::fromLocalFile(path));
    decoder->start();
}

void StemAudioMixer::processOriginalBuffer(const QAudioBuffer &buffer,
                                           qint64 enginePositionMs)
{
    if (!m_enabled || !buffer.isValid())
        return;

    const QAudioFormat format = buffer.format();
    const int channels = format.channelCount();
    if (!format.isValid() || channels < 1 || channels > 2
        || format.sampleFormat() == QAudioFormat::Unknown) {
        if (!m_outputFailureReported) {
            m_outputFailureReported = true;
            emit outputUnavailable(QStringLiteral("当前原曲 PCM 格式不支持人声分离混音"));
        }
        return;
    }

    ensureSink(format);
    if (!m_sinkDevice)
        return;

    qint64 startUs = buffer.startTime();
    if (startUs < 0)
        startUs = enginePositionMs >= 0 ? enginePositionMs * 1000 : m_lastEndUs;
    if (startUs < 0)
        startUs = 0;

    if (m_lastEndUs >= 0 && qAbs(startUs - m_lastEndUs) > kSeekResetThresholdUs)
        restartSink(format);
    if (!m_sinkDevice)
        return;

    const qsizetype frames = buffer.frameCount();
    const qsizetype frameBytes = format.bytesPerFrame();
    QByteArray output(buffer.byteCount(), Qt::Uninitialized);
    uchar *destination = reinterpret_cast<uchar *>(output.data());
    const uchar *source = buffer.constData<uchar>();
    const int rate = format.sampleRate();

    const double startSec = startUs / 1000000.0;
    int currentSpec = specIndexAt(startSec);
    if (currentSpec >= 0) {
        ensureChunkLoaded(currentSpec);
        const int next = nextChunkIndex(currentSpec);
        if (next >= 0)
            ensureChunkLoaded(next);
    }

    const DecodedChunk *chunk = chunkAt(startSec);
    if (chunk && chunk->index != m_lastActiveIndex) {
        m_lastActiveIndex = chunk->index;
        evictChunksAround(chunk->index);
        emit playbackActive(chunk->index);
    }
    int usedIndex = -1;
    for (qsizetype frame = 0; frame < frames; ++frame) {
        const double timeSec = startSec + qreal(frame) / rate;
        if (!chunk || timeSec < chunk->startSec || timeSec >= chunk->endSec) {
            chunk = chunkAt(timeSec);
            if (chunk && chunk->index != m_lastActiveIndex) {
                m_lastActiveIndex = chunk->index;
                evictChunksAround(chunk->index);
                emit playbackActive(chunk->index);
            }
        }

        const uchar *sourceFrame = source + frame * frameBytes;
        const float originalLeft =
            normalizedSample(sourceFrame, 0, format.sampleFormat());
        const float originalRight = channels > 1
                ? normalizedSample(sourceFrame, 1, format.sampleFormat())
                : originalLeft;

        float mixedLeft = originalLeft;
        float mixedRight = originalRight;
        if (chunk) {
            const double position = (timeSec - chunk->startSec) * chunk->sampleRate;
            const int first = int(position);
            if (first >= 0 && first + 1 < chunk->frames()) {
                const float fraction = float(position - first);
                const auto sample = [chunk, first, fraction](bool vocals, int channel) {
                    const QVector<float> &track =
                        vocals ? chunk->vocals : chunk->accompaniment;
                    const float a = track[first * 2 + channel];
                    const float b = track[(first + 1) * 2 + channel];
                    return a + (b - a) * fraction;
                };
                const float vocalLeft = sample(true, 0);
                const float vocalRight = sample(true, 1);
                const float accompanimentLeft = sample(false, 0);
                const float accompanimentRight = sample(false, 1);
                mixedLeft = vocalLeft * m_vocals
                        + accompanimentLeft * m_accompaniment;
                mixedRight = vocalRight * m_vocals
                        + accompanimentRight * m_accompaniment;
                usedIndex = chunk->index;
            }
        }

        if (channels == 1) {
            writeNormalized(destination + frame * frameBytes, 0,
                            format.sampleFormat(),
                            (mixedLeft + mixedRight) * 0.5f);
        } else {
            writeNormalized(destination + frame * frameBytes, 0,
                            format.sampleFormat(), mixedLeft);
            writeNormalized(destination + frame * frameBytes, 1,
                            format.sampleFormat(), mixedRight);
        }
    }

    if (usedIndex < 0 && m_lastActiveIndex >= 0) {
        m_lastActiveIndex = -1;
        emit playbackFallback();
    }
    m_lastEndUs = startUs + qint64(qreal(frames) * 1000000.0 / rate);
    const qint64 expectedBytes = output.size();
    qint64 written = 0;
    while (written < expectedBytes && m_sinkDevice) {
        const qint64 n = m_sinkDevice->write(output.constData() + written,
                                             expectedBytes - written);
        if (n <= 0)
            break;
        written += n;
    }
}

void StemAudioMixer::ensureSink(const QAudioFormat &format)
{
    if (m_sink && m_sinkDevice && m_sinkFormat == format)
        return;
    stopSink();
    m_sinkFormat = format;
    m_sink = m_device.isNull()
            ? new QAudioSink(format, this)
            : new QAudioSink(m_device, format, this);
    m_sink->setBufferFrameCount(qMax(1024, format.sampleRate() / 10));
    m_sink->setVolume(m_master);
    m_sinkDevice = m_sink->start();
    if (!m_sinkDevice || m_sink->error() != QtAudio::NoError) {
        const QString error = QStringLiteral("无法启动分离混音输出（错误码 %1）")
                                  .arg(int(m_sink->error()));
        stopSink();
        if (!m_outputFailureReported) {
            m_outputFailureReported = true;
            emit outputUnavailable(error);
        }
        return;
    }
    m_outputFailureReported = false;
}

void StemAudioMixer::restartSink(const QAudioFormat &format)
{
    stopSink();
    ensureSink(format);
    m_lastEndUs = -1;
}

void StemAudioMixer::stopSink()
{
    m_sinkDevice = nullptr;
    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    m_sinkFormat = QAudioFormat();
}

void StemAudioMixer::evictChunksAround(int index)
{
    const QList<int> keys = m_chunks.keys();
    for (int key : keys) {
        if (key < index - 1 || key > index + 2)
            m_chunks.remove(key);
    }
}

const StemAudioMixer::DecodedChunk *StemAudioMixer::chunkAt(double timeSec) const
{
    for (auto it = m_chunks.cbegin(); it != m_chunks.cend(); ++it) {
        const auto &chunk = it.value();
        if (timeSec >= chunk->startSec && timeSec < chunk->endSec)
            return chunk.get();
    }
    return nullptr;
}

int StemAudioMixer::specIndexAt(double timeSec) const
{
    for (auto it = m_specs.cbegin(); it != m_specs.cend(); ++it) {
        if (timeSec >= it.value().startSec && timeSec < it.value().endSec)
            return it.key();
    }
    return -1;
}

int StemAudioMixer::nextChunkIndex(int index) const
{
    const auto current = m_specs.constFind(index);
    if (current == m_specs.cend())
        return -1;
    int next = -1;
    double nextStart = std::numeric_limits<double>::max();
    for (auto it = m_specs.cbegin(); it != m_specs.cend(); ++it) {
        if (it.value().startSec >= current.value().endSec
            && it.value().startSec < nextStart) {
            next = it.key();
            nextStart = it.value().startSec;
        }
    }
    return next;
}
