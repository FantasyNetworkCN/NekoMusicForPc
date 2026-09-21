#include "ui/commentpanel.h"
#include "core/apiclient.h"
#include "core/usermanager.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/scrollareafix.h"
#include "ui/svgicon.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QPainter>
#include <QPainterPath>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsDropShadowEffect>
#include <QResizeEvent>
#include <QEnterEvent>
#include <QFrame>
#include <QSignalBlocker>
#include <QPixmap>
#include <QScrollBar>
#include <QDateTime>
#include <QUrl>

namespace {

constexpr int kMaxLength = 500;
constexpr int kPageSize = 20;
constexpr int kListPad = 16;
constexpr int kReplyIndent = 44;
constexpr int kCardRadius = 8;

/** 与播放队列抽屉同一主色（Theme 未暴露 accent 常量） */
constexpr QColor kPrimary(230, 57, 80);

bool isDark() {
    return Theme::ThemeManager::instance().isDarkMode();
}

QString themeTextMain() {
    return isDark() ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
}

QString themeTextSub() {
    return isDark() ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.62)");
}

QString themeTextFaint() {
    return isDark() ? QStringLiteral("rgba(255,255,255,0.42)") : QStringLiteral("rgba(33,37,41,0.42)");
}

QString themeAccent() {
    return QString::fromUtf8(Theme::kLavenderLt);
}

/** 抽屉底色：暗色为 kBgSurface，亮色为白（与主界面一致） */
QColor themePanelBg() {
    return isDark() ? QColor(QString::fromUtf8(Theme::kBgSurface)) : QColor(255, 255, 255);
}

QColor themePanelBorder() {
    return isDark() ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 30);
}

/** 半透明浅底：暗色用白、亮色用黑，避免亮色下白底叠白底看不见 */
QString themeSoftFill(int darkAlpha, int lightAlpha) {
    return isDark() ? QStringLiteral("rgba(255,255,255,%1)").arg(darkAlpha)
                    : QStringLiteral("rgba(33,37,41,%1)").arg(lightAlpha);
}

// ─── 评论卡片：对齐播放队列 .song-node 的圆角底 + hover 描边 ─────────
class CommentCardFrame : public QWidget {
public:
    explicit CommentCardFrame(int alpha, QWidget *parent = nullptr)
        : QWidget(parent), m_alpha(alpha)
    {
        setAttribute(Qt::WA_StyledBackground, false);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(rect(), kCardRadius, kCardRadius);
        p.fillPath(path, QColor(kPrimary.red(), kPrimary.green(), kPrimary.blue(), m_alpha));
        if (m_hover)
            p.strokePath(path, QPen(kPrimary, 1.0));
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hover = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hover = false;
        update();
        QWidget::leaveEvent(event);
    }

private:
    int m_alpha;
    bool m_hover = false;
};

} // namespace

CommentPanel::CommentPanel(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api)
{
    setObjectName(QStringLiteral("commentPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kDrawerWidth);

    m_avatarNam = new QNetworkAccessManager(this);

    m_drawerShadow = new QGraphicsDropShadowEffect(this);
    m_drawerShadow->setBlurRadius(28);
    m_drawerShadow->setOffset(-6, 0);
    m_drawerShadow->setColor(QColor(0, 0, 0, 100));
    setGraphicsEffect(m_drawerShadow);

    setupUi();
    applyPanelChrome();
    syncToHost();
    hide();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) {
                applyPanelChrome();
                rebuildList();
            });
}

