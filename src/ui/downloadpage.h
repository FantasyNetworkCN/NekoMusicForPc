#pragma once

#include <QWidget>
#include <QList>
#include <QSet>

#include "core/musicinfo.h"

class QLabel;
class QPushButton;
class QStackedWidget;
class SongListWidget;
class BatchActionBar;

/** 下载管理 — 正在下载 / 已完成 两个分区 */
class DownloadPage : public QWidget
{
    Q_OBJECT

public:
    explicit DownloadPage(QWidget *parent = nullptr);

    void retranslate();
    void refresh();
    void setPlaybackPaused(bool paused);
    void updatePlayingHighlight();
    void setFavoritedMusicIds(const QSet<int> &ids);
    void refreshDownloadDisplay();

signals:
    void playRequested(const MusicInfo &info);
    void playAllRequested(const QList<MusicInfo> &songs);
    void favoriteRequested(int musicId);
    void playPauseRequested();
    /** 批量操作：加入歌单 / 批量取消收藏（已完成分区才有意义） */
    void batchAddToPlaylistRequested(const QList<MusicInfo> &songs);
    void batchFavoriteToggleRequested(const QList<int> &musicIds);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    enum class Tab { Downloading, Completed };

    void setupUi();
    void applyPageStyle();
    void setActiveTab(Tab tab);
    void loadActiveTab();
    void loadCompletedDownloads();
    void updateHeaderMeta();
    void updateTabLabels();
    void confirmClearDownloads();
    int currentPlayingMusicId() const;
    void showPageStatus(const QString &text, const char *iconName = nullptr);
    void hidePageStatus();
    QList<MusicInfo> activeSongs() const;
    /** 批量编辑模式：按钮排 ↔ 批量操作条切换（仅「已完成」分区开放） */
    void setupBatchBar();
    void enterBatchMode();
    void exitBatchMode();
    void onBatchSelectionChanged(int count);
    /** 一键把当前分区（已完成）的全部曲目加入本地播放队列 */
    void addAllToQueue();
    void showMoreMenu();

    QWidget *m_header = nullptr;
    QLabel *m_titleLbl = nullptr;
    QLabel *m_countLbl = nullptr;
    QLabel *m_statusHintLbl = nullptr;
    QWidget *m_tabBar = nullptr;
    QPushButton *m_tabDownloading = nullptr;
    QPushButton *m_tabCompleted = nullptr;
    QPushButton *m_playBtn = nullptr;
    QWidget *m_menuRow = nullptr;
    QPushButton *m_moreBtn = nullptr;
    BatchActionBar *m_batchBar = nullptr;

    QStackedWidget *m_contentStack = nullptr;
    SongListWidget *m_songList = nullptr;
    QWidget *m_emptyWrap = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_emptyIcon = nullptr;

    Tab m_activeTab = Tab::Downloading;
    QList<MusicInfo> m_pendingDownloads;
    QList<MusicInfo> m_completedDownloads;
    QSet<int> m_favoritedIds;
};
