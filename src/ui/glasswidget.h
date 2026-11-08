#pragma once

/**
 * @file glasswidget.h
 * @brief 圆角雾面容器，复用窗口背景缓存，不对控件树递归截图。
 */

#include <QColor>
#include <QWidget>

class GlassPlate;

class GlassWidget : public QWidget
{
    Q_OBJECT
    friend class GlassPlate;

public:
    explicit GlassWidget(QWidget *parent = nullptr);
    ~GlassWidget() override;

    qreal opacity() const;
    void setOpacity(qreal v);

    QColor baseColor() const;
    void setBaseColor(const QColor &c);

    QColor borderColor() const;
    void setBorderColor(const QColor &c);

    int borderRadius() const;
    void setBorderRadius(int r);

    /** 背景或主题变化后请求重绘。 */
    void refreshBackdrop();

    /** 将子控件布局在此层之上（勿对 GlassWidget 再 setLayout，否则会顶掉底层玻璃绘制层） */
    QWidget *contentWidget() const { return m_content; }

    /** 为 true 时采样共享背景缓存；false 时仅绘制指定底色。 */
    void setBackdropCaptureEnabled(bool enabled);
    bool backdropCaptureEnabled() const { return m_backdropCaptureEnabled; }

private:
    GlassPlate *m_plate = nullptr;
    QWidget *m_content = nullptr;

    bool m_backdropCaptureEnabled = false;

    qreal m_opacity = 0.65;
    QColor m_base{26, 26, 46};
    QColor m_border{255, 183, 197, 70};
    int m_radius = 16;
};
