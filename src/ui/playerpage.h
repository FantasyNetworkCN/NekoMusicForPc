#pragma once

#include <QWidget>
#include <QResizeEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QMetaObject>
#include <QColor>
#include <QPixmap>
#include "../core/audioquality.h"
#include "../core/musicinfo.h"
#include "../core/playerengine.h"

class ApiClient;
class QTimer;
class QWheelEvent;
class QSlider;

struct LyricLine {
    qint64 time;
    QString text;
    QString translation;
};

class PlayerPage : public QWidget
{
    Q_OBJECT

public:
    explicit PlayerPage(PlayerEngine *engine, ApiClient *apiClient, QWidget *parent = nullptr);
    ~PlayerPage() override;

    void setMusicInfo(int id, const QString &title, const QString &artist,
                      const QString &album, const QString &coverUrl = QString());
    /** 缓存就绪或起播后重新探测音质（避免启动时误显示 HQ） */
    void refreshAudioQuality();
    void retranslate();
    /** 在线曲走 API；本地曲读内嵌标签（ID3 USLT / FLAC 注释）再尝试同名 .lrc，不请求网络。 */
    void loadLyricsForTrack(const MusicInfo &info);
    void updateLyricHighlight(qint64 positionMs);
    void updatePlayModeBtn(const QString &mode);
    void refreshTintedPalette();
    int coverSideLength() const;
    QColor idleIconColor() const;
    QColor accentIconColor() const;
    void setFavoriteStatus(bool isFavorited);
    void setDesktopLyricsChecked(bool checked);
    void setVolumePercentSynced(int percent);
    void layoutPlayerPageChrome();
    /** 抓取 host 并模糊，对齐 SPlayer .full-player backdrop-filter，避免透出背后清晰界面 */
    void refreshUnderlayBackdrop(QWidget *source, const QSize &targetSize = QSize());
    /** 直接提交已抓取的底图（主窗口缓存），避免进入播放页时同步 render 整窗。 */
    void setUnderlaySnapshot(const QPixmap &snapshot, const QSize &targetSize);
    /** 滑入/滑出动画期间使用轻量背景绘制，减轻掉帧。 */
    void setOpenTransitionActive(bool active);
    void emitDesktopLyricsPayload();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

signals:
    void backRequested();
    void previousClicked();
    void nextClicked();
    void favoriteClicked(int musicId);
    void addToPlaylistClicked(int musicId);
    void playlistClicked();
    void desktopLyricsToggled(bool enabled);
    void volumePercentChanged(int percent);
    /** 播放页歌词更新后同步桌面歌词（LRC 文本，空表示无歌词） */
    void lyricsPayloadReady(const QString &lrcText);
    /** 底栏艺人行：当前歌词行（含翻译括号）；lineIndex&lt;0 表示尚无对应当前时间的行 */
    void barLyricLineChanged(const QString &displayText, int lineIndex, bool trackHasLyrics);

private:
    void setupUi();
    void setupPlayerControl();
    void connectPlayerControlEngine();
    void updatePlayControlState();
    void updateVolumeIcon(int percent);
    void showVolumePanel();
    void hideVolumePanel();
    QRect volumePanelHotRectGlobal() const;
    void updateCoverPlayScale(bool playing);
    void applyCoverVisualScale(qreal scale);
    void refreshCoverLayout();
    void updateCoverBackdrop(const QPixmap &source);
    void scheduleUnderlayBlur(const QSize &target);
    void scheduleCoverBlur(const QPixmap &source, const QSize &target);
    void setControlSidesVisible(bool visible);
    void bumpControlShowTimer();
    void hidePlayerChrome();
    void scheduleChromeLeaveCheck();
    void applyPlayerPageStyle();
    void applyMetaLabelFonts();
    /** 按左栏宽度对曲名/歌手/专辑单行省略，避免过长换行堆叠溢出 */
    void applyMetaTextElide();
    void updateMetaIcons();
    void scheduleAudioQualityProbe();
    void applyAudioQualityBadge(const AudioQuality::ProbeResult &result);
    void hideAudioQualityBadge();
    void refineAudioQualityFromEngine();
    void updateQualityBadgeStyle();
    void applyLyricLineStyle(QLabel *textLabel, QLabel *transLabel, bool isCurrent) const;
    void syncLyricLinesVisual(int activeLine, int previousLine = -1);
    /** 视口中心清晰、上下渐模糊（对齐 Apple Music / SPlayer 歌词聚焦） */
    void applyLyricViewportFocusBlur();
    void scheduleLyricFocusBlurUpdate();
    void scrollLyricsToActiveLine(int line);
    void pauseLyricAutoScroll();
    /** 按歌词区视口宽度约束换行（长句自动折行） */
    void updateLyricWrapWidth();
    int lyricTextAreaWidth() const;
    void relayoutLeftInfoColumn();
    void loadCover(const QString &url);
    void applyCoverPixmap(const QPixmap &sourcePixmap);
    void applyCoverUnknownLarge();
    void parseLrc(const QString &lrc);
    /** 将一段 LRC 或纯文本歌词写入 m_lyrics（无时间轴时整段作为 t=0 一行） */
    void applyLyricsRawText(const QString &raw);
    void rebuildLyricLabels();
    void updateLyricCountdown(qint64 positionMs);
    QColor lyricCountdownDotColor() const;
    QString serializeLyricsForDesktop() const;
    void emitBarLyricUpdate(int lineIndex);
    void resetVideoRenderState();
    void updateVideoRenderUi();
    void pollVideoRenderStatus();
    void openVideoRenderDialog();
    void downloadRenderedVideo();
    int trackDurationSec() const;

