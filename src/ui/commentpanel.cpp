#include "ui/commentpanel.h"
#include "core/apiclient.h"
#include "core/usermanager.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
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
#include <QResizeEvent>
#include <QFrame>
#include <QSignalBlocker>
#include <QPixmap>
#include <QScrollBar>
#include <QDateTime>
#include <QUrl>

namespace {

constexpr int kMaxLength = 500;
constexpr int kPageSize = 20;
constexpr int kListPad = 14;

QString themeTextMain() {
    return Theme::ThemeManager::instance().isDarkMode()
        ? QString::fromUtf8(Theme::kTextMain)
        : QStringLiteral("#212529");
}

QString themeTextSub() {
    return Theme::ThemeManager::instance().isDarkMode()
        ? QString::fromUtf8(Theme::kTextSub)
        : QStringLiteral("rgba(33,37,41,0.62)");
}

QString themeTextFaint() {
    return Theme::ThemeManager::instance().isDarkMode()
        ? QStringLiteral("rgba(255,255,255,0.42)")
        : QStringLiteral("rgba(33,37,41,0.42)");
}

QString themeCardBg() {
    return Theme::ThemeManager::instance().isDarkMode()
        ? QStringLiteral("rgba(255,255,255,0.05)")
        : QStringLiteral("rgba(0,0,0,0.035)");
}

QString themeAccent() {
    // 与播放队列抽屉同一主色（Theme 未暴露 accent 常量）
    return QStringLiteral("#E63950");
}

} // namespace

CommentPanel::CommentPanel(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api)
{
    setObjectName(QStringLiteral("commentPanel"));
    setAttribute(Qt::WA_TranslucentBackground, true);
    m_avatarNam = new QNetworkAccessManager(this);
    setupUi();
    applyPanelChrome();
    syncToHost();
    hide();
}

void CommentPanel::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(10);

    // ─── 标题行 ─────────────────────────────
    auto *head = new QHBoxLayout();
    head->setSpacing(8);
    m_titleLabel = new QLabel(this);
    m_countLabel = new QLabel(this);
    m_closeBtn = new QPushButton(this);
    m_closeBtn->setFlat(true);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setFixedSize(30, 30);
    m_closeBtn->setToolTip(QStringLiteral("关闭"));
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        emit hideRequested();
    });
    head->addWidget(m_titleLabel);
    head->addWidget(m_countLabel);
    head->addStretch(1);
    head->addWidget(m_closeBtn);
    root->addLayout(head);

    // ─── 输入区 ─────────────────────────────
    auto *composer = new QWidget(this);
    composer->setObjectName(QStringLiteral("cmtComposer"));
    auto *composerLay = new QVBoxLayout(composer);
    composerLay->setContentsMargins(12, 10, 12, 10);
    composerLay->setSpacing(6);

    m_replyChip = new QWidget(composer);
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
    composerLay->addWidget(m_replyChip);

    m_input = new QTextEdit(composer);
    m_input->setObjectName(QStringLiteral("cmtInput"));
    m_input->setFixedHeight(72);
    m_input->setAcceptRichText(false);
    connect(m_input, &QTextEdit::textChanged, this, &CommentPanel::updateComposerState);
    composerLay->addWidget(m_input);

    auto *foot = new QHBoxLayout();
    foot->setSpacing(8);
    m_counterLabel = new QLabel(composer);
    m_submitBtn = new QPushButton(composer);
    m_submitBtn->setCursor(Qt::PointingHandCursor);
    m_submitBtn->setFixedHeight(28);
    connect(m_submitBtn, &QPushButton::clicked, this, &CommentPanel::submit);
    foot->addWidget(m_counterLabel);
    foot->addStretch(1);
    foot->addWidget(m_submitBtn);
    composerLay->addLayout(foot);
    root->addWidget(composer);

    m_loginHint = new QWidget(this);
    auto *hintLay = new QHBoxLayout(m_loginHint);
    hintLay->setContentsMargins(0, 0, 0, 0);
    m_loginHintLabel = new QLabel(m_loginHint);
    hintLay->addWidget(m_loginHintLabel);
    hintLay->addStretch(1);
    m_loginHint->hide();
    root->addWidget(m_loginHint);

    // ─── 列表 ─────────────────────────────
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("cmtScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);

    m_listContainer = new QWidget(m_scroll);
    m_listContainer->setAutoFillBackground(false);
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(kListPad, 4, kListPad, 4);
    m_listLayout->setSpacing(14);
    m_listLayout->addStretch(1);
    m_scroll->setWidget(m_listContainer);
    root->addWidget(m_scroll, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    m_moreBtn = new QPushButton(this);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setFixedHeight(30);
    connect(m_moreBtn, &QPushButton::clicked, this, [this]() {
        if (m_loading || !m_hasMore) return;
        m_loading = true;
        updateComposerState();
        const int nextPage = m_page + 1;
        if (!m_api) { m_loading = false; return; }
        m_api->fetchComments(m_musicId, nextPage, kPageSize,
                             [this, nextPage](bool ok, const QString &message, const QVariantMap &data) {
            m_loading = false;
            if (!ok) {
                m_statusLabel->setText(message);
                updateComposerState();
                return;
            }
            m_page = nextPage;
            m_hasMore = data.value(QStringLiteral("hasMore")).toBool();
            const QVariantList list = data.value(QStringLiteral("comments")).toList();
            for (const auto &entry : list)
                addCommentCard(m_listLayout, entry.toMap(), 0);
            m_statusLabel->setVisible(!m_hasMore);
            updateComposerState();
        });
    });
    root->addWidget(m_moreBtn);
    m_moreBtn->hide();

    retranslate();
}

void CommentPanel::applyPanelChrome()
{
    const QString main = themeTextMain();
    const QString sub = themeTextSub();
    const QString faint = themeTextFaint();
    const QString accent = themeAccent();

    if (m_titleLabel)
        m_titleLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 16px; font-weight: 700; color: %1; }").arg(main));
    if (m_countLabel)
        m_countLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 12px; color: %1; }").arg(sub));
    if (m_closeBtn) {
        m_closeBtn->setIcon(Icons::iconNamed("Close", 16, QColor(sub), QColor(accent)));
        m_closeBtn->setIconSize(QSize(16, 16));
    }
    if (m_statusLabel)
        m_statusLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 12px; color: %1; }").arg(faint));
    if (m_counterLabel)
        m_counterLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 11px; color: %1; }").arg(faint));
    if (m_input)
        m_input->setStyleSheet(QStringLiteral(
            "QTextEdit { background: transparent; border: none; color: %1; font-size: 13px; }").arg(main));
    if (m_replyChipLabel)
        m_replyChipLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 12px; color: %1; }").arg(accent));
    if (m_loginHintLabel)
        m_loginHintLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 12px; color: %1; }").arg(sub));

    const QString btnStyle = QStringLiteral(
        "QPushButton { background: %1; color: #ffffff; border: none; border-radius: 8px; padding: 0 14px; font-size: 12px; }"
        "QPushButton:disabled { background: rgba(255,255,255,0.12); color: rgba(255,255,255,0.45); }").arg(accent);
    const QString ghostStyle = QStringLiteral(
        "QPushButton { background: rgba(255,255,255,0.06); color: %1; border: 1px solid %2; "
        "border-radius: 8px; padding: 0 14px; font-size: 12px; }").arg(main, faint);

    if (m_submitBtn)
        m_submitBtn->setStyleSheet(btnStyle);
    if (m_moreBtn)
        m_moreBtn->setStyleSheet(ghostStyle);

    auto *composer = findChild<QWidget *>(QStringLiteral("cmtComposer"));
    if (composer)
        composer->setStyleSheet(QStringLiteral("QWidget#cmtComposer { background: %1; border-radius: 12px; }").arg(themeCardBg()));
}