void CommentPanel::setupUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ─── 标题行（对齐播放队列抽屉）─────────────
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("cmtHeader"));
    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(16, 16, 12, 8);
    headerLay->setSpacing(8);

    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(0);
    m_titleLabel = new QLabel(QStringLiteral("评论"), header);
    m_countLabel = new QLabel(header);
    titleCol->addWidget(m_titleLabel);
    titleCol->addWidget(m_countLabel);
    headerLay->addLayout(titleCol, 1);

    m_refreshBtn = new QPushButton(header);
    m_refreshBtn->setFixedSize(32, 32);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setFlat(true);
    m_refreshBtn->setToolTip(QStringLiteral("刷新"));
    connect(m_refreshBtn, &QPushButton::clicked, this, &CommentPanel::refreshComments);
    headerLay->addWidget(m_refreshBtn, 0, Qt::AlignTop);

    m_closeBtn = new QPushButton(header);
    m_closeBtn->setFixedSize(32, 32);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setFlat(true);
    m_closeBtn->setToolTip(QStringLiteral("关闭"));
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        emit hideRequested();
    });
    headerLay->addWidget(m_closeBtn, 0, Qt::AlignTop);

    lay->addWidget(header);

    // ─── 评论列表 ─────────────────────────────
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("cmtScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_listContainer = new QWidget(m_scroll);
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(kListPad, 4, kListPad, 4);
    m_listLayout->setSpacing(10);

    m_itemsHost = new QWidget(m_listContainer);
    m_itemsHost->setAttribute(Qt::WA_TranslucentBackground, true);
    m_itemsLayout = new QVBoxLayout(m_itemsHost);
    m_itemsLayout->setContentsMargins(0, 0, 0, 0);
    m_itemsLayout->setSpacing(10);
    m_listLayout->addWidget(m_itemsHost);

    m_moreBtn = new QPushButton(m_listContainer);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setMinimumHeight(36);
    connect(m_moreBtn, &QPushButton::clicked, this, &CommentPanel::loadMore);
    m_listLayout->addWidget(m_moreBtn);
    m_listLayout->addStretch(1);

    m_statusLabel = new QLabel(m_listContainer);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_statusLabel->hide();

    m_scroll->setWidget(m_listContainer);
    nekoPolishScrollAreaViewport(m_scroll);
    lay->addWidget(m_scroll, 1);

    // ─── 底部发表区 ───────────────────────────
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("cmtFooter"));
    auto *footerLay = new QVBoxLayout(footer);
    footerLay->setContentsMargins(16, 8, 16, 16);
    footerLay->setSpacing(8);

    m_loginHint = new QWidget(footer);
    auto *hintLay = new QHBoxLayout(m_loginHint);
    hintLay->setContentsMargins(0, 0, 0, 0);
    m_loginHintLabel = new QLabel(m_loginHint);
    m_loginHintLabel->setAlignment(Qt::AlignCenter);
    hintLay->addWidget(m_loginHintLabel);
    m_loginHint->hide();
    footerLay->addWidget(m_loginHint);

    m_inputBox = new QWidget(footer);
    m_inputBox->setObjectName(QStringLiteral("cmtInputBox"));
    auto *inputLay = new QVBoxLayout(m_inputBox);
    inputLay->setContentsMargins(10, 8, 10, 8);
    inputLay->setSpacing(6);

    m_replyChip = new QWidget(m_inputBox);
    auto *chipLay = new QHBoxLayout(m_replyChip);
    chipLay->setContentsMargins(0, 0, 0, 0);
    chipLay->setSpacing(6);
    m_replyChipLabel = new QLabel(m_replyChip);
    auto *chipCancel = new QPushButton(QStringLiteral("✕"), m_replyChip);
    chipCancel->setFlat(true);
    chipCancel->setFixedSize(18, 18);
    chipCancel->setCursor(Qt::PointingHandCursor);
    connect(chipCancel, &QPushButton::clicked, this, &CommentPanel::cancelReply);
    chipLay->addWidget(m_replyChipLabel);
    chipLay->addWidget(chipCancel);
    chipLay->addStretch(1);
    m_replyChip->hide();
    inputLay->addWidget(m_replyChip);

    m_input = new QTextEdit(m_inputBox);
    m_input->setObjectName(QStringLiteral("cmtInput"));
    m_input->setFixedHeight(58);
    m_input->setAcceptRichText(false);
    connect(m_input, &QTextEdit::textChanged, this, &CommentPanel::updateComposerState);
    inputLay->addWidget(m_input);

    auto *foot = new QHBoxLayout();
    foot->setSpacing(8);
    m_counterLabel = new QLabel(m_inputBox);
    m_submitBtn = new QPushButton(m_inputBox);
    m_submitBtn->setCursor(Qt::PointingHandCursor);
    m_submitBtn->setFixedHeight(28);
    connect(m_submitBtn, &QPushButton::clicked, this, &CommentPanel::submit);
    foot->addWidget(m_counterLabel);
    foot->addStretch(1);
    foot->addWidget(m_submitBtn);
    inputLay->addLayout(foot);
    footerLay->addWidget(m_inputBox);

    lay->addWidget(footer);

    retranslate();
}

