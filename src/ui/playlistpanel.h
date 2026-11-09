#pragma once

/**
 * @file playlistpanel.h
 * @brief 播放队列抽屉 — 从窗口右侧滑入，盖住底栏播放器（对齐 SPlayer n-drawer）
 *
 * 显示当前播放队列，支持点击播放、移除、清空操作。
 * 列表采用可见区虚拟化，大列表打开不卡顿。
 *
 * 单曲行右侧有「三点菜单」（复用 SongMenuBuilder 的统一菜单），
 * 顶部可进入「批量编辑」模式（复用 BatchActionBar）；两者都只对**本机队列**开放，
 * 局域网远端设备的队列是只读视图。
 */

#include <QWidget>
#include <QList>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QPropertyAnimation>
#include <functional>
#include "core/musicinfo.h"
#include "ui/songcontextmenu.h"

class QScrollArea;
class QVBoxLayout;
class QLabel;
class QPushButton;
class QComboBox;
class QStackedWidget;
class BatchActionBar;
class PlaylistItemCard;
struct LanDeviceInfo;

class PlaylistPanel : public QWidget {
    Q_OBJECT

public:
    explicit PlaylistPanel(QWidget *parent = nullptr);

    static constexpr int kDrawerWidth = 400;

    bool isDrawerOpen() const { return m_drawerOpen; }
    /** 宿主尺寸变化时保持贴右（打开中则保持展开，关闭则保持屏外） */
    void syncToHost();
    /** 全屏播放页内打开：抽屉透明，仅靠背后模糊（对齐 SPlayer .full-player） */
    void setFullPlayerMode(bool on);

    /** 收藏态查询与刷新：由 MainWindow 注入（它持有收藏缓存），与 SongListWidget 同一套写法 */
    std::function<bool(int)> isFavorited;
    std::function<void(const MusicInfo &)> onToggleFavorite;
    std::function<void(const MusicInfo &)> onDownload;
    /** 收藏缓存变化后刷新行内菜单状态 */
    void refreshFavoriteDisplay();

signals:
    void playRequested(int musicId);
    void remoteReplaceRequested(bool play);
    void hideRequested();
    void drawerClosed();
    /** 批量操作：加入歌单 / 批量下载 / 批量取消收藏（移出队列在面板内直接做） */
    void batchAddToPlaylistRequested(const QList<MusicInfo> &songs);
    void batchDownloadRequested(const QList<MusicInfo> &songs);
    void batchUnfavoriteRequested(const QList<int> &musicIds);

public slots:
    void refresh();
    void refreshDevices();
    void refreshRemoteQueue();
    void retranslate();
    void openDrawer();
    void closeDrawer();
    void showPanel();
    void hidePanel();
    void togglePanel();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void applyPanelChrome();
    void syncContainerHeight();
    void updateVisibleRows();
    void clearAllCards();
    void updateCountLabel();
    void scrollToCurrent();
    void updateFooterButtonIcons();
    /** 把某一行绑定到卡片（含批量模式状态回灌） */
    void bindRowCard(PlaylistItemCard *card, int row);
    /** 当前队列里已选中的曲目（按队列顺序返回） */
    QList<MusicInfo> batchSelectedSongs() const;
    /** 弹出单曲统一菜单（下一首播放 / 收藏 / 从队列移除 / 下载） */
    void showSongMenu(const MusicInfo &info, const QPoint &globalPos);
    /** 批量模式选中集合变化后，把状态回灌给可见行 */
    void syncBatchToVisibleRows();
    void refreshBatchBar();
    void enterBatchMode();
    void exitBatchMode();
    /** 弹出批量操作的竖排悬浮菜单（400px 抽屉里横排放不下，改成浮层） */
    void showBatchActionsMenu();
    /** 组装批量浮层的条目（「全选」原地刷新时用它重建一份新状态的） */
    QList<SongContextMenuPopup::Entry> buildBatchMenuEntries();
    /** 竖排浮层的锚点：紧凑触发按钮正下方 */
    QPoint batchMenuAnchorPos() const;
    /** 批量移出播放队列：按序本地移除，收尾一次 Toast */
    void removeSelectedFromQueue();

    /** SPlayer VirtualScroll item-height 80（行高 64 + 上下间距 8+8） */
    static constexpr int kRowHeight = 64;
    static constexpr int kRowSpacing = 16;
    static constexpr int kRowStride = kRowHeight + kRowSpacing;
    static constexpr int kVisibleBuffer = 4;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QComboBox *m_deviceCombo = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_scrollCurrentBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_listContainer = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QWidget *m_footer = nullptr;
    /** 批量模式：「批量」按钮那排 ↔ 批量操作条 的切换容器 */
    QStackedWidget *m_actionStack = nullptr;
    QWidget *m_actionRow = nullptr;
    QPushButton *m_batchBtn = nullptr;
    BatchActionBar *m_batchBar = nullptr;
    /** 当前打开的批量浮层（WA_DeleteOnClose，必须弱引用）；「全选」原地刷新它 */
    QPointer<SongContextMenuPopup> m_batchMenu = nullptr;
    QHash<int, QWidget *> m_rowCards;
    QList<QWidget *> m_cardPool;
    QList<MusicInfo> m_displayPlaylist;
    int m_displayCurrentIndex = -1;
    bool m_remoteView = false;
    int m_lastPlaylistSize = 0;
    bool m_drawerOpen = false;
    bool m_animating = false;
    bool m_fullPlayerMode = false;
    /** 批量模式的选中集合：按 music_id 保存，卡片池复用不影响（与 SongListWidget 同一套约定） */
    bool m_batchMode = false;
    QSet<int> m_batchSelectedIds;
    /** 每个可见行当前已回灌的选中态，避免重复刷同一行 */
    QSet<int> m_rowBatchChecked;
    // QPointer：动画以 DeleteWhenStopped 启动，Qt 可能在成员未置空时已经销毁它
    QPointer<QPropertyAnimation> m_slideAnim;
    class QGraphicsDropShadowEffect *m_drawerShadow = nullptr;
};
