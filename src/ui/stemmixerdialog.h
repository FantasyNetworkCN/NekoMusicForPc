#pragma once

#include <QDialog>

#include "core/musicinfo.h"

class ApiClient;
class QLabel;
class QMediaPlayer;
class QAudioOutput;
class QPushButton;
class QSlider;
class QTimer;
class HttpMediaDevice;
class QUrl;

/**
 * 人声 / 伴奏分离播放器。
 *
 * 服务端返回两条媒体地址后，PC 使用两个 QMediaPlayer 同步播放，并分别调节
 * 人声与伴奏增益。原播放器由主窗口暂停，关闭弹窗后再恢复。
 */
class StemMixerDialog final : public QDialog
{
public:
    explicit StemMixerDialog(ApiClient *apiClient, const MusicInfo &music, QWidget *parent = nullptr);
    ~StemMixerDialog() override;

private:
    void setupUi();
    void fetchFiles();
    void preparePlayers(const QUrl &vocalsUrl, const QUrl &accompanimentUrl);
    void togglePlayback();
    void syncPositions();
    void updatePlayButton();
    void setStatus(const QString &text);
    void stopPlayback();

    ApiClient *m_apiClient = nullptr;
    MusicInfo m_music;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QSlider *m_vocalsSlider = nullptr;
    QSlider *m_accompanimentSlider = nullptr;
    QLabel *m_vocalsValue = nullptr;
    QLabel *m_accompanimentValue = nullptr;
    QPushButton *m_playButton = nullptr;
    QPushButton *m_closeButton = nullptr;

    QMediaPlayer *m_vocalsPlayer = nullptr;
    QMediaPlayer *m_accompanimentPlayer = nullptr;
    QAudioOutput *m_vocalsOutput = nullptr;
    QAudioOutput *m_accompanimentOutput = nullptr;
    HttpMediaDevice *m_vocalsDevice = nullptr;
    HttpMediaDevice *m_accompanimentDevice = nullptr;
    QTimer *m_syncTimer = nullptr;
    bool m_vocalsReady = false;
    bool m_accompanimentReady = false;
};
