#pragma once

#include <QImage>
#include <QRect>

class QColor;
class QPainter;
class QWidget;

namespace LiquidGlass {

/** 保存应用内原始背景。坐标使用宿主的逻辑像素。 */
class Backdrop
{
public:
    void setImage(const QImage &image, const QRect &bounds);
    bool isEmpty() const { return m_image.isNull(); }

private:
    friend class Surface;
    friend class ControlSurface;
    QRect m_bounds;
    QImage m_image;
};

enum class Material { Chrome, Popup, SwitchTrack, SwitchThumb };

struct ControlRecipe {
    QRectF track;
    QRectF thumb;
    qreal trackRadius = 0;
    qreal thumbRadius = 0;
    qreal pressure = 0;
    bool dark = false;
    bool checked = false;
};

/** 每块玻璃各自缓存合成图，控件的文字和图标仍独立绘制。 */
class Surface
{
public:
    bool paint(QPainter &painter, const QRectF &target, const Backdrop &backdrop,
               const QRectF &sourceArea, qreal radius, bool dark,
               Material material = Material::Chrome, qreal morph = 1.0);
    void clear() { m_image = QImage(); }

private:
    QImage m_image;
    qint64 m_sourceKey = 0;
    QRect m_backdropBounds;
    QRectF m_sourceArea;
    QSizeF m_size;
    qreal m_radius = 0;
    qreal m_scale = 0;
    qreal m_morph = 1;
    bool m_dark = false;
    Material m_material = Material::Chrome;
};

/** 单帧绘制的控件玻璃：距离场和材质合成共享同一份几何与背景。 */
class ControlSurface
{
public:
    bool paint(QPainter &painter, const QRectF &target, const Backdrop &backdrop,
               const QRectF &sourceArea, const ControlRecipe &recipe);
    void clear() { m_image = QImage(); }

private:
    QImage m_image;
    qint64 m_sourceKey = 0;
    QRect m_backdropBounds;
    QRectF m_sourceArea;
    QSizeF m_size;
    QSize m_pixelSize;
    ControlRecipe m_recipe;
    Surface m_fallbackTrack;
    Surface m_fallbackThumb;
};

/** 采集宿主指定区域，stopBefore 非空时只渲染它之下的同级层。 */
QImage capture(QWidget *host, const QRect &region, const QWidget *stopBefore = nullptr);

/** 外壳玻璃共享背景图与 Vulkan 中生成的漫射纹理。 */
const Backdrop &shellBackdrop(const QWidget *widget);

constexpr int kCaptureMargin = 40;

} // namespace LiquidGlass
