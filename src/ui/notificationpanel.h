#pragma once

/**
 * @file notificationpanel.h
 * @brief 消息中心抽屉 — 从窗口右侧滑入（外观对齐评论 / 播放队列抽屉）
 *
 * 数据来自 `/api/user/notifications`：列表按 id 游标分页，实时增量由 NotificationCenter 的
 * SSE 长连接推来。列表是主路径，SSE 只是在线时提前告知——断线期间的消息在重连后按
 * `latestId` 补拉，因此不会丢。
 *
 * 未读数统一由 NotificationCenter 持有：本面板只负责用响应里的权威值校准它。
 */

#include <QWidget>
#include <QList>
#include <QVariantMap>

class ApiClient;
class QVBoxLayout;
class QScrollArea;
class QLabel;
class QPushButton;
class QPropertyAnimation;
class QGraphicsDropShadowEffect;

class NotificationPanel : public QWidget
{
    Q_OBJECT

public:
    explicit NotificationPanel(ApiClient *api, QWidget *parent = nullptr);

    static constexpr int kDrawerWidth = 380;

    bool isDrawerOpen() const { return m_drawerOpen; }
    /** 宿主尺寸变化时保持贴右 */
    void syncToHost();
    /** 换语言后刷新文案 */
    void retranslate();

signals:
    void hideRequested();
    void drawerClosed();
    /** 点击一条消息（已先标记已读），由宿主决定怎么跳转 */
    void messageActivated(const QVariantMap &item);

public slots:
    void openDrawer();
    void closeDrawer();
    void togglePanel();
    /** 重新拉取第一页 */
    void reload();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void applyPanelChrome();
    void rebuildList();
    void appendCard(const QVariantMap &item);
    void setStatusText(const QString &text);
    void loadMore();
    void markAllRead();
    void activateItem(const QVariantMap &item);
    void prependItem(const QVariantMap &item);
    /** SSE 新消息：插到列表最前；列表没加载过就不管（打开时本来会重拉） */
    void onStreamMessage(const QVariantMap &item);
    /** 重连后 `latestId` 领先本地游标：补拉漏掉的消息 */
    void onStreamReady(int unread, int latestId);
    int newestId() const;
    int oldestId() const;
    void updateHeaderState();

    ApiClient *m_api = nullptr;

    QVBoxLayout *m_listLayout = nullptr;
    QWidget *m_listContainer = nullptr;
    QScrollArea *m_scroll = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_markAllBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QPushButton *m_moreBtn = nullptr;

    QList<QVariantMap> m_items;
    int m_unread = 0;
    bool m_hasMore = false;
    bool m_loading = false;
    bool m_loaded = false;
    bool m_drawerOpen = false;
    bool m_animating = false;
    QPropertyAnimation *m_slideAnim = nullptr;
    QGraphicsDropShadowEffect *m_drawerShadow = nullptr;
};
