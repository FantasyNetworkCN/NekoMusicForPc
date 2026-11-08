#pragma once

#include <QLayout>
#include <QWidget>

/** 固定宽度卡片自动换行，向滚动区报告实际高度，窄窗口不会裁掉后面的卡片。 */
class FlowLayout final : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent, int gap = 16) : QLayout(parent), m_gap(gap)
    {
        setContentsMargins(0, 0, 0, 0);
    }
    ~FlowLayout() override
    {
        while (auto *item = takeAt(0))
            delete item;
    }
    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize minimumSize() const override
    {
        QSize size;
        for (auto *item : m_items)
            size = size.expandedTo(item->minimumSize());
        return size;
    }
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }
private:
    int arrange(const QRect &rect, bool apply) const
    {
        int x = rect.x(), y = rect.y(), rowHeight = 0;
        for (auto *item : m_items) {
            const QSize size = item->sizeHint();
            if (x > rect.x() && x + size.width() > rect.x() + rect.width()) {
                x = rect.x();
                y += rowHeight + m_gap;
                rowHeight = 0;
            }
            if (apply)
                item->setGeometry(QRect(QPoint(x, y), size));
            x += size.width() + m_gap;
            rowHeight = qMax(rowHeight, size.height());
        }
        return y + rowHeight - rect.y();
    }
    QList<QLayoutItem *> m_items;
    int m_gap;
};
