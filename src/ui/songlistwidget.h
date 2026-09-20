#pragma once

#include <QWidget>
#include <QList>
#include <QPoint>
#include <functional>

#include "core/musicinfo.h"

class QScrollArea;
class QLabel;
class QPushButton;
class QTimer;
class QShowEvent;
class QWidget;
class SongCardWidget;

/** SPlayer SongList.vue — 虚拟列表 item-height 90、sticky 表头 */
class SongListWidget : public QWidget
{
    Q_OBJECT

public:
    enum class ListDisplayMode {
        Default,
        HotRanking,
        LatestUpload,
    };

    explicit SongListWidget(QWidget *parent = nullptr);

    void setListDisplayMode(ListDisplayMode mode);
    void setSongs(const QList<MusicInfo> &songs);
    void setCurrentPlayingId(int musicId);
    void setPlaybackPaused(bool paused);
    void applyTheme();
    void retranslate();
    void scrollToTop();
    void scrollToPlaying();
    /** 当前滚动位置（像素），用于「返回歌单时回到原位」。 */
    int scrollOffset() const;
    /** 还原滚动位置：列表刚重建时范围还没跟上，会延后一帧再对齐一次。 */
    void scrollToOffset(int offset);
    bool hasCurrentPlaying() const;
    void setRemoveMode(bool remove);
    void refreshFavoriteDisplay();
    void refreshDownloadDisplay();
    void refreshDownloadProgress();
    void setShowDownloadActions(bool show);
    void setDownloadTaskMode(bool enabled);

    /** 行高 90px + 行间距 12px（对齐 SPlayer SongList） */
    static constexpr int kRowHeight = 90;
    static constexpr int kRowGap = 12;
    static constexpr int kRowStride = kRowHeight + kRowGap;

    std::function<void(const MusicInfo &)> onSongActivate;
    std::function<void(const MusicInfo &)> onSongPlayNext;
    std::function<void(const MusicInfo &, const QPoint &)> onSongContextMenu;
    std::function<void(int)> onUnfavorite;
    std::function<void(const MusicInfo &)> onDownload;
    std::function<void(int)> onCancelDownload;
    std::function<void()> onTogglePlayPause;
    std::function<bool(int)> isFavorited;
    std::function<bool(int)> isDownloaded;
    std::function<bool(int)> isActiveDownload;
    std::function<qint64(int)> downloadProgressReceived;
    std::function<qint64(int)> downloadProgressTotal;

signals:
    void scrolled(int scrollTop);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void syncContainerHeight();
    void updateVisibleRows();
    void scheduleVisibleUpdate();
    void rebuildCurrentRowCache();
    void refreshPlayingState();
    SongCardWidget *acquireCard();
    void releaseCard(SongCardWidget *card);

    static constexpr int kHeaderHeight = 40;
    static constexpr int kListPad = 0;
    static constexpr int kVisibleBuffer = 4;

    QList<MusicInfo> m_songs;
    ListDisplayMode m_listDisplayMode = ListDisplayMode::Default;
    int m_currentId = -1;
    int m_currentRowInList = -1;
    bool m_paused = false;
    bool m_removeMode = false;
    bool m_showDownloadActions = true;
    bool m_downloadTaskMode = false;

    QWidget *m_header = nullptr;
    QLabel *m_hdrNum = nullptr;
    QLabel *m_hdrTitle = nullptr;
    QLabel *m_hdrAlbum = nullptr;
    QLabel *m_hdrActions = nullptr;
    QLabel *m_hdrDuration = nullptr;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_container = nullptr;

    QHash<int, SongCardWidget *> m_rowCards;
    QList<SongCardWidget *> m_cardPool;

    QPushButton *m_scrollTopBtn = nullptr;
    QPushButton *m_scrollCurrentBtn = nullptr;
    QTimer *m_visibleUpdateTimer = nullptr;
};
