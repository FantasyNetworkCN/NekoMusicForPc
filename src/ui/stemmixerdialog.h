#pragma once

#include <QDialog>
#include <QUrl>

#include "core/musicinfo.h"

class ApiClient;
class PlayerEngine;
class QLabel;
class QCheckBox;
class QPushButton;
class QSlider;
class QTimer;

/**
 * 人声 / 伴奏分离播放器。
 *
 * 服务端返回两条媒体地址后，PC 使用两个 QMediaPlayer 同步播放，并分别调节
 * 人声与伴奏增益。启用后主播放器继续作为播放时钟，只静音原音频；双轨
 * 跟随主播放器的播放状态与进度。
 */
class StemMixerDialog final : public QDialog
{
public:
    explicit StemMixerDialog(ApiClient *apiClient, PlayerEngine *engine,
                             const MusicInfo &music, QWidget *parent = nullptr);
    ~StemMixerDialog() override;

private:
    void setupUi();
    void fetchFiles();
    void prepareTracks(const QUrl &vocalsUrl, const QUrl &accompanimentUrl);
    void setStemActive(bool active);
    void applyStemVolumes();
    void syncTransport();
    void syncPositions();
    void updateStemControls();
    void setStatus(const QString &text);
    void stopPlayback();

    ApiClient *m_apiClient = nullptr;
    PlayerEngine *m_engine = nullptr;
    MusicInfo m_music;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
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
    QUrl m_vocalsUrl;
    QUrl m_accompanimentUrl;
    QTimer *m_syncTimer = nullptr;
    bool m_tracksReady = false;
    bool m_stemActive = false;
    bool m_pendingEnable = false;
};
