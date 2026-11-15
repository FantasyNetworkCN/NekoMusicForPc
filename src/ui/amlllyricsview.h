#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

#include "core/lyrics.h"

#include <memory>

class QImage;
class QPainter;
class QPointF;

/**
 * AMLL 风格歌词画布的原生 Qt 实现。
 *
 * <p>对齐 Web 端 @applemusic-like-lyrics/vue 的 LyricPlayer 行为：
 * 跟随焦点弹簧滚动、滞后的高亮生命周期、逐词上浮/扫光、行级缩放与
 * 距离模糊；同时提供悬停、点击跳转、滚轮/触摸惯性与间奏圆点。</p>
 */
class AmllLyricsView final : public QWidget {
    Q_OBJECT

public:
    explicit AmllLyricsView(QWidget *parent = nullptr);
    ~AmllLyricsView() override;

    void setLyrics(const QVector<LyricLine> &lyrics);
    void setPosition(qint64 positionMs, bool seek = false);
    void setPlaying(bool playing);
    void setTextStyle(const QColor &active, const QColor &dim,
                      const QColor &secondary, int mainFontPx);
    void setEmptyText(const QString &text);
    void resetView();

signals:
    /** 用户点击某一行歌词，请求跳转到该行起点（毫秒）。 */
    void seekRequested(qint64 positionMs);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool event(QEvent *event) override;

private:
    void rebuildLayout(bool preservePosition);
    bool updateTimeline(qint64 positionMs, bool seek);
    void updateLineTargets(bool stagger, bool seeking);
    QImage sharpLineImage(int index);
    void drawKaraokeLine(QPainter &painter, int index);
    int lineIndexAt(const QPointF &position) const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
