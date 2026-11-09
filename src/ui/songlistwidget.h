#pragma once

#include <QWidget>
#include <QList>
#include <QPoint>
#include <QSet>
#include <functional>

#include "core/musicinfo.h"

class QScrollArea;
class QLabel;
class QPushButton;
class QTimer;
class QShowEvent;
class QWidget;
class SongCardWidget;

/** 音乐虚拟列表：统一行高、固定表头与批量操作。 */
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

    /**
     * ─── 批量编辑模式 ───────────────────────────────
     * 选中状态按 music_id 保存在本控件里（m_batchSelectedIds），
     * 绝不存在会被卡片池复用的 SongCardWidget 上，滚动后不会错乱。
     * 退出模式 / 列表数据被替换时自动清空选择。
     */
    void setBatchMode(bool enabled);
    bool batchMode() const { return m_batchMode; }
    void clearBatchSelection();
    QSet<int> batchSelectedIds() const { return m_batchSelectedIds; }
    int batchSelectedCount() const { return m_batchSelectedIds.size(); }
    /** 当前列表里已选中的曲目（按列表顺序返回，供批量动作使用） */
    QList<MusicInfo> batchSelectedSongs() const;
    /** 全选 / 取消全选（以当前 m_songs 为准） */
    void setBatchSelectAll(bool selectAll);
    bool batchAllSelected() const;

    /** 保留封面与两行文字的呼吸感，同时提高桌面列表密度。 */
    static constexpr int kRowHeight = 76;
    static constexpr int kRowGap = 6;
    static constexpr int kRowStride = kRowHeight + kRowGap;

    std::function<void(const MusicInfo &)> onSongActivate;
    std::function<void(const MusicInfo &)> onSongPlayNext;
    std::function<void(const MusicInfo &, const QPoint &)> onSongContextMenu;
    /** 统一菜单（设计稿）所需回调：页面未接管 onSongContextMenu 时，列表自己弹出统一菜单 */
    std::function<void(const MusicInfo &)> onToggleFavorite;
    std::function<bool(int)> isInPlayQueue;
    std::function<void(const MusicInfo &)> onToggleQueue;
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
    /** 批量模式选中集合变化（含进入/退出模式导致的清空） */
    void batchSelectionChanged(int count);
    void batchModeChanged(bool enabled);

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void syncContainerHeight();
    void updateVisibleRows();
    /** 上下边缘渐隐：浮层挂在滚动视口内，几何完全由列表自己维护，页面无需参与。 */
    void updateEdgeFade();
    void scheduleVisibleUpdate();
    void rebuildCurrentRowCache();
    void refreshPlayingState();
    SongCardWidget *acquireCard();
    void releaseCard(SongCardWidget *card);
    /** 按当前语言的最宽表头文案重算列宽；「时长」列同步到数据行以保持对齐。 */
    void updateHeaderColumnWidths();
    /** 弹出统一菜单（收藏 / 播放队列 / 下载）——设计稿的三点菜单内容 */
    void showStandardContextMenu(const MusicInfo &info, const QPoint &globalPos);
    /** 批量模式：切换某首曲目的选中态（按 music_id 记录，与行号无关） */
    void toggleBatchSelection(const MusicInfo &info);
    /** 把选中集合回灌到当前可见行（卡片池复用后必须重灌） */
    void syncBatchToVisibleRows();
    /** 以当前列表内容为准，丢弃已经不在列表里的选中项 */
    void pruneBatchSelection();

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
    bool m_batchMode = false;
    /** 批量模式的选中集合：按 music_id 保存，卡片池复用不影响 */
    QSet<int> m_batchSelectedIds;

    QWidget *m_header = nullptr;
    QLabel *m_hdrNum = nullptr;
    QLabel *m_hdrTitle = nullptr;
    QLabel *m_hdrAlbum = nullptr;
    QLabel *m_hdrActions = nullptr;
    QLabel *m_hdrDuration = nullptr;
    int m_durationColumnWidth = 50;

    QScrollArea *m_scroll = nullptr;
    QWidget *m_container = nullptr;
    /** 渐隐浮层（挂在滚动视口上，随视口自动对齐）。 */
    QWidget *m_edgeFade = nullptr;

    QHash<int, SongCardWidget *> m_rowCards;
    QList<SongCardWidget *> m_cardPool;
    /** 每行当前已回灌的选中态，避免滚动时重复刷同一行 */
    QSet<int> m_rowBatchChecked;

    QPushButton *m_scrollTopBtn = nullptr;
    QPushButton *m_scrollCurrentBtn = nullptr;
    QTimer *m_visibleUpdateTimer = nullptr;
};
