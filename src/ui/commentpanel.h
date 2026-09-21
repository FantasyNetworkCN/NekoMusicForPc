#pragma once

/**
 * @file commentpanel.h
 * @brief 单曲评论抽屉 — 从窗口右侧滑入（外观对齐播放队列抽屉 PlaylistPanel）
 *
 * 数据来自后端单端点 `/api/comments`：GET 拉楼层（含回复）/ POST 发表与回复 / DELETE 删除。
 * 每条评论展示头像、昵称、时间与 IP 归属地；回复再回复仍归入同一楼层并标记 @ 对象。
 *
 * 交互：标题行提供「刷新」与「关闭」；底部为发表区；切歌后由宿主调用
 * showMusicComments() 自动加载新歌曲的评论（同一首歌不重复请求）。
 */

#include <QWidget>
#include <QVariantMap>
#include <QVariantList>

class ApiClient;
class QScrollArea;
class QVBoxLayout;
class QLabel;
class QPushButton;
class QTextEdit;
class QNetworkAccessManager;
class QPropertyAnimation;
class QGraphicsDropShadowEffect;

class CommentPanel : public QWidget {
    Q_OBJECT

public:
    explicit CommentPanel(ApiClient *api, QWidget *parent = nullptr);

    static constexpr int kDrawerWidth = 400;

    bool isDrawerOpen() const { return m_drawerOpen; }
    /** 宿主尺寸变化时保持贴右 */
    void syncToHost();
    /** 换语言后刷新文案 */
    void retranslate();
    /** 打开并展示指定歌曲的评论 */
    void openFor(int musicId);

signals:
    void hideRequested();
    void drawerClosed();
    void commentCountChanged(int count);

public slots:
    void openDrawer();
    void closeDrawer();
    void togglePanel();
    /** 重新拉取第一页 */
    void reload();
    /** 换曲：切换评论目标并自动加载（不改动抽屉显隐） */
    void showMusicComments(int musicId);
    /** 刷新按钮：强制重新拉取当前歌曲评论 */
    void refreshComments();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi();
    void applyPanelChrome();
    void rebuildList();
    void addCommentCard(QVBoxLayout *layout, const QVariantMap &comment, int depth);
    void loadMore();
    void submit();
    void startReply(int commentId, const QString &nickname);
    void cancelReply();
    void removeComment(int commentId);
    void loadAvatar(QLabel *label, int userId, int px);
    void updateComposerState();
    void clearList();
    void setStatusText(const QString &text);
    void layoutOverlay();

    static QString formatTime(const QString &raw);
    static QPixmap roundedAvatar(const QImage &source, int px);

    ApiClient *m_api = nullptr;
    int m_musicId = 0;
    int m_page = 1;
    int m_floorTotal = 0;
    int m_commentTotal = 0;
    bool m_hasMore = false;
    bool m_loading = false;
    bool m_submitting = false;
    int m_replyId = 0;
    QString m_replyName;
    int m_scrollGeneration = 0;
    QVariantList m_topComments;

    QLabel *m_titleLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QWidget *m_replyChip = nullptr;
    QLabel *m_replyChipLabel = nullptr;
    QWidget *m_inputBox = nullptr;
    QTextEdit *m_input = nullptr;
    QLabel *m_counterLabel = nullptr;
    QPushButton *m_submitBtn = nullptr;
    QWidget *m_loginHint = nullptr;
    QLabel *m_loginHintLabel = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_listContainer = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QWidget *m_itemsHost = nullptr;
    QVBoxLayout *m_itemsLayout = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_moreBtn = nullptr;

    QNetworkAccessManager *m_avatarNam = nullptr;

    bool m_drawerOpen = false;
    bool m_animating = false;
    QPropertyAnimation *m_slideAnim = nullptr;
    QGraphicsDropShadowEffect *m_drawerShadow = nullptr;
};
