#pragma once

/**
 * @file songcontextmenu.h
 * @brief 歌曲右键菜单（自定义浮层，非 QMenu）
 */

#include <QWidget>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <functional>

class QCloseEvent;
class QVariantAnimation;

class SongContextMenuPopup : public QWidget
{
    Q_OBJECT

public:
    struct Entry {
        const char *iconName = nullptr;
        QString label;
        std::function<void()> action;
        /**
         * 置灰、不可点击（批量操作浮层里「选中 0 首」时的动作项用）。
         * 默认 false，现有各处 Menu 项的构造方式完全不受影响。
         */
        bool disabled = false;
    };

    /** 在 globalPos 显示菜单；anchor 用于 Popup 父级与主题 */
    static void showAt(QWidget *anchor, const QPoint &globalPos, const QList<Entry> &entries);

    /**
     * 同上，但把新建出来的浮层交回给调用方。
     *
     * 调用方需要持有它才能在**不关窗**的前提下原地换内容（见 setEntries），
     * 用于「点了全选，计数/文案/置灰状态要立刻刷新」这类场景。
     * 浮层带 WA_DeleteOnClose，调用方请用 QPointer 持有。
     */
    static SongContextMenuPopup *showAt(QWidget *anchor, const QPoint &globalPos,
                                        const QList<Entry> &entries, bool returnInstance);

    /**
     * 原地换掉菜单内容：保留同一个 Popup 窗口（不关闭、不重新 new），
     * 重排行部件、重算宽高，并重新做一次边界收敛，保证换完仍然完整可见。
     */
    void setEntries(const QList<Entry> &entries);

    /** 以 globalPos 为左上角落位并做边界收敛（含下边界翻转到锚点上方） */
    void placeAt(const QPoint &globalPos);

    /** 在当前几何下重新夹进屏幕可用区域（不改锚点，供 setEntries 后调用） */
    void clampToScreen();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    explicit SongContextMenuPopup(QWidget *anchor, const QList<Entry> &entries);
    ~SongContextMenuPopup() override = default;

    void applyTheme();
    void refreshGlassBackdrop();
    void animateGlassOpen();
    void animateGlass(qreal end);
    /** 按给定 entries 重建行部件并重算固定宽高（构造与 setEntries 共用） */
    void applyEntriesToLayout(const QList<Entry> &entries);

    QPointer<QWidget> m_anchor;
    QWidget *m_panel = nullptr;
    class QVBoxLayout *m_panelLay = nullptr;
    QVariantAnimation *m_glassAnimation = nullptr;
    qreal m_glassProgress = 1.0;
    bool m_glassClosing = false;
    bool m_closeReady = false;
    bool m_openUpwards = false;
    QList<Entry> m_entries;
    QList<QWidget *> m_rowWidgets;
};
