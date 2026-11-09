#ifndef PLAYLISTLISTITEM_H
#define PLAYLISTLISTITEM_H

#include <QWidget>
#include <QLabel>
#include <QMetaObject>

class RoundCoverLabel;

class PlaylistListItem : public QWidget {
    Q_OBJECT

public:
    enum Mode { UserPlaylist, FavoritePlaylist };
    explicit PlaylistListItem(int playlistId, const QString& name, int musicCount, const QString& coverUrl, Mode mode = UserPlaylist, QWidget *parent = nullptr);

    int playlistId() const { return m_playlistId; }
    void setMusicCount(int count);

signals:
    void clicked(int playlistId);
    void renameRequested(int playlistId);
    void editDescriptionRequested(int playlistId);
    void deleteRequested(int playlistId);
    void unfavoriteRequested(int playlistId);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    int m_playlistId;
    QString m_name;
    int m_musicCount;
    bool m_hovered = false;
    Mode m_mode = UserPlaylist;
    RoundCoverLabel *m_coverLbl = nullptr;
    QLabel *m_nameLbl = nullptr;
    QMetaObject::Connection m_coverConn;
    void setPlaceholderCover();
    /** 按当前可用宽度给歌单名加省略号（否则长名字会被硬切成半个字）。 */
    void applyElidedName();
};

#endif // PLAYLISTLISTITEM_H
