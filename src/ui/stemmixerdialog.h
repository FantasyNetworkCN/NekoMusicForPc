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
class QTimer;

/**
 * 人声 / 伴奏分离控制器与音量面板。
 *
 * 原曲继续由 PlayerEngine 解码并推进唯一播放时钟。分轨可以输出时才静音
 * 原声；等待、跳转至缺片位置或输出失败时保持原曲可听。
 */
class StemMixerDialog final : public QDialog
{
public:
    explicit StemMixerDialog(ApiClient *apiClient, PlayerEngine *engine, QWidget *parent = nullptr);
    ~StemMixerDialog() override;

    static bool enabledPreference();

private:
    void setupUi();
    void setTrack(const MusicInfo &music);
    bool trackSupportsStems() const;
    void ensureMixer();
    void registerReadyChunk(int index, const QVariantMap &chunk);
    void loadCachedStems();
    void startStemStream();
    void stopStemStream();
    void cancelStemRequests();
    void onStemStreamEvent(const QString &event, const QVariantMap &data);
    void onStemStreamError(int statusCode, const QString &message);
    void onMixerOutputUnavailable(const QString &message);
    void onMixerChunkFailure(int index, const QString &message);
    void finishStemPreparation();
    void prepareFullTracks();
    bool allChunksReady() const;
    void cacheChunk(int index, const QVariantMap &data);
    void saveChunkManifest(int index, const QVariantMap &chunk) const;
    void updateProgress();
    void updateStemControls();
    void refreshStemAccess();
    void syncStemState();
    void persistGains();
    void setStatus(const QString &text);
    void setStemActive(bool active);
    void applyStemVolumes();
    void stopPlayback();

    QString musicCacheDir() const;
    QString chunkVocalsPath(int index) const;
    QString chunkAccompanimentPath(int index) const;
    QString chunkManifestPath(int index) const;
    QString fullTrackPath(bool vocals) const;

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
    QTimer *m_gainSaveTimer = nullptr;
    bool m_gainsDirty = false;

    StemAudioMixer *m_audioMixer = nullptr;
    NekoNetworkAccessManager *m_cacheNam = nullptr;
    QNetworkReply *m_streamReply = nullptr;
    QSet<QNetworkReply *> m_downloadReplies;
    quint64 m_requestGeneration = 0;
    QString m_jobId;
    QMap<int, QVariantMap> m_readyChunks;
    QSet<int> m_chunkDownloads;
    QSet<QString> m_seenChunkEvents;
    QVariantMap m_fullStemData;
    int m_totalChunks = 0;
    bool m_streamDone = false;
    bool m_fullDownloadPending = false;
    bool m_fullDecodePending = false;
    bool m_fullReady = false;
    bool m_stemRequested = false;
    bool m_stemActive = false;
    bool m_mixerOutputFailed = false;
    int m_accessFailure = 0;
};
