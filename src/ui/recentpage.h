#pragma once

#include <QWidget>
#include <QList>
#include <QSet>

#include "core/musicinfo.h"

class QLabel;
class QPushButton;
class SongListWidget;
class BatchActionBar;

/** 最近播放 — 1:1 SPlayer History.vue (标题 + 播放/清空 + SongList) */
class RecentPage : public QWidget
{
    Q_OBJECT

public:
    explicit RecentPage(QWidget *parent = nullptr);

    void retranslate();
    void refresh();
    void setPlaybackPaused(bool paused);
    void updatePlayingHighlight();
    void setFavoritedMusicIds(const QSet<int> &ids);
    void refreshDownloadDisplay();

signals:
    void playRequested(const MusicInfo &info);
    void playAllRequested(const QList<MusicInfo> &songs);
    void downloadRequested(const MusicInfo &info);
    void downloadAllRequested(const QList<MusicInfo> &songs);
    void favoriteRequested(int musicId);
    void playPauseRequested();
    /** 批量操作：加入歌单 / 批量下载 / 批量取消收藏 */
    void batchAddToPlaylistRequested(const QList<MusicInfo> &songs);
    void batchDownloadRequested(const QList<MusicInfo> &songs);
    void batchFavoriteToggleRequested(const QList<int> &musicIds);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void setupUi();
    void applyPageStyle();
    void loadRecentPlays();
    void updateHeaderMeta();
    void confirmClearRecent();
    int currentPlayingMusicId() const;
    void showPageStatus(const QString &text, const char *iconName = nullptr);
    void hidePageStatus();
    /** 批量编辑模式：按钮排 ↔ 批量操作条切换 */
    void setupBatchBar();
    void enterBatchMode();
    void exitBatchMode();
    void onBatchSelectionChanged(int count);
    /** 一键把当前列表的全部曲目加入本地播放队列 */
    void addAllToQueue();
    void showMoreMenu();

    QWidget *m_header = nullptr;
    QLabel *m_titleLbl = nullptr;
    QLabel *m_countLbl = nullptr;
    QPushButton *m_playBtn = nullptr;
    QWidget *m_menuRow = nullptr;
    QPushButton *m_moreBtn = nullptr;
    BatchActionBar *m_batchBar = nullptr;

    SongListWidget *m_songList = nullptr;
    QWidget *m_emptyWrap = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_emptyIcon = nullptr;

    QList<MusicInfo> m_allRecent;
    QSet<int> m_favoritedIds;
};