    PlayerEngine *m_engine;
    ApiClient *m_apiClient = nullptr;

    QWidget *m_leftPanel = nullptr;
    QWidget *m_rightPanel = nullptr;
    QWidget *m_leftInfoColumn = nullptr;
    QWidget *m_metaPanel = nullptr;
    QWidget *m_artistRow = nullptr;
    QWidget *m_albumRow = nullptr;

    QString m_clrTitle;
    QString m_clrArtist;
    QString m_clrAlbum;
    QString m_clrLyricDim;
    QString m_clrLyricHi;
    QString m_clrLyricHiTrans;
    QString m_clrLyricHiBg;
    QColor m_coverMainColor;
    QColor m_coverSecondColor;
    QPixmap m_bgBlurPixmap;
    QPixmap m_coverBackdropSource;
    QPixmap m_underlaySnapshot;
    QPixmap m_underlayBlurPixmap;
    bool m_controlSidesVisible = false;
    qreal m_coverVisualScale = 0.9;
    bool m_coverScalePlaying = false;

    QWidget *m_menuBar = nullptr;
    QWidget *m_contentHost = nullptr;
    QPushButton *m_backBtn = nullptr;
    QWidget *m_controlBar = nullptr;
    QGraphicsOpacityEffect *m_ppMenuOpacity = nullptr;
    QPropertyAnimation *m_ppMenuOpAnim = nullptr;
    QVariantAnimation *m_chromeFadeAnim = nullptr;
    QVariantAnimation *m_coverScaleAnim = nullptr;
    QWidget *m_ppLeftTools = nullptr;
    QWidget *m_ppRightTools = nullptr;
    QGraphicsOpacityEffect *m_ppControlOpacity = nullptr;
    QPropertyAnimation *m_ppControlOpAnim = nullptr;
    QTimer *m_controlHideTimer = nullptr;
    QPushButton *m_ppHeartBtn = nullptr;
    QPushButton *m_ppAddToPlaylistBtn = nullptr;
    QPushButton *m_ppPlayModeBtn = nullptr;
    QPushButton *m_ppPlaylistBtn = nullptr;
    QPushButton *m_ppVolumeBtn = nullptr;
    QWidget *m_ppVolumePanel = nullptr;
    QSlider *m_ppVolumeSlider = nullptr;
    QLabel *m_ppVolumeLabel = nullptr;
    QGraphicsOpacityEffect *m_ppVolumeOpacityFx = nullptr;
    QPropertyAnimation *m_ppVolumeOpAnim = nullptr;
    QPropertyAnimation *m_ppVolumePosAnim = nullptr;
    QTimer *m_ppVolumeLeaveTimer = nullptr;
    bool m_ppVolumePanelClosing = false;
    bool m_ppVolumeAppFilterInstalled = false;
    QPushButton *m_ppDesktopLrcBtn = nullptr;
    QPushButton *m_ppPrevBtn = nullptr;
    QPushButton *m_ppPlayBtn = nullptr;
    QPushButton *m_ppNextBtn = nullptr;
    class PlayerProgressSlider *m_ppProgress = nullptr;
    QLabel *m_ppCurTime = nullptr;
    QLabel *m_ppDurTime = nullptr;
    QWidget *m_coverFrame = nullptr;
    QLabel *m_coverImage = nullptr;
    QPixmap m_coverRoundedBase;
    QLabel *m_titleLabel;
    QWidget *m_qualityRow = nullptr;
    QLabel *m_qualityBadge = nullptr;
    QLabel *m_artistMetaIcon = nullptr;
    QLabel *m_albumMetaIcon = nullptr;
    QLabel *m_artistLabel;
    QLabel *m_albumLabel;
    QPushButton *m_videoRenderBtn = nullptr;
    QPushButton *m_videoDownloadBtn = nullptr;
    QLabel *m_videoStatusLbl = nullptr;
    QTimer *m_videoPollTimer = nullptr;
    QString m_fullMetaTitle;
    QString m_fullMetaArtist;
    QString m_fullMetaAlbum;
    bool m_titleIsPlaceholder = true;
    bool m_artistIsPlaceholder = true;
    QScrollArea *m_lyricsScroll;
    QWidget *m_lyricsContainer;
    QVBoxLayout *m_lyricsLayout;