void CommentPanel::applyPanelChrome()
{
    const bool dark = isDark();
    const QString main = themeTextMain();
    const QString sub = themeTextSub();
    const QString faint = themeTextFaint();

    if (m_titleLabel)
        m_titleLabel->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 16px; font-weight: 700; color: %1; }").arg(main));
    if (m_countLabel)
        m_countLabel->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 12px; color: %1; margin-top: 2px; }").arg(sub));
    if (m_statusLabel)
        m_statusLabel->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 13px; padding: 48px 24px; line-height: 1.5; }").arg(sub));

    const QColor iconIc = dark ? QColor(244, 246, 255, 180) : QColor(33, 37, 41, 180);
    const QString iconBtnStyle = QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 8px; "
        "min-width: 32px; min-height: 32px; }"
        "QPushButton:hover { background: %1; }").arg(themeSoftFill(12, 10));
    if (m_refreshBtn) {
        m_refreshBtn->setIcon(Icons::renderNamed("Refresh", 18, iconIc));
        m_refreshBtn->setIconSize(QSize(18, 18));
        m_refreshBtn->setStyleSheet(iconBtnStyle);
    }
    if (m_closeBtn) {
        m_closeBtn->setIcon(Icons::renderNamed("Close", 18, iconIc));
        m_closeBtn->setIconSize(QSize(18, 18));
        m_closeBtn->setStyleSheet(iconBtnStyle);
    }

    if (m_scroll) {
        m_scroll->setStyleSheet(QStringLiteral(
            "QScrollArea#cmtScroll { border: none; background: transparent; }"
            "QScrollBar:vertical { width: 5px; background: transparent; margin: 2px 0 4px 0; }"
            "QScrollBar::handle:vertical { background: rgba(230,57,80,%1); border-radius: 3px; min-height: 40px; }"
            "QScrollBar::handle:vertical:hover { background: rgba(230,57,80,%2); }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
                                    .arg(dark ? 70 : 82)
                                    .arg(dark ? 108 : 125));
    }

    if (m_inputBox)
        m_inputBox->setStyleSheet(QStringLiteral(
            "QWidget#cmtInputBox { background: %1; border-radius: 10px; }").arg(themeSoftFill(8, 5)));
    if (m_input)
        m_input->setStyleSheet(QStringLiteral(
            "QTextEdit { background: transparent; border: none; color: %1; font-size: 13px; }").arg(main));
    if (m_counterLabel)
        m_counterLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 11px; color: %1; }").arg(faint));
    if (m_replyChipLabel)
        m_replyChipLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 12px; color: %1; }").arg(themeAccent()));
    if (m_loginHintLabel)
        m_loginHintLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 12px; color: %1; }").arg(sub));

    if (m_submitBtn)
        m_submitBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: #E63950; color: #ffffff; border: none; border-radius: 8px; "
            "padding: 0 16px; font-size: 12px; }"
            "QPushButton:hover { background: %1; }"
            "QPushButton:disabled { background: %2; color: rgba(255,255,255,0.45); }")
                                       .arg(QString::fromUtf8(Theme::kLavender))
                                       .arg(themeSoftFill(12, 10)));
    if (m_moreBtn)
        m_moreBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: none; border-radius: 8px; color: %2; "
            "font-size: 13px; font-weight: 500; min-height: 36px; }"
            "QPushButton:hover { background: %3; }"
            "QPushButton:disabled { color: %4; }")
                                     .arg(themeSoftFill(8, 5), main, themeSoftFill(14, 8), faint));
}

