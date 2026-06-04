#pragma once

/**
 * @file sidebar.h
 * @brief 侧边栏导航 — 日系动漫风
 *
 * 240px 紧凑宽度，重度毛玻璃。
 * 选中项薰衣草紫背景 + 左侧薄荷绿竖条指示器。
 */

#include <QWidget>
#include <QMap>
#include <QIcon>

class QPushButton;
class QVBoxLayout;
class QWidget;
class PlaylistListItem;
class ApiClient;

struct ApiPlaylistInfo {
    int id = 0;
    QString name;
    QString description;
    QString coverUrl;
    int musicCount = 0;
};

class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(ApiClient *apiClient, QWidget *parent = nullptr);
    void setActiveNav(const QString &key);
    void retranslate();
    void refreshPlaylists();
    void loadPlaylists();

signals:
    void navigationRequested(const QString &key);
    void playlistClicked(int playlistId);
    void playlistCreateRequested();
    void neteaseImportRequested();
    void qqImportRequested();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void setupUi();
    QPushButton *createNavItem(const QString &key, const QString &label, const QIcon &icon);
    void refreshPlaylistList();
    void loadFavPlaylists();
    void refreshFavPlaylistList();

    ApiClient *m_apiClient = nullptr;
    QList<ApiPlaylistInfo> m_apiPlaylists;
    QMap<QString, QPushButton *> m_navBtns;
    QString m_activeKey;
    QPushButton *m_favBtn = nullptr;
    QPushButton *m_recBtn = nullptr;
    QPushButton *m_searchBtn = nullptr;

    QWidget *m_playlistContainer = nullptr;
    QVBoxLayout *m_playlistLayout = nullptr;
    QList<PlaylistListItem *> m_playlistItems;
    QPushButton *m_createPlaylistBtn = nullptr;
    QPushButton *m_importNeteaseBtn = nullptr;
    QPushButton *m_importQqBtn = nullptr;

    // 收藏的歌单区域
    QWidget *m_favPlaylistContainer = nullptr;
    QVBoxLayout *m_favPlaylistLayout = nullptr;
    QList<PlaylistListItem *> m_favPlaylistItems;
    QList<ApiPlaylistInfo> m_favPlaylists;
};