    int m_musicId = 0;
    int m_trackDurationSec = 0;
    QString m_videoJobId;
    QString m_videoJobStatus;
    QString m_videoJobError;
    int m_videoRemainingToday = -1;
    bool m_videoRenderBusy = false;
    QString m_coverUrl;
    QVector<LyricLine> m_lyrics;
    /** 已解析歌词：在线为正 id；本地为 stableLocalTrackId（负） */
    QHash<int, QVector<LyricLine>> m_lyricsCache;
    QMetaObject::Connection m_coverConn;
    int m_currentLyricLine = -1;
    QVector<QWidget *> m_lyricLineWidgets;
    QWidget *m_lyricIntroCountdown = nullptr;
    QPropertyAnimation *m_scrollAnim = nullptr;
    bool m_lyricUserScrolling = false;
    QTimer *m_lyricScrollResumeTimer = nullptr;
    QTimer *m_lyricBlurTimer = nullptr;
    QTimer *m_underlayResizeTimer = nullptr;
    /** 每次发起/清空歌词请求自增，用于丢弃过期的异步回调（本地曲歌词 id 可与 m_musicId 不一致） */
    int m_lyricsFetchGeneration = 0;
    int m_underlayBlurGen = 0;
    int m_coverBlurGen = 0;
    QSize m_underlayBlurTarget;
    bool m_openTransitionActive = false;

    QNetworkAccessManager *m_qualityNam = nullptr;
    QNetworkReply *m_qualityReply = nullptr;
    int m_qualityProbeGen = 0;
    AudioQuality::ProbeResult m_lastQuality;
    AudioQuality::ProbeResult m_fileProbedQuality;
    bool m_qualityFromPlayerMeta = false;
    bool m_hasFileProbedQuality = false;
};
