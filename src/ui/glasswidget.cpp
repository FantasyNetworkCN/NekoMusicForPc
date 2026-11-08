#include "glasswidget.h"
#include "glasspaint.h"
#include "theme/thememanager.h"

#include <QPainter>
#include <QPainterPath>
#include <QStackedLayout>

// 只采样预先柔化的窗口背景，滚动时仅改变采样坐标。
// 不隐藏控件或递归截图，避免重入绘制、布局抖动和正文一起模糊。
class GlassPlate final : public QWidget
{
public:
    explicit GlassPlate(GlassWidget *owner) : QWidget(owner), m_owner(owner)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF bounds = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
        const qreal radius = qMin(qreal(m_owner->m_radius), bounds.height() / 2);
        QPainterPath shape;
        shape.addRoundedRect(bounds, radius, radius);
        p.setClipPath(shape);
        if (m_owner->m_backdropCaptureEnabled)
            GlassPaint::paintCachedBackdrop(p, this, rect());
        QColor fill = m_owner->m_base;
        fill.setAlphaF(fill.alphaF() * qBound(0.0, m_owner->m_opacity, 1.0));
        p.fillPath(shape, fill);
        if (m_owner->m_backdropCaptureEnabled) {
            QLinearGradient sheen(bounds.topLeft(), bounds.bottomRight());
            sheen.setColorAt(0, QColor(255, 255, 255,
                Theme::ThemeManager::instance().isDarkMode() ? 9 : 36));
            sheen.setColorAt(1, Qt::transparent);
            p.fillPath(shape, sheen);
        }
        p.setClipping(false);
        p.setPen(QPen(m_owner->m_border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(shape);
    }
private:
    GlassWidget *m_owner;
};

GlassWidget::GlassWidget(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    m_plate = new GlassPlate(this);
    m_content = new QWidget(this);
    m_content->setAttribute(Qt::WA_TranslucentBackground);
    m_content->setAutoFillBackground(false);
    auto *stack = new QStackedLayout(this);
    stack->setStackingMode(QStackedLayout::StackAll);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->addWidget(m_plate);
    stack->addWidget(m_content);
    stack->setCurrentWidget(m_content);
    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged,
            this, [this](Theme::ThemeMode) { refreshBackdrop(); });
}

GlassWidget::~GlassWidget() = default;
qreal GlassWidget::opacity() const { return m_opacity; }
QColor GlassWidget::baseColor() const { return m_base; }
QColor GlassWidget::borderColor() const { return m_border; }
int GlassWidget::borderRadius() const { return m_radius; }

void GlassWidget::setOpacity(qreal value)
{
    m_opacity = qBound(0.0, value, 1.0);
    refreshBackdrop();
}
void GlassWidget::setBaseColor(const QColor &color)
{
    m_base = color;
    refreshBackdrop();
}
void GlassWidget::setBorderColor(const QColor &color)
{
    m_border = color;
    refreshBackdrop();
}
void GlassWidget::setBorderRadius(int radius)
{
    m_radius = qMax(0, radius);
    refreshBackdrop();
}
void GlassWidget::setBackdropCaptureEnabled(bool enabled)
{
    m_backdropCaptureEnabled = enabled;
    refreshBackdrop();
}
void GlassWidget::refreshBackdrop()
{
    if (m_plate)
        m_plate->update();
    update();
}
