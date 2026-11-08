#pragma once

/**
 * @file playlistcard.h
 * @brief 歌单卡片 — 网格布局版
 *
 * 170x220，圆角封面由 paintEvent 绘制 + 歌单名。
 * 悬停薄荷绿光晕边框 + 平滑上浮动画。
 */

#include <QWidget>
#include <QLabel>
#include <QPixmap>
#include <QMetaObject>

struct PlaylistInfo {
    int id = -1;
    QString name;
    QString description;
    int musicCount = 0;
    QString coverUrl;
};

class PlaylistCard : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    explicit PlaylistCard(const PlaylistInfo &info, QWidget *parent = nullptr);
    int playlistId() const;
    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal value) { m_hoverProgress = value; update(); }

signals:
    void clicked(int id);

protected:
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void changeEvent(QEvent *) override;

private:
    void loadCover();
    void animateHover(bool enter);

    PlaylistInfo m_info;
    QLabel *m_name       = nullptr;
    QPixmap m_coverPixmap;
    bool m_hovered       = false;
    qreal m_hoverProgress = 0;
    class QPropertyAnimation *m_hoverAnimation = nullptr;
    QMetaObject::Connection m_coverConn;
};
