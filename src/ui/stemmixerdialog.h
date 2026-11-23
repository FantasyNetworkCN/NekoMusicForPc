#pragma once

#include <QDialog>
#include <QMap>
#include <QSet>
#include <QVariantMap>
#include <functional>

#include "core/musicinfo.h"

class ApiClient;
class PlayerEngine;
class NekoNetworkAccessManager;
class QNetworkReply;
class QLabel;
class QCheckBox;
class QPushButton;
class QProgressBar;
class QSlider;
class QTimer;

/**
 * 人声 / 伴奏分离播放器。
 *
 * 原曲始终承担播放时钟。服务端通过 SSE 返回分片后，PC 立即下载并缓存，
 * 双轨就绪时静音原曲；遇到缺片、seek 或网络失败则继续播放原曲，不能阻塞
 * 主播放器，也不能因为普通网络错误自动关闭开关。
 */
class StemMixerDialog final : public QDialog
{
public:
    explicit StemMixerDialog(ApiClient *apiClient, PlayerEngine *engine,
                             const MusicInfo &music, QWidget *parent = nullptr);
    ~StemMixerDialog() override;

private:
    enum class StemSource {
        None,
        Chunk,
        Full
    };

    void setupUi();
    void ensurePlayers();
    void loadCachedStems();
    void startStemStream();
    void stopStemStream();
    void onStemStreamEvent(const QString &event, const QVariantMap &data);
    void onStemStreamError(int statusCode, const QString &message);
    void cacheChunk(int index, const QVariantMap &data);
    void cacheFullTracks(const QVariantMap &data);
    void startChunkPlayback(int index, const QVariantMap &chunk);
    void startFullPlayback(qint64 positionMs);
    void stopStemPlayback(bool restoreMain = true);
    void chooseSourceForPosition(qint64 positionMs);
    void handlePlaybackFailure(const QString &message);
    void saveChunkManifest(int index, const QVariantMap &chunk) const;
    void updateProgress();
    void updateStemControls();
    void persistGains() const;
    void setStatus(const QString &text);
    void setStemActive(bool active);
    void finishStemSwitch();
    void applyStemVolumes();
    void syncTransport();
    void syncPositions();
    void stopPlayback();

    QString musicCacheDir() const;
    QString chunkVocalsPath(int index) const;
    QString chunkAccompanimentPath(int index) const;
    QString chunkManifestPath(int index) const;
    QString fullVocalsPath() const;
    QString fullAccompanimentPath() const;
    int chunkIndexAt(qint64 positionMs, QVariantMap *chunk = nullptr) const;
    qint64 chunkOffsetMs(const QVariantMap &chunk, qint64 positionMs) const;
    bool currentSourceMatchesPosition(qint64 positionMs) const;

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

    PlayerEngine *m_vocalsEngine = nullptr;
    PlayerEngine *m_accompanimentEngine = nullptr;
    NekoNetworkAccessManager *m_cacheNam = nullptr;
    QNetworkReply *m_streamReply = nullptr;
    QString m_jobId;
    QMap<int, QVariantMap> m_readyChunks;
    QSet<int> m_chunkDownloads;
    QSet<int> m_failedChunks;
    QSet<QString> m_seenChunkEvents;
    int m_totalChunks = 0;
    int m_activeChunkIndex = -1;
    int m_loadingChunkIndex = -1;
    StemSource m_source = StemSource::None;
    StemSource m_loadingSource = StemSource::None;
    bool m_fullReady = false;
    bool m_fullDownloading = false;
    bool m_fullFailed = false;
    bool m_streamDone = false;
    bool m_stemActive = false;
    bool m_stemSwitched = false;
    bool m_switchPending = false;
    bool m_vocalsLoaded = false;
    bool m_accompanimentLoaded = false;
    QTimer *m_syncTimer = nullptr;
    QTimer *m_switchTimeout = nullptr;
};