void CommentPanel::retranslate()
{
    if (m_titleLabel) m_titleLabel->setText(QStringLiteral("评论"));
    if (m_refreshBtn) m_refreshBtn->setToolTip(QStringLiteral("刷新"));
    if (m_closeBtn) m_closeBtn->setToolTip(QStringLiteral("关闭"));
    if (m_input) m_input->setPlaceholderText(m_replyId > 0
        ? QStringLiteral("回复 @%1").arg(m_replyName)
        : QStringLiteral("说点什么吧…"));
    if (m_loginHintLabel) m_loginHintLabel->setText(QStringLiteral("登录后即可发表评论与回复"));
    if (m_submitBtn) m_submitBtn->setText(m_replyId > 0 ? QStringLiteral("回复") : QStringLiteral("发表"));
    if (m_replyChipLabel) m_replyChipLabel->setText(QStringLiteral("回复 @%1").arg(m_replyName));
    applyPanelChrome();
    updateComposerState();
}

void CommentPanel::updateComposerState()
{
    if (!m_input || !m_submitBtn) return;
    const QString text = m_input->toPlainText().trimmed();
    if (text.size() > kMaxLength) {
        QSignalBlocker blocker(m_input);
        m_input->setPlainText(text.left(kMaxLength));
    }
    if (m_counterLabel)
        m_counterLabel->setText(QStringLiteral("%1/%2").arg(m_input->toPlainText().size()).arg(kMaxLength));

    const bool loggedIn = UserManager::instance().isLoggedIn();
    if (m_loginHint) m_loginHint->setVisible(!loggedIn);
    if (m_inputBox) m_inputBox->setVisible(loggedIn);
    if (m_replyChip) m_replyChip->setVisible(loggedIn && m_replyId > 0);
    if (m_submitBtn) {
        m_submitBtn->setEnabled(loggedIn && !text.isEmpty() && !m_submitting);
        m_submitBtn->setText(m_submitting
            ? QStringLiteral("提交中…")
            : (m_replyId > 0 ? QStringLiteral("回复") : QStringLiteral("发表")));
    }
    if (m_counterLabel) m_counterLabel->setVisible(loggedIn);
    if (m_moreBtn) {
        m_moreBtn->setVisible(m_hasMore && !m_topComments.isEmpty());
        m_moreBtn->setEnabled(!m_loading);
        m_moreBtn->setText(m_loading ? QStringLiteral("加载中…") : QStringLiteral("加载更多"));
    }
}

void CommentPanel::clearList()
{
    if (!m_itemsLayout) return;
    while (QLayoutItem *item = m_itemsLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
}

void CommentPanel::setStatusText(const QString &text)
{
    if (!m_statusLabel) return;
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.isEmpty());
    layoutOverlay();
}

void CommentPanel::layoutOverlay()
{
    if (!m_statusLabel || !m_listContainer) return;
    m_statusLabel->setGeometry(m_listContainer->rect());
    if (m_statusLabel->isVisible()) m_statusLabel->raise();
}

void CommentPanel::openFor(int musicId)
{
    showMusicComments(musicId);
    openDrawer();
}

