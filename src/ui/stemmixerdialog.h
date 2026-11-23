#pragma once

#include <QDialog>
#include <QMap>
#include <QSet>
#include <QVariantMap>
#include <functional>

#include "core/musicinfo.h"

class ApiClient;
class PlayerEngine;
class StemAudioMixer;
class NekoNetworkAccessManager;
class QNetworkReply;
class QLabel;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QSlider;

/**
 * 人声 / 伴奏分离控制器与音量面板。
 *
 * 原曲继续由 PlayerEngine 解码并推进唯一播放时钟。启用后主音频输出静音，
 * StemAudioMixer 在同一条 PCM 流里混入当前人声/伴奏分片；缺片时原样输出
 * 原曲。不会创建额外播放器，也不会因分片切换重启媒体源。
 */
class StemMixerDialog final : public QDialog
{
public:
    explicit StemMixerDialog(ApiClient *apiClient, PlayerEngine *engine,
                             const MusicInfo &music, QWidget *parent = nullptr);
    ~StemMixerDialog() override;

private:
    void setupUi();
    void ensureMixer();
    void registerReadyChunk(int index, const QVariantMap &chunk);
    void loadCachedStems();
    void startStemStream();
    void stopStemStream();
    void onStemStreamEvent(const QString &event, const QVariantMap &data);
    void onStemStreamError(int statusCode, const QString &message);
    void onMixerOutputUnavailable(const QString &message);
    void onMixerChunkFailure(int index, const QString &message);
    void cacheChunk(int index, const QVariantMap &data);
    void saveChunkManifest(int index, const QVariantMap &chunk) const;
    void updateProgress();
    void updateStemControls();
    void persistGains() const;
    void setStatus(const QString &text);
    void setStemActive(bool active);
    void applyStemVolumes();
    void stopPlayback();

    QString musicCacheDir() const;
    QString chunkVocalsPath(int index) const;
    QString chunkAccompanimentPath(int index) const;
    QString chunkManifestPath(int index) const;

    using DownloadFileCb = std::function<void(bool ok, const QString &message)>;
    using DownloadPairCb = std::function<void(bool ok, const QString &message)>;
    void downloadFile(const QString &url, const QString &destination,
                      DownloadFileCb callback);
    void downloadPair(const QString &vocalsUrl, const QString &accompanimentUrl,
                      const QString &vocalsPath, const QString &accompanimentPath,
                      DownloadPairCb callback);

    ApiClient *m_apiClient = nullptr;
    PlayerEngine *m_engine = nullptr;
    MusicInfo m_music;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QSlider *m_masterSlider = nullptr;
    QLabel *m_masterValue = nullptr;
    QSlider *m_vocalsSlider = nullptr;
    QSlider *m_accompanimentSlider = nullptr;
    QLabel *m_vocalsValue = nullptr;
    QLabel *m_accompanimentValue = nullptr;
    QCheckBox *m_stemToggle = nullptr;
    QPushButton *m_resetMixButton = nullptr;
    QPushButton *m_closeButton = nullptr;

    StemAudioMixer *m_audioMixer = nullptr;
    NekoNetworkAccessManager *m_cacheNam = nullptr;
    QNetworkReply *m_streamReply = nullptr;
    QString m_jobId;
    QMap<int, QVariantMap> m_readyChunks;
    QSet<int> m_chunkDownloads;
    QSet<QString> m_seenChunkEvents;
    int m_totalChunks = 0;
    bool m_streamDone = false;
    bool m_stemActive = false;
    bool m_mixerOutputFailed = false;
};
