#pragma once

/**
 * @file playerbar.h
 * @brief 底部播放控制栏 — 日系动漫风
 *
 * 80px，对齐 SPlayer MainPlayer：顶栏进度条 + 左封面信息 / 中控 / 右时间工具。
 */

#include <QWidget>

class QResizeEvent;
class QShowEvent;

class PlayerEngine;
class QSlider;
class QPushButton;
class QLabel;
class QTimer;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

class PlayerBar : public QWidget
{
    Q_OBJECT

signals:
    void coverClicked();
    void playlistClicked();
    void desktopLyricsToggled(bool enabled);
    void previousClicked();
    void nextClicked();
    void favoriteClicked(int musicId);
    void addToPlaylistClicked(int musicId);
    void shareClicked();
    void videoShareClicked();
    void playModeClicked();
    void volumePercentChanged(int percent);

public:
    explicit PlayerBar(PlayerEngine *engine, QWidget *parent = nullptr);
    ~PlayerBar() override;
    void retranslate();
    void setVolumePercentSynced(int percent);
    void setSongInfo(const QString &title, const QString &artist, const QString &coverUrl = QString());
    void setCoverVisible(bool visible);
    void setCurrentMusicId(int musicId);
    int currentMusicId() const { return m_currentMusicId; }
    void setFavoriteStatus(bool isFavorited);
    void setDesktopLyricsChecked(bool checked);
    void setLoading(bool loading);
    void updatePlayModeBtn(const QString &mode);
    void updateVideoShareUi(bool available, bool busy, const QString &jobStatus);

    /** 窗口尺寸变化时同步顶栏进度条几何（挂到主窗口并 top:-8px） */
    void relayoutChrome();

    /** 全屏播放页时隐藏顶栏进度条（进度条 parent 为主窗口，hide 底栏不会带走） */
    void setChromeVisible(bool visible);

    /** 播放队列抽屉打开时隐藏悬浮进度条（避免盖住抽屉底部） */
    void setFloatingProgressSuppressed(bool suppressed);

    /** 切页等导致主内容变化时刷新底部栏磨砂采样 */
    void refreshGlassBackdrop();

    /** 整窗底图切换时同步底栏透明度 */
    void applyShellBackdropChrome();

public slots:
    /** 播放页当前歌词行（SPlayer 底栏 lyric-slide） */
    void setBarLyricLine(const QString &displayText, int lineIndex, bool trackHasLyrics);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setupUi();
    void layoutPlayerBarChrome(); // relayoutChrome()
    /** 播放控件绝对居中（不占 flex 槽位，避免左右列挤压偏移） */
    void layoutCenterControls();
    /** 底栏单侧（左/右）可用宽度上限，由窗口几何推算，不读 m_pbLeft 当前宽 */
    int playerBarSideBudget() const;
    void applyPlayerBarGlassStyle();
    void updateState();
    void setCoverPixmap(const QPixmap &pm);
    void setCoverUnknownPlaceholder();
    void updateVolumeIcon(int value);
    void showVolumePanelAnimated();
    void hideVolumePanelAnimated();
    QRect volumePanelHotRectGlobal() const;
    void installVolumePanelAppFilter();
    void removeVolumePanelAppFilter();
    void applyLocalBadgeChrome();
    void refreshLocalBadge();
    void updateTitleMarqueeWidth();
    void scheduleTitleMarqueeWidthUpdate();
    void refreshBarLyricSlot();

    bool m_titleMarqueeUpdateScheduled = false;
    bool m_chromeVisible = true;
    bool m_floatingProgressSuppressed = false;

    QWidget *m_pbLeft = nullptr;
    QWidget *m_pbCenter = nullptr;
    QWidget *m_pbCenterSpacer = nullptr;
    QWidget *m_pbRight = nullptr;
    QWidget *m_titleRow = nullptr;

    PlayerEngine *m_engine = nullptr;
    class GlassWidget *m_glass = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_playModeBtn = nullptr;
    QPushButton *m_heartBtn = nullptr;
    QPushButton *m_addToPlaylistBtn = nullptr;
    QPushButton *m_shareBtn = nullptr;
    QPushButton *m_videoShareBtn = nullptr;
    QSlider *m_progress = nullptr;
    
    QWidget *m_volumePanel = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QPushButton *m_volumeBtn = nullptr;
    QPushButton *m_desktopLrcBtn = nullptr;
    QLabel *m_volumeLabel = nullptr;
    QGraphicsOpacityEffect *m_volumeOpacityFx = nullptr;
    QPropertyAnimation *m_volumeOpAnim = nullptr;
    QPropertyAnimation *m_volumePosAnim = nullptr;
    bool m_volumePanelClosing = false;
    QTimer *m_volumeLeaveTimer = nullptr;
    bool m_volumeAppFilterInstalled = false;

    QLabel *m_localBadge = nullptr;
    QLabel *m_songName = nullptr;
    QLabel *m_artist = nullptr;
    QWidget *m_lyricSlot = nullptr;
    QLabel *m_barLyricLine = nullptr;
    QString m_barLyricText;
    int m_barLyricLineIndex = -1;
    bool m_trackHasLyrics = false;
    QLabel *m_curTime = nullptr;
    QLabel *m_durTime = nullptr;
    QPushButton *m_cover = nullptr;
    QMetaObject::Connection m_coverConn;
    int m_currentMusicId = 0;
    bool m_isFavorited = false;
    bool m_isLoading = false;
    int m_loadingAngle = 0;
    QTimer *m_loadingTimer = nullptr;
};
