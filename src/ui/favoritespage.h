#pragma once

#include <QWidget>
#include <QList>

#include "core/musicinfo.h"

class QLabel;
class QPushButton;
class QLineEdit;
class RoundCoverLabel;
class QVariantAnimation;
class SongListWidget;
class ApiClient;
class BatchActionBar;

/** 「我喜欢的音乐」— 1:1 SPlayer liked.vue (ListDetail + SongList + SongCard) */
class FavoritesPage : public QWidget
{
    Q_OBJECT

public:
    explicit FavoritesPage(ApiClient *apiClient, QWidget *parent = nullptr);

    void retranslate();
    void refresh();
    void setPlaybackPaused(bool paused);
    void updatePlayingHighlight();
    void refreshDownloadDisplay();

signals:
    void playRequested(int musicId, const QString &title, const QString &artist, const QString &coverUrl);
    void playAllRequested(const QList<MusicInfo> &results);
    void downloadRequested(const MusicInfo &info);
    void downloadAllRequested(const QList<MusicInfo> &results);
    void unfavoriteRequested(int musicId);
    void playPauseRequested();
    /** 批量操作：加入歌单 / 批量下载 / 批量收藏切换（该页语义为取消收藏） */
    void batchAddToPlaylistRequested(const QList<MusicInfo> &songs);
    void batchDownloadRequested(const QList<MusicInfo> &songs);
    void batchFavoriteToggleRequested(const QList<int> &musicIds);

protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void applyPageStyle();
    void loadFavorites();
    void applyFilter();
    void updateHeaderMeta();
    void updateCoverImage();
    void setHeaderCompact(bool compact);
    void onListScrolled(int scrollTop);
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

    ApiClient *m_apiClient = nullptr;

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
    QPushButton *m_moreBtn = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QWidget *m_searchWrap = nullptr;
    /** 批量模式：「播放 / 更多」那排按钮整体让位给批量操作条 */
    QWidget *m_menuRow = nullptr;
    BatchActionBar *m_batchBar = nullptr;

    SongListWidget *m_songList = nullptr;
    QWidget *m_emptyWrap = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_emptyIcon = nullptr;

    QVariantAnimation *m_headerAnim = nullptr;
    bool m_headerCompact = false;

    QList<MusicInfo> m_allFavorites;
    QList<MusicInfo> m_displaySongs;
    QString m_coverMusicId;
};
