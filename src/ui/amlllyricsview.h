#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

#include "core/lyrics.h"

#include <memory>

/**
 * AMLL 风格歌词画布的原生 Qt 实现。
 *
 * <p>不创建逐行 QLabel，而是在单一控件中完成文本排版、弹簧滚动、
 * 行级缩放/透明度与字级高亮，避免滚动时反复触发布局和图形特效。</p>
 */
class AmllLyricsView final : public QWidget {
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

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