void CommentPanel::retranslate()
{
    if (m_titleLabel) m_titleLabel->setText(QStringLiteral("评论"));
    if (m_closeBtn) m_closeBtn->setToolTip(QStringLiteral("关闭"));
    if (m_input) m_input->setPlaceholderText(m_replyId > 0
        ? QStringLiteral("回复 @%1").arg(m_replyName)
        : QStringLiteral("说点什么吧…"));
    if (m_loginHintLabel) m_loginHintLabel->setText(QStringLiteral("登录后即可发表评论与回复"));
    if (m_submitBtn) m_submitBtn->setText(m_replyId > 0 ? QStringLiteral("回复") : QStringLiteral("发表"));
    if (m_moreBtn) m_moreBtn->setText(QStringLiteral("加载更多"));
    if (m_replyChipLabel) m_replyChipLabel->setText(QStringLiteral("回复 @%1").arg(m_replyName));
    if (m_statusLabel && m_statusLabel->text().isEmpty())
        m_statusLabel->setText(QStringLiteral("评论加载中…"));
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
    if (m_input) m_input->setVisible(loggedIn);
    if (m_replyChip) m_replyChip->setVisible(loggedIn && m_replyId > 0);
    if (m_submitBtn) {
        m_submitBtn->setVisible(loggedIn);
        m_submitBtn->setEnabled(loggedIn && !text.isEmpty() && !m_submitting);
        m_submitBtn->setText(m_submitting
            ? QStringLiteral("提交中…")
            : (m_replyId > 0 ? QStringLiteral("回复") : QStringLiteral("发表")));
    }
    if (m_counterLabel) m_counterLabel->setVisible(loggedIn);
    if (m_moreBtn) {
        m_moreBtn->setVisible(m_hasMore);
        m_moreBtn->setEnabled(!m_loading);
        m_moreBtn->setText(m_loading ? QStringLiteral("加载中…") : QStringLiteral("加载更多"));
    }
}

void CommentPanel::clearList()
{
    if (!m_listLayout) return;
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_listLayout->addStretch(1);
}

void CommentPanel::setStatusText(const QString &text)
{
    if (!m_statusLabel) return;
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.isEmpty());
}