void CommentPanel::showMusicComments(int musicId)
{
    if (m_musicId == musicId) return;

    m_musicId = musicId;
    m_page = 1;
    m_hasMore = false;
    m_floorTotal = 0;
    m_commentTotal = 0;
    m_topComments.clear();
    m_replyId = 0;
    m_replyName.clear();
    m_loading = false;
    ++m_scrollGeneration; // 丢弃在途请求的回调

    if (m_input) {
        m_input->clear();
        m_input->setPlaceholderText(QStringLiteral("说点什么吧…"));
    }
    if (m_countLabel) m_countLabel->setText(QString());
    clearList();
    if (m_scroll) m_scroll->verticalScrollBar()->setValue(0);
    reload();
}

void CommentPanel::refreshComments()
{
    m_loading = false; // 允许打断进行中的请求
    reload();
}

void CommentPanel::reload()
{
    if (!m_api || m_musicId <= 0) {
        if (m_musicId == 0)
            setStatusText(QStringLiteral("暂无歌曲信息"));
        else
            setStatusText(QStringLiteral("本地歌曲暂不支持评论"));
        updateComposerState();
        return;
    }

    m_loading = true;
    m_page = 1;
    m_hasMore = false;
    clearList();
    setStatusText(QStringLiteral("评论加载中…"));
    updateComposerState();

    const int generation = ++m_scrollGeneration;
    m_api->fetchComments(m_musicId, 1, kPageSize,
                         [this, generation](bool ok, const QString &message, const QVariantMap &data) {
        m_loading = false;
        if (generation != m_scrollGeneration)
            return;
        if (!ok) {
            setStatusText(message.isEmpty() ? QStringLiteral("评论加载失败") : message);
            updateComposerState();
            return;
        }
        m_hasMore = data.value(QStringLiteral("hasMore")).toBool();
        m_floorTotal = data.value(QStringLiteral("total")).toInt();
        m_commentTotal = data.value(QStringLiteral("totalComments")).toInt();
        m_topComments = data.value(QStringLiteral("comments")).toList();
        setStatusText(m_topComments.isEmpty() ? QStringLiteral("还没有评论，来抢沙发吧") : QString());
        rebuildList();
        if (m_countLabel)
            m_countLabel->setText(QStringLiteral("共 %1 条评论").arg(m_commentTotal));
        emit commentCountChanged(m_commentTotal);
        updateComposerState();
    });
}

void CommentPanel::loadMore()
{
    if (m_loading || !m_hasMore || !m_api || m_musicId <= 0) return;

    m_loading = true;
    updateComposerState();
    const int nextPage = m_page + 1;
    const int generation = m_scrollGeneration;
    m_api->fetchComments(m_musicId, nextPage, kPageSize,
                         [this, nextPage, generation](bool ok, const QString &message, const QVariantMap &data) {
        if (generation != m_scrollGeneration)
            return; // 期间已换曲 / 刷新
        m_loading = false;
        if (!ok) {
            setStatusText(message.isEmpty() ? QStringLiteral("加载失败") : message);
            updateComposerState();
            return;
        }
        m_page = nextPage;
        m_hasMore = data.value(QStringLiteral("hasMore")).toBool();
        m_topComments += data.value(QStringLiteral("comments")).toList();
        rebuildList();
        updateComposerState();
    });
}

void CommentPanel::rebuildList()
{
    if (!m_itemsLayout) return;
    clearList();
    for (const QVariant &entry : m_topComments)
        addCommentCard(m_itemsLayout, entry.toMap(), 0);
    updateComposerState();
    layoutOverlay();
}

