#pragma once

/**
 * @file playlistdetailpage.h
 * @brief 歌单详情 — 1:1 SPlayer playlist.vue (ListDetail + SongList + SongCard)
 */

#include <QWidget>
#include <QList>
#include <QSet>

#include "core/musicinfo.h"

class QLabel;
class QPushButton;
class QLineEdit;
class RoundCoverLabel;
class QVariantAnimation;
class QTimer;
class SongListWidget;
class ApiClient;

class PlaylistDetailPage : public QWidget
{
    Q_OBJECT

public:
    explicit PlaylistDetailPage(ApiClient *apiClient, QWidget *parent = nullptr);

    void loadPlaylist(int playlistId);
    void retranslate();
    void setPlaybackPaused(bool paused);
    void setFavoritedMusicIds(const QSet<int> &ids);
    void updatePlayingHighlight();
    void refreshDownloadDisplay();

signals:
    void playMusic(const MusicInfo &info);
    /** 右键「下一首播放」 */
    void playNextRequested(const MusicInfo &info);
    void playAllRequested(const QList<MusicInfo> &songs);
    void downloadRequested(const MusicInfo &info);
    void downloadAllRequested(const QList<MusicInfo> &songs);
    void favoriteRequested(int musicId);
    void playPauseRequested();
    void backRequested();
    void refreshSidebarPlaylists();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void applyPageStyle();
    void applyFilter();
    void updateHeaderMeta();
    void updateCoverImage();
    void setHeaderCompact(bool compact);
    void onListScrolled(int scrollTop);
    int currentPlayingMusicId() const;
    void showPageStatus(const QString &text, const char *iconName = nullptr);
    void hidePageStatus();
    void reloadPlaylist();
    void editPlaylistDescription();
    void updateActionButtons();
    void updateCollectPlaylistButton();
    void refreshPlaylistCollectedState();
    void toggleCollectPlaylist();
    void showSongContextMenu(const MusicInfo &info, const QPoint &globalPos);
    void removeSongFromPlaylist(const MusicInfo &info);
    int currentUserId() const;

    ApiClient *m_apiClient = nullptr;
    int m_playlistId = 0;
    int m_firstMusicId = 0;
    int m_creatorId = 0;
    bool m_isUserPlaylist = false;
    bool m_isPlaylistCollected = false;
    QString m_playlistName;
    QString m_playlistDesc;
    QString m_creatorUsername;

    QWidget *m_detailHeader = nullptr;
    QWidget *m_coverWrap = nullptr;
    RoundCoverLabel *m_coverImg = nullptr;
    RoundCoverLabel *m_coverShadow = nullptr;
    QWidget *m_coverMask = nullptr;
    QWidget *m_playCountRow = nullptr;
    QLabel *m_playCountLbl = nullptr;
    QLabel *m_titleLbl = nullptr;
    QWidget *m_metaRow = nullptr;
    QLabel *m_creatorLbl = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_downloadAllBtn = nullptr;
    QPushButton *m_editPlaylistBtn = nullptr;
    QPushButton *m_collectPlaylistBtn = nullptr;
    QPushButton *m_moreBtn = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QWidget *m_searchWrap = nullptr;

    SongListWidget *m_songList = nullptr;
    QWidget *m_emptyWrap = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_emptyIcon = nullptr;

    QVariantAnimation *m_headerAnim = nullptr;
    bool m_headerCompact = false;

    /** 搜索框防抖：几万首的歌单下每敲一个字都重建列表会明显卡顿。 */
    QTimer *m_searchDebounce = nullptr;
    /** 当前歌单的滚动位置，切走再回来时还原。 */
    int m_savedScrollTop = 0;
    int m_savedScrollPlaylistId = -1;
    /** 本次加载要还原的滚动位置（0 表示回到顶部）。 */
    int m_pendingScrollTop = 0;

    QList<MusicInfo> m_allSongs;
    QList<MusicInfo> m_displaySongs;
    QString m_coverMusicId;
    QSet<int> m_favoritedIds;
};
