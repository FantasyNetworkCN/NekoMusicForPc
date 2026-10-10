#pragma once

#include <QMetaObject>
#include <QWidget>
#include <functional>

#include "core/musicinfo.h"

class QLabel;
class QPushButton;

/** SPlayer SongCard.vue — 90px 行、序号悬停播放、封面 50、专辑/时长/收藏 */
class SongCardWidget : public QWidget
{
    Q_OBJECT

public:
    enum class DisplayMode {
        Default,
        HotRanking,
        LatestUpload,
    };

    explicit SongCardWidget(QWidget *parent = nullptr);

    void setDisplayMode(DisplayMode mode);
    void bind(const MusicInfo &info, int index);
    /** 回收到列表池前断开封面监听，避免滚动时信号堆积 */
    void prepareForPool();
    void setPlaying(bool playing);
    void setPaused(bool paused);
    /** false = 收藏心形；true = 从歌单移除（Delete） */
    void setRemoveMode(bool remove);
    void setFavorited(bool favorited);
    void setDownloaded(bool downloaded);
    void setShowDownloadButton(bool show);
    void setDownloadTaskMode(bool enabled);
    void setDownloadTaskState(bool active, qint64 received, qint64 total);
    /** 与列表表头「时长」列保持一致的宽度，避免英文等较长文案被裁切。 */
    void setDurationColumnWidth(int width);
    void applyTheme();

    const MusicInfo &info() const { return m_info; }

    std::function<void(const MusicInfo &)> onActivate;
    std::function<void(const MusicInfo &)> onPlayNext;
    std::function<void(int)> onUnfavorite;
    std::function<void(const MusicInfo &)> onDownload;
    std::function<void(int)> onCancelDownload;
    std::function<void()> onTogglePlayPause;
    std::function<void(const MusicInfo &, const QPoint &)> onContextMenu;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void rebuildLayout();
    void installContentEventFilters();
    void setHover(bool hover);
    void syncHoverFromCursor();
    bool isInteractiveButton(QObject *obj) const;
    void updateIndexColumn();
    void updateHoverOverlays();
    void updateHeartIcon();
    void updateDownloadIcon();
    void updateMoreIcon();
    void updateCancelIcon();
    void updateDownloadTaskUi();
    void updateOverlayIcons();
    void loadCover();
    void updateSecondaryColumn();
    void updateLocalBadge();
    void updateLrcBadge();
    void updateQualityBadge();
    QString secondaryColumnText() const;
    QString formatDuration(int seconds) const;

    MusicInfo m_info;
    QString m_secondaryText;
    DisplayMode m_displayMode = DisplayMode::Default;
    int m_index = 0;
    bool m_playing = false;
    bool m_paused = false;
    bool m_hover = false;
    bool m_removeMode = false;
    bool m_favorited = false;
    bool m_downloaded = false;
    bool m_showDownloadButton = true;
    bool m_downloadTaskMode = false;
    bool m_downloadActive = false;
    qint64 m_progressReceived = 0;
    qint64 m_progressTotal = 0;
    QString m_coverKey;

    QWidget *m_content = nullptr;
    QWidget *m_numCol = nullptr;
    QLabel *m_indexLbl = nullptr;
    QPushButton *m_playOverlay = nullptr;
    QPushButton *m_statusOverlay = nullptr;
    QLabel *m_coverLbl = nullptr;
    QLabel *m_localBadge = nullptr;
    QLabel *m_titleLbl = nullptr;
    QLabel *m_lrcBadge = nullptr;
    QLabel *m_qualityBadge = nullptr;
    QLabel *m_artistLbl = nullptr;
    QLabel *m_albumLbl = nullptr;
    QWidget *m_progressCol = nullptr;
    QLabel *m_progressLbl = nullptr;
    class QProgressBar *m_progressBar = nullptr;
    QLabel *m_timeLbl = nullptr;
    QPushButton *m_heartBtn = nullptr;
    QPushButton *m_downloadBtn = nullptr;
    /** 行右侧三点菜单按钮（设计稿入口），点击后走同一套 onContextMenu 回调 */
    QPushButton *m_moreBtn = nullptr;
    int m_durationWidth = 50;

    QMetaObject::Connection m_coverConn;
};