void CommentPanel::addCommentCard(QVBoxLayout *layout, const QVariantMap &comment, int depth)
{
    if (!layout) return;
    QWidget *owner = layout->parentWidget() ? layout->parentWidget() : m_listContainer;

    const int id = comment.value(QStringLiteral("id")).toInt();
    const QVariantMap user = comment.value(QStringLiteral("user")).toMap();
    const bool isReply = depth > 0;

    auto *host = new QWidget(owner);
    host->setAttribute(Qt::WA_TranslucentBackground, true);
    auto *hostLay = new QHBoxLayout(host);
    hostLay->setContentsMargins(isReply ? kReplyIndent : 0, 0, 0, 0);
    hostLay->setSpacing(0);

    auto *card = new CommentCardFrame(isReply ? 12 : 20, host);
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(10, 8, 10, 8);
    cardLay->setSpacing(6);

    auto *row = new QHBoxLayout();
    row->setSpacing(10);

    const int px = isReply ? 28 : 34;
    auto *avatar = new QLabel(card);
    avatar->setFixedSize(px, px);
    const QPixmap fallback(QStringLiteral(":/icons/app.png"));
    if (!fallback.isNull())
        avatar->setPixmap(fallback.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    loadAvatar(avatar, user.value(QStringLiteral("id")).toInt(), px);
    row->addWidget(avatar, 0, Qt::AlignTop);

    auto *body = new QVBoxLayout();
    body->setSpacing(4);

    auto *meta = new QHBoxLayout();
    meta->setSpacing(8);
    auto *nick = new QLabel(user.value(QStringLiteral("nickname")).toString(), card);
    nick->setStyleSheet(QStringLiteral("QLabel { font-size: 13px; font-weight: 600; color: %1; }").arg(themeTextMain()));
    meta->addWidget(nick);

    const QVariantMap replyTo = comment.value(QStringLiteral("replyToUser")).toMap();
    if (!replyTo.isEmpty()) {
        auto *at = new QLabel(QStringLiteral("@%1").arg(replyTo.value(QStringLiteral("nickname")).toString()), card);
        at->setStyleSheet(QStringLiteral("QLabel { font-size: 12px; color: %1; }").arg(themeAccent()));
        meta->addWidget(at);
    }

    auto *time = new QLabel(formatTime(comment.value(QStringLiteral("createdAt")).toString()), card);
    time->setToolTip(comment.value(QStringLiteral("createdAt")).toString());
    time->setStyleSheet(QStringLiteral("QLabel { font-size: 11px; color: %1; }").arg(themeTextFaint()));
    meta->addWidget(time);

    const QString ip = comment.value(QStringLiteral("ipRegion")).toString();
    if (!ip.isEmpty()) {
        auto *region = new QLabel(ip, card);
        region->setStyleSheet(QStringLiteral("QLabel { font-size: 11px; color: %1; }").arg(themeTextFaint()));
        meta->addWidget(region);
    }
    meta->addStretch(1);
    body->addLayout(meta);

    auto *text = new QLabel(comment.value(QStringLiteral("content")).toString(), card);
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text->setStyleSheet(QStringLiteral("QLabel { font-size: 13px; color: %1; }").arg(themeTextMain()));
    body->addWidget(text);

    const QString actionStyle = QStringLiteral(
        "QPushButton { border: none; background: transparent; border-radius: 6px; font-size: 11px; "
        "color: %1; padding: 2px 8px; }"
        "QPushButton:hover { background: rgba(230,57,80,0.18); color: %2; }")
        .arg(themeTextSub(), themeAccent());

    auto *actions = new QHBoxLayout();
    actions->setSpacing(2);
    auto *replyBtn = new QPushButton(QStringLiteral("回复"), card);
    replyBtn->setCursor(Qt::PointingHandCursor);
    replyBtn->setStyleSheet(actionStyle);
    connect(replyBtn, &QPushButton::clicked, this, [this, id, nick]() {
        startReply(id, nick->text());
    });
    actions->addWidget(replyBtn);

    if (comment.value(QStringLiteral("canDelete")).toBool()) {
        auto *delBtn = new QPushButton(QStringLiteral("删除"), card);
        delBtn->setCursor(Qt::PointingHandCursor);
        delBtn->setStyleSheet(actionStyle);
        connect(delBtn, &QPushButton::clicked, this, [this, id]() { removeComment(id); });
        actions->addWidget(delBtn);
    }
    actions->addStretch(1);
    body->addLayout(actions);

    row->addLayout(body, 1);
    cardLay->addLayout(row);
    hostLay->addWidget(card, 1);
    layout->addWidget(host);

    // 楼层内回复（深度 1，不再继续嵌套）
    if (!isReply) {
        const QVariantList replies = comment.value(QStringLiteral("replies")).toList();
        for (const QVariant &entry : replies)
            addCommentCard(cardLay, entry.toMap(), 1);
    }
}

void CommentPanel::loadAvatar(QLabel *label, int userId, int px)
{
    if (!label || !m_avatarNam || userId <= 0) return;
    QUrl url(QString::fromUtf8("%1/api/user/avatar/%2").arg(QString::fromUtf8(Theme::kApiBase)).arg(userId));
    auto *reply = m_avatarNam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, label, [reply, label, px]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        QImage image;
        if (!image.loadFromData(reply->readAll())) return;
        label->setPixmap(roundedAvatar(image, px));
    });
}

