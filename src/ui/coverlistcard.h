#pragma once

/**
 * @file coverlistcard.h
 * @brief SPlayer CoverList.vue — playlist 类型封面卡片
 */

#include <QWidget>
#include <QString>
#include <QPixmap>
#include <QMetaObject>

#include "ui/liquidglass.h"

class QLabel;
class QPushButton;

struct CoverListItemData {
    int id = 0;
    QString name;
    QString description;
    QString creator;
    int musicCount = 0;
    QString coverUrl;
};

/** 自适应列宽；高度 = 封面正方形 + 文案区 */
class CoverListCard : public QWidget
{
    Q_OBJECT

public:
    explicit CoverListCard(const CoverListItemData &data, QWidget *parent = nullptr);
    int playlistId() const { return m_data.id; }
    void setCellWidth(int width);
    void applyTheme();

signals:
    void clicked(int playlistId);
    void playClicked(int playlistId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void loadCover();
    void updateCoverGeometry();
    void setHovered(bool hovered);
    void updatePlayButtonStyle();

    CoverListItemData m_data;
    QPixmap m_coverPixmap;
    bool m_hovered = false;
    int m_cellWidth = 160;
    int m_coverSide = 160;

    QWidget *m_coverWrap = nullptr;
    QLabel *m_playCountRow = nullptr;
    QLabel *m_descOverlay = nullptr;
    QPushButton *m_playBtn = nullptr;
    QLabel *m_nameLbl = nullptr;
    QLabel *m_creatorLbl = nullptr;
    QMetaObject::Connection m_coverConn;
    qreal m_liquidProgress = 0;
    LiquidGlass::Surface m_liquidSurface;
};

/** 供搜索页网格在 resize 时重排 */
class CoverGridHost : public QWidget
{
    Q_OBJECT

public:
    explicit CoverGridHost(QWidget *parent = nullptr);
    std::function<void()> onResized;

protected:
    void resizeEvent(QResizeEvent *event) override;
};
