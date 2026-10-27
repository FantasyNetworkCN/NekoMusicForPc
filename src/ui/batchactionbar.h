#pragma once

/**
 * @file batchactionbar.h
 * @brief 单曲列表批量编辑模式的操作条（已选 N 首 / 全选 / 批量操作 / 退出）
 *
 * 设计约定：
 *   - 与页面顶部原有按钮排（播放 / 下载全部 / 更多 ⋯）占同一行位置，
 *     进入批量模式时隐藏原按钮排、显示本操作条，退出时反向切换；
 *   - 按钮按页面能力显示（canXxx = false 的整个不显示），
 *     选中数量为 0 时统一置灰，避免出现点了没反应的死按钮；
 *   - 文案全部走 I18n，三张语言表都有对应键。
 */

#include <QWidget>
#include <QHash>

class QLabel;
class QPushButton;

class BatchActionBar : public QWidget
{
    Q_OBJECT

public:
    explicit BatchActionBar(QWidget *parent = nullptr);

    /** 页面能力：false 的按钮不创建/不显示 */
    void setCapabilities(bool canAddToPlaylist, bool canAddToQueue, bool canDownload,
                         bool canUnfavorite, bool canRemoveFromPlaylist);

    /**
     * 复用同一个按钮但换个说法（如播放队列抽屉里「移出歌单」语义为「移出播放队列」）。
     * @param actionKey "removeFromPlaylist" / "addToPlaylist" / "addToQueue" /
     *                  "download" / "unfavorite"
     */
    void setActionText(const char *actionKey, const QString &text);

    /**
     * 紧凑触发模式：横向动作按钮全部收起，只留「已选 N 首」计数 + 一个
     * 「批量操作 ▾」触发按钮（点它触发 actionsRequested()，由调用方弹竖排浮层）。
     * 给 400px 宽的播放队列抽屉这类窄容器用，避免横向文案被压扁截断。
     * 普通列表页用 setActionMenuMode()，让本组件自己弹菜单。
     */
    void setCompactTrigger(bool compact);
    bool compactTrigger() const { return m_compactTrigger; }
    /**
     * 操作菜单模式：横排动作按钮收进「批量操作」浮层，
     * 只保留计数、全选、触发按钮和退出。适合普通列表页的批量工具条。
     */
    void setActionMenuMode(bool enabled);
    bool actionMenuMode() const { return m_actionMenuMode; }
    /** 批量操作浮层的触发按钮（普通模式下为 nullptr） */
    QPushButton *triggerButton() const { return m_triggerBtn; }

    /** 已选数量：更新「已选 N 首」文案与各按钮可用状态 */
    void setSelectedCount(int count);
    /** 全选 / 取消全选按钮文案跟随当前是否已全选 */
    void setAllSelected(bool allSelected);
    /** 退出批量模式时复位（数量归零、全选态复位） */
    void reset()
    {
        // 顺序要紧：先归零数量，再复位全选态。反过来的话 setAllSelected→retranslate
        // 会用旧的 m_selectedCount 把「批量操作 (N) ▾」写回去，退出后残留旧数字。
        setSelectedCount(0);
        setAllSelected(false);
    }

    void retranslate();
    void applyTheme();

signals:
    void addToPlaylistRequested();
    void addToQueueRequested();
    void downloadRequested();
    void unfavoriteRequested();
    void removeFromPlaylistRequested();
    void selectAllToggled();
    void exitRequested();
    /** 紧凑模式下点了「批量操作 ▾」触发按钮：调用方负责弹出竖排浮层 */
    void actionsRequested();

private:
    QPushButton *makeActionButton(const char *iconName, const char *textKey);
    void rebuildLayout();
    void showActionMenu();
    /** 同步「已选 N 首」的两处显示：横排计数标签 + 触发按钮 */
    void updateCountLabels();

    QLabel *m_countLbl = nullptr;
    QPushButton *m_selectAllBtn = nullptr;
    QPushButton *m_addToPlaylistBtn = nullptr;
    QPushButton *m_addToQueueBtn = nullptr;
    QPushButton *m_downloadBtn = nullptr;
    QPushButton *m_unfavoriteBtn = nullptr;
    QPushButton *m_removeFromPlaylistBtn = nullptr;
    QPushButton *m_exitBtn = nullptr;
    /** 批量操作浮层的触发按钮（紧凑/操作菜单模式下存在） */
    QPushButton *m_triggerBtn = nullptr;

    int m_selectedCount = 0;
    bool m_allSelected = false;
    bool m_compactTrigger = false;
    bool m_actionMenuMode = false;
    bool m_canAddToPlaylist = false;
    bool m_canAddToQueue = false;
    bool m_canDownload = false;
    bool m_canUnfavorite = false;
    bool m_canRemoveFromPlaylist = false;
    /** actionKey → 覆盖文案；非空时 retranslate() 不覆盖它（语言切换后仍按调用方给的文案） */
    QHash<QString, QString> m_textOverrides;
};