QPixmap CommentPanel::roundedAvatar(const QImage &source, int px)
{
    QPixmap out(px, px);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addEllipse(0, 0, px, px);
    p.setClipPath(clip);
    p.drawImage(QRect(0, 0, px, px), source);
    return out;
}

QString CommentPanel::formatTime(const QString &raw)
{
    if (raw.isEmpty()) return QString();
    const QDateTime dt = QDateTime::fromString(raw, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!dt.isValid()) return raw;
    const qint64 diff = dt.secsTo(QDateTime::currentDateTime());
    if (diff < 60) return QStringLiteral("刚刚");
    if (diff < 3600) return QStringLiteral("%1 分钟前").arg(diff / 60);
    if (diff < 86400) return QStringLiteral("%1 小时前").arg(diff / 3600);
    if (diff < 86400 * 7) return QStringLiteral("%1 天前").arg(diff / 86400);
    return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

void CommentPanel::startReply(int commentId, const QString &nickname)
{
    if (!UserManager::instance().isLoggedIn()) return;
    m_replyId = commentId;
    m_replyName = nickname;
    if (m_replyChipLabel)
        m_replyChipLabel->setText(QStringLiteral("回复 @%1").arg(nickname));
    if (m_input)
        m_input->setPlaceholderText(QStringLiteral("回复 @%1").arg(nickname));
    updateComposerState();
    if (m_input) m_input->setFocus();
}

void CommentPanel::cancelReply()
{
    m_replyId = 0;
    m_replyName.clear();
    if (m_input) m_input->setPlaceholderText(QStringLiteral("说点什么吧…"));
    updateComposerState();
}

void CommentPanel::submit()
{
    if (!m_api || m_submitting || !m_input) return;
    if (!UserManager::instance().isLoggedIn()) return;
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty()) return;

    m_submitting = true;
    updateComposerState();
    const int parentId = m_replyId;
    const int musicId = m_musicId;
    m_api->postComment(m_musicId, text, parentId,
                       [this, parentId, musicId](bool ok, const QString &message, const QVariantMap &) {
        m_submitting = false;
        if (!ok) {
            setStatusText(message.isEmpty() ? QStringLiteral("发表失败") : message);
            updateComposerState();
            return;
        }
        if (musicId != m_musicId)
            return; // 提交期间已换曲，结果留给新歌
        if (m_input) m_input->clear();
        m_replyId = 0;
        m_replyName.clear();
        setStatusText(QString());
        if (m_input) m_input->setPlaceholderText(QStringLiteral("说点什么吧…"));
        reload();
    });
}

void CommentPanel::removeComment(int commentId)
{
    if (!m_api) return;
    const int musicId = m_musicId;
    m_api->deleteComment(commentId, [this, musicId](bool ok, const QString &message, const QVariantMap &) {
        if (!ok) {
            setStatusText(message.isEmpty() ? QStringLiteral("删除失败") : message);
            return;
        }
        if (musicId != m_musicId)
            return;
        reload();
    });
}

void CommentPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = rect();
    const int rad = 12;

    QPainterPath clip;
    clip.moveTo(r.right(), r.top());
    clip.lineTo(r.left() + rad, r.top());
    clip.arcTo(r.left(), r.top(), rad * 2, rad * 2, 90, 90);
    clip.lineTo(r.left(), r.bottom() - rad);
    clip.arcTo(r.left(), r.bottom() - rad * 2, rad * 2, rad * 2, 180, 90);
    clip.lineTo(r.right(), r.bottom());
    clip.closeSubpath();

    p.fillPath(clip, themePanelBg());
    p.setPen(QPen(themePanelBorder(), 1.0));
    p.drawPath(clip);
}

void CommentPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_listContainer && m_scroll)
        m_listContainer->setFixedWidth(qMax(80, m_scroll->viewport()->width()));
    layoutOverlay();
}

void CommentPanel::syncToHost()
{
    QWidget *host = parentWidget();
    if (!host) return;
    const int h = host->height();
    setFixedHeight(h);
    const int x = m_drawerOpen ? host->width() - kDrawerWidth : host->width();
    setGeometry(x, 0, kDrawerWidth, h);
}

void CommentPanel::openDrawer()
{
    QWidget *host = parentWidget();
    if (!host) return;

    if (m_slideAnim) {
        m_slideAnim->stop();
        m_slideAnim->deleteLater();
        m_slideAnim = nullptr;
    }

    const int h = host->height();
    setFixedHeight(h);
    show();
    raise();
    m_drawerOpen = true;

    const QRect end(host->width() - kDrawerWidth, 0, kDrawerWidth, h);
    const QRect start(host->width(), 0, kDrawerWidth, h);
    if (geometry() == end) {
        m_animating = false;
        return;
    }

    m_animating = true;
    setGeometry(start);
    m_slideAnim = new QPropertyAnimation(this, "geometry", this);
    m_slideAnim->setDuration(220);
    m_slideAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_slideAnim->setStartValue(start);
    m_slideAnim->setEndValue(end);
    connect(m_slideAnim, &QPropertyAnimation::finished, this, [this]() {
        m_animating = false;
        // DeleteWhenStopped 会在动画结束后销毁对象，必须同步置空，避免野指针
        m_slideAnim = nullptr;
    });
    m_slideAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void CommentPanel::closeDrawer()
{
    QWidget *host = parentWidget();
    if (!host) {
        hide();
        m_drawerOpen = false;
        emit drawerClosed();
        return;
    }
    if (!m_drawerOpen && !isVisible()) return;

    if (m_slideAnim) {
        m_slideAnim->stop();
        m_slideAnim->deleteLater();
        m_slideAnim = nullptr;
    }

    m_drawerOpen = false;
    const int h = host->height();
    const QRect start(geometry());
    const QRect end(host->width(), 0, kDrawerWidth, h);
    if (!isVisible() || start == end) {
        hide();
        syncToHost();
        emit drawerClosed();
        return;
    }

    m_animating = true;
    m_slideAnim = new QPropertyAnimation(this, "geometry", this);
    m_slideAnim->setDuration(200);
    m_slideAnim->setEasingCurve(QEasingCurve::InCubic);
    m_slideAnim->setStartValue(start);
    m_slideAnim->setEndValue(end);
    connect(m_slideAnim, &QPropertyAnimation::finished, this, [this]() {
        m_animating = false;
        // DeleteWhenStopped 会在动画结束后销毁对象，必须同步置空，避免野指针
        m_slideAnim = nullptr;
        hide();
        syncToHost();
        emit drawerClosed();
    });
    m_slideAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void CommentPanel::togglePanel()
{
    if (m_drawerOpen) closeDrawer();
    else openDrawer();
}
