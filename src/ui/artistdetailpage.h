#pragma once

/**
 * @file artistdetailpage.h
 * @brief 歌手详情子页 — 搜索艺术家结果点击进入
 */

#include <QWidget>
#include <QList>
#include <QSet>
#include <QVariantMap>

#include "core/musicinfo.h"

class QLabel;
class QPushButton;
class SongListWidget;
class BatchActionBar;

class ArtistDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit ArtistDetailPage(QWidget *parent = nullptr);

    void loadArtist(const QVariantMap &artist);
    void retranslate();
    void setFavoritedMusicIds(const QSet<int> &ids);
    void setPlaybackPaused(bool paused);
    void updatePlayingHighlight();
    void refreshDownloadDisplay();

signals:
    void playMusic(const MusicInfo &info);
    void playAllRequested(const QList<MusicInfo> &songs);
    void downloadRequested(const MusicInfo &info);
    void downloadAllRequested(const QList<MusicInfo> &songs);
    void favoriteRequested(int musicId);
    void playPauseRequested();
    void backRequested();
    /** 批量操作：加入歌单 / 批量下载 / 批量取消收藏 */
    void batchAddToPlaylistRequested(const QList<MusicInfo> &songs);
    void batchDownloadRequested(const QList<MusicInfo> &songs);
    void batchFavoriteToggleRequested(const QList<int> &musicIds);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void setupUi();
    void applyPageStyle();
    void updateHeaderMeta();
    int currentPlayingMusicId() const;
    static QList<MusicInfo> tracksFromArtistMap(const QVariantMap &artist);
    /** 批量编辑模式：按钮排 ↔ 批量操作条切换 */
    void setupBatchBar();
    void enterBatchMode();
    void exitBatchMode();
    void onBatchSelectionChanged(int count);
    /** 一键把当前歌手的全部曲目加入本地播放队列 */
    void addAllToQueue();
    void showMoreMenu();

    QWidget *m_topBar = nullptr;
    QPushButton *m_backBtn = nullptr;
    QWidget *m_header = nullptr;
    QLabel *m_titleLbl = nullptr;
    QLabel *m_countLbl = nullptr;
    QPushButton *m_playBtn = nullptr;
    QWidget *m_menuRow = nullptr;
    QPushButton *m_moreBtn = nullptr;
    BatchActionBar *m_batchBar = nullptr;
    SongListWidget *m_songList = nullptr;

    QString m_artistName;
    QList<MusicInfo> m_tracks;
    QSet<int> m_favoritedIds;
};