void CommentPanel::openFor(int musicId)
{
    if (m_musicId != musicId) {
        m_musicId = musicId;
        m_page = 1;
        m_hasMore = false;
        m_replyId = 0;
        m_replyName.clear();
        clearList();
        setStatusText(QStringLiteral("评论加载中…"));
        reload();
    }
    openDrawer();
}

void CommentPanel::reload()
{
    if (m_loading || !m_api || m_musicId <= 0) {
        if (m_musicId == 0)
            setStatusText(QStringLiteral("暂无歌曲信息"));
        else if (m_musicId < 0)
            setStatusText(QStringLiteral("本地歌曲暂不支持评论"));
        return;
    }
    m_loading = true;
    m_page = 1;
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
        const QVariantList list = data.value(QStringLiteral("comments")).toList();
        if (list.isEmpty())
            setStatusText(QStringLiteral("还没有评论，来抢沙发吧"));
        else
            setStatusText(QString());
        for (const auto &entry : list)
            addCommentCard(m_listLayout, entry.toMap(), 0);
        if (m_countLabel)
            m_countLabel->setText(QStringLiteral("%1").arg(m_commentTotal));
        emit commentCountChanged(m_commentTotal);
        updateComposerState();
    });
}

void CommentPanel::addCommentCard(QVBoxLayout *layout, const QVariantMap &comment, int depth)
{
    if (!layout) return;

    const int id = comment.value(QStringLiteral("id")).toInt();
    const QVariantMap user = comment.value(QStringLiteral("user")).toMap();

    auto *card = new QWidget(m_listContainer);
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(depth > 0 ? 26 : 0, 0, 0, 0);
    cardLay->setSpacing(4);

    auto *row = new QHBoxLayout();
    row->setSpacing(10);

    const int px = depth > 0 ? 26 : 34;
    auto *avatar = new QLabel(card);
    avatar->setFixedSize(px, px);
    avatar->setPixmap(QPixmap(QStringLiteral(":/icons/app.png")).scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    loadAvatar(avatar, user.value(QStringLiteral("id")).toInt(), px);
    row->addWidget(avatar, 0, Qt::AlignTop);

    auto *body = new QVBoxLayout();
    body->setSpacing(3);

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

    auto *actions = new QHBoxLayout();
    actions->setSpacing(12);
    auto *replyBtn = new QPushButton(QStringLiteral("回复"), card);
    replyBtn->setFlat(true);
    replyBtn->setCursor(Qt::PointingHandCursor);
    replyBtn->setStyleSheet(QStringLiteral(
        "QPushButton { border: none; background: transparent; font-size: 11px; color: %1; padding: 0; }").arg(themeTextSub()));
    connect(replyBtn, &QPushButton::clicked, this, [this, id, nick]() {
        startReply(id, nick->text());
    });
    actions->addWidget(replyBtn);

    if (comment.value(QStringLiteral("canDelete")).toBool()) {
        auto *delBtn = new QPushButton(QStringLiteral("删除"), card);
        delBtn->setFlat(true);
        delBtn->setCursor(Qt::PointingHandCursor);
        delBtn->setStyleSheet(QStringLiteral(
            "QPushButton { border: none; background: transparent; font-size: 11px; color: %1; padding: 0; }").arg(themeTextSub()));
        connect(delBtn, &QPushButton::clicked, this, [this, id]() { removeComment(id); });
        actions->addWidget(delBtn);
    }
    actions->addStretch(1);
    body->addLayout(actions);

    row->addLayout(body, 1);
    cardLay->addLayout(row);

    layout->insertWidget(qMax(0, layout->count() - 1), card);

    // 楼层内回复（深度 1，不再继续嵌套）
    if (depth == 0) {
        const QVariantList replies = comment.value(QStringLiteral("replies")).toList();
        for (const auto &entry : replies)
            addCommentCard(layout, entry.toMap(), 1);
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
    m_api->postComment(m_musicId, text, parentId,
                       [this, parentId](bool ok, const QString &message, const QVariantMap &) {
        m_submitting = false;
        if (!ok) {
            setStatusText(message.isEmpty() ? QStringLiteral("发表失败") : message);
            updateComposerState();
            return;
        }
        if (m_input) m_input->clear();
        m_replyId = 0;
        m_replyName.clear();
        if (parentId > 0)
            setStatusText(QString());
        reload();
    });
}

void CommentPanel::removeComment(int commentId)
{
    if (!m_api) return;
    m_api->deleteComment(commentId, [this](bool ok, const QString &message, const QVariantMap &) {
        if (!ok) {
            setStatusText(message.isEmpty() ? QStringLiteral("删除失败") : message);
            return;
        }
        reload();
    });
}

void CommentPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, 16, 16);
    p.setPen(QPen(dark ? QColor(255, 255, 255, 26) : QColor(0, 0, 0, 20), 1));
    p.setBrush(dark ? QColor(28, 28, 34, 246) : QColor(252, 252, 254, 250));
    p.drawPath(path);
}

void CommentPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_listContainer && m_scroll)
        m_listContainer->setFixedWidth(qMax(80, m_scroll->viewport()->width()));
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
