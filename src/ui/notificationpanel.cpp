/**
 * @file notificationpanel.cpp
 * @brief 消息中心抽屉实现
 */

#include "ui/notificationpanel.h"
#include "core/apiclient.h"
#include "core/i18n.h"
#include "core/notificationcenter.h"
#include "core/usermanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/scrollareafix.h"
#include "ui/svgicon.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsDropShadowEffect>
#include <QResizeEvent>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QFrame>
#include <QScrollBar>
#include <algorithm>
#include <functional>

namespace {

constexpr int kPageSize = 20;
constexpr int kListPad = 16;
constexpr int kCardRadius = 8;

/** 与评论 / 播放队列抽屉同一主色（Theme 未暴露 accent 常量） */
const QColor kPrimary(230, 57, 80);

bool isDark()
{
    return Theme::ThemeManager::instance().isDarkMode();
}

QString themeTextMain()
{
    return isDark() ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
}

QString themeTextSub()
{
    return isDark() ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.62)");
}

QString themeTextFaint()
{
    return isDark() ? QStringLiteral("rgba(255,255,255,0.42)") : QStringLiteral("rgba(33,37,41,0.42)");
}

QColor themePanelBg()
{
    return isDark() ? QColor(QString::fromUtf8(Theme::kBgSurface)) : QColor(255, 255, 255);
}

QColor themePanelBorder()
{
    return isDark() ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 30);
}

/** 半透明浅底：暗色用白、亮色用黑，避免亮色下白底叠白底看不见 */
QString themeSoftFill(int darkAlpha, int lightAlpha)
{
    return isDark() ? QStringLiteral("rgba(255,255,255,%1)").arg(darkAlpha)
                    : QStringLiteral("rgba(33,37,41,%1)").arg(lightAlpha);
}

/** 「2026-10-09 12:30:05」→「10-09 12:30」，与服务端返回的格式解耦地用前缀截取 */
QString formatTime(const QString &raw)
{
    const QString text = raw.trimmed();
    return text.size() >= 16 ? text.mid(5, 11) : text;
}

// ─── 消息卡片：对齐评论卡片的圆角底 + hover 描边 ─────────────────
class NotificationCard : public QWidget
{
public:
    explicit NotificationCard(const QVariantMap &item, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, false);
        setAttribute(Qt::WA_TranslucentBackground, true);

        const bool unread = !item.value(QStringLiteral("read")).toBool();
        setAttribute(Qt::WA_Hover, true);

        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 10, 12, 10);
        lay->setSpacing(10);

        auto *dotWrap = new QWidget(this);
        dotWrap->setFixedWidth(10);
        auto *dotLay = new QVBoxLayout(dotWrap);
        dotLay->setContentsMargins(1, 6, 1, 0);
        dotLay->setSpacing(0);
        m_dot = new QLabel(dotWrap);
        m_dot->setFixedSize(8, 8);
        m_dot->setStyleSheet(QStringLiteral("QLabel { border-radius: 4px; background: %1; }")
                                 .arg(unread ? kPrimary.name() : QStringLiteral("transparent")));
        dotLay->addWidget(m_dot, 0, Qt::AlignTop);
        dotLay->addStretch(1);
        lay->addWidget(dotWrap, 0);

        auto *col = new QVBoxLayout();
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(4);

        m_title = new QLabel(item.value(QStringLiteral("title")).toString(), this);
        m_title->setWordWrap(true);
        m_title->setStyleSheet(QStringLiteral("QLabel { font-size: 13px; font-weight: %1; color: %2; }")
                                   .arg(unread ? QStringLiteral("700") : QStringLiteral("500"),
                                        themeTextMain()));
        col->addWidget(m_title);

        const QString body = item.value(QStringLiteral("body")).toString();
        if (!body.isEmpty()) {
            auto *bodyLabel = new QLabel(body, this);
            bodyLabel->setWordWrap(true);
            bodyLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 12px; color: %1; }")
                                         .arg(themeTextSub()));
            col->addWidget(bodyLabel);
        }

        auto *timeLabel = new QLabel(formatTime(item.value(QStringLiteral("createdAt")).toString()), this);
        timeLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 11px; color: %1; }")
                                     .arg(themeTextFaint()));
        col->addWidget(timeLabel);

        lay->addLayout(col, 1);
    }

    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(rect(), kCardRadius, kCardRadius);
        p.fillPath(path, QColor(kPrimary.red(), kPrimary.green(), kPrimary.blue(), m_hover ? 26 : 16));
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

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())
            && onClick) {
            onClick();
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    QLabel *m_dot = nullptr;
    QLabel *m_title = nullptr;
    bool m_hover = false;
};

} // namespace

NotificationPanel::NotificationPanel(ApiClient *api, QWidget *parent)
    : QWidget(parent), m_api(api)
{
    setObjectName(QStringLiteral("notificationPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kDrawerWidth);

    m_drawerShadow = new QGraphicsDropShadowEffect(this);
    m_drawerShadow->setBlurRadius(28);
    m_drawerShadow->setOffset(-6, 0);
    m_drawerShadow->setColor(QColor(0, 0, 0, 100));
    setGraphicsEffect(m_drawerShadow);

    setupUi();
    applyPanelChrome();
    syncToHost();
    hide();

    auto &center = NotificationCenter::instance();
    connect(&center, &NotificationCenter::messageReceived, this, &NotificationPanel::onStreamMessage);
    connect(&center, &NotificationCenter::readyReceived, this, &NotificationPanel::onStreamReady);
    connect(&center, &NotificationCenter::unreadChanged, this, [this](int unread) {
        m_unread = unread;
        updateHeaderState();
    });

    connect(&UserManager::instance(), &UserManager::loginStateChanged, this, [this]() {
        if (m_drawerOpen)
            reload();
    });

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) {
                applyPanelChrome();
                rebuildList();
            });
}

void NotificationPanel::setupUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ─── 标题行（对齐评论 / 播放队列抽屉）─────────────
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("ntfHeader"));
    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(16, 16, 12, 8);
    headerLay->setSpacing(8);

    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(0);
    m_titleLabel = new QLabel(header);
    m_subLabel = new QLabel(header);
    titleCol->addWidget(m_titleLabel);
    titleCol->addWidget(m_subLabel);
    headerLay->addLayout(titleCol, 1);

    m_markAllBtn = new QPushButton(header);
    m_markAllBtn->setCursor(Qt::PointingHandCursor);
    m_markAllBtn->setFlat(true);
    connect(m_markAllBtn, &QPushButton::clicked, this, &NotificationPanel::markAllRead);
    headerLay->addWidget(m_markAllBtn, 0, Qt::AlignTop);

    m_closeBtn = new QPushButton(header);
    m_closeBtn->setFixedSize(32, 32);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setFlat(true);
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() { emit hideRequested(); });
    headerLay->addWidget(m_closeBtn, 0, Qt::AlignTop);

    lay->addWidget(header);

    // ─── 消息列表 ─────────────────────────────
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("ntfScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);
    nekoPolishScrollAreaViewport(m_scroll);

    m_listContainer = new QWidget(m_scroll);
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(kListPad, 4, kListPad, 4);
    m_listLayout->setSpacing(6);
    m_listLayout->addStretch(1);
    m_scroll->setWidget(m_listContainer);
    lay->addWidget(m_scroll, 1);

    m_statusLabel = new QLabel(m_listContainer);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    m_listLayout->insertWidget(0, m_statusLabel);

    // ─── 底部：加载更早 ────────────────────────
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("ntfFooter"));
    auto *footerLay = new QVBoxLayout(footer);
    footerLay->setContentsMargins(kListPad, 8, kListPad, 16);
    footerLay->setSpacing(0);
    m_moreBtn = new QPushButton(footer);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setFlat(true);
    connect(m_moreBtn, &QPushButton::clicked, this, &NotificationPanel::loadMore);
    footerLay->addWidget(m_moreBtn);
    lay->addWidget(footer);

    retranslate();
}

void NotificationPanel::applyPanelChrome()
{
    const bool dark = isDark();
    const QString main = themeTextMain();
    const QString sub = themeTextSub();
    const QString faint = themeTextFaint();

    if (m_titleLabel)
        m_titleLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 16px; font-weight: 700; color: %1; }")
                                        .arg(main));
    if (m_subLabel)
        m_subLabel->setStyleSheet(QStringLiteral("QLabel { font-size: 12px; color: %1; margin-top: 2px; }")
                                      .arg(sub));
    if (m_statusLabel)
        m_statusLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 13px; padding: 48px 24px; "
                                                    "line-height: 1.5; }")
                                         .arg(sub));

    const QColor iconIc = dark ? QColor(244, 246, 255, 180) : QColor(33, 37, 41, 180);
    const QString iconBtnStyle = QStringLiteral(
        "QPushButton { background: transparent; border: none; border-radius: 8px; "
        "min-width: 32px; min-height: 32px; }"
        "QPushButton:hover { background: %1; }").arg(themeSoftFill(12, 10));
    if (m_closeBtn) {
        m_closeBtn->setIcon(Icons::renderNamed("Close", 18, iconIc));
        m_closeBtn->setIconSize(QSize(18, 18));
        m_closeBtn->setStyleSheet(iconBtnStyle);
    }
    if (m_markAllBtn)
        m_markAllBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: none; border-radius: 8px; color: %2; "
            "font-size: 12px; padding: 6px 12px; }"
            "QPushButton:hover { background: %3; }"
            "QPushButton:disabled { color: %4; background: transparent; }")
                                        .arg(themeSoftFill(8, 5), main, themeSoftFill(14, 8), faint));
    if (m_moreBtn)
        m_moreBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: none; border-radius: 8px; color: %2; "
            "font-size: 13px; font-weight: 500; min-height: 36px; }"
            "QPushButton:hover { background: %3; }"
            "QPushButton:disabled { color: %4; }")
                                     .arg(themeSoftFill(8, 5), main, themeSoftFill(14, 8), faint));

    if (m_scroll)
        m_scroll->setStyleSheet(QStringLiteral(
            "QScrollArea#ntfScroll { border: none; background: transparent; }"
            "QScrollBar:vertical { width: 5px; background: transparent; margin: 2px 0 4px 0; }"
            "QScrollBar::handle:vertical { background: rgba(230,57,80,%1); border-radius: 3px; min-height: 40px; }"
            "QScrollBar::handle:vertical:hover { background: rgba(230,57,80,%2); }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
                                    .arg(dark ? 70 : 82)
                                    .arg(dark ? 108 : 125));
}

void NotificationPanel::retranslate()
{
    auto &i18n = I18n::instance();
    if (m_titleLabel)
        m_titleLabel->setText(i18n.tr(QStringLiteral("notificationsTitle")));
    if (m_closeBtn)
        m_closeBtn->setToolTip(i18n.tr(QStringLiteral("close")));
    if (m_markAllBtn)
        m_markAllBtn->setText(i18n.tr(QStringLiteral("markAllRead")));
    if (m_moreBtn)
        m_moreBtn->setText(i18n.tr(QStringLiteral("loadEarlierMessages")));
    updateHeaderState();
}

int NotificationPanel::newestId() const
{
    return m_items.isEmpty() ? 0 : m_items.first().value(QStringLiteral("id")).toInt();
}

int NotificationPanel::oldestId() const
{
    return m_items.isEmpty() ? 0 : m_items.last().value(QStringLiteral("id")).toInt();
}

void NotificationPanel::updateHeaderState()
{
    const int unread = m_unread;
    if (m_subLabel) {
        m_subLabel->setText(unread > 0
                                ? I18n::instance().tr(QStringLiteral("notificationsUnreadFmt")).arg(unread)
                                : I18n::instance().tr(QStringLiteral("notificationsAllRead")));
    }
    if (m_markAllBtn)
        m_markAllBtn->setEnabled(unread > 0);
    if (m_moreBtn)
        m_moreBtn->setVisible(m_hasMore);
}

void NotificationPanel::setStatusText(const QString &text)
{
    m_statusLabel->setText(text);
    m_statusLabel->setVisible(!text.isEmpty());
    m_moreBtn->setVisible(m_hasMore && text.isEmpty());
}

void NotificationPanel::rebuildList()
{
    // 清掉除状态标签与末尾弹簧以外的全部卡片
    for (int i = m_listLayout->count() - 1; i >= 0; --i) {
        QLayoutItem *layoutItem = m_listLayout->takeAt(i);
        if (!layoutItem)
            continue;
        QWidget *w = layoutItem->widget();
        delete layoutItem;
        if (!w || w == m_statusLabel)
            continue;
        // 只从布局里摘掉还不够：仍挂在父控件上会继续显示
        w->setParent(nullptr);
        w->deleteLater();
    }
    m_listLayout->insertWidget(0, m_statusLabel);

    if (m_items.isEmpty()) {
        setStatusText(m_loading ? I18n::instance().tr(QStringLiteral("loading"))
                                : I18n::instance().tr(QStringLiteral("notificationsEmptyHint")));
        m_listLayout->addStretch(1);
        updateHeaderState();
        return;
    }

    setStatusText(QString());
    int insertAt = 1; // 状态标签之后
    for (const QVariantMap &item : m_items) {
        auto *card = new NotificationCard(item, m_listContainer);
        card->onClick = [this, item]() { activateItem(item); };
        m_listLayout->insertWidget(insertAt++, card);
    }
    m_listLayout->addStretch(1);
    updateHeaderState();
}

void NotificationPanel::prependItem(const QVariantMap &item)
{
    m_items.prepend(item);
    rebuildList();
}

void NotificationPanel::reload()
{
    if (!m_api)
        return;
    if (!UserManager::instance().isLoggedIn()) {
        m_items.clear();
        m_loaded = false;
        m_hasMore = false;
        m_loading = false;
        rebuildList();
        setStatusText(I18n::instance().tr(QStringLiteral("notificationsLoginHint")));
        return;
    }

    m_loading = true;
    rebuildList();
    m_api->fetchNotifications(0, 0, kPageSize,
                              [this](bool ok, const QString &, const QVariantMap &data) {
                                  m_loading = false;
                                  if (!ok) {
                                      setStatusText(I18n::instance().tr(QStringLiteral("networkError")));
                                      return;
                                  }
                                  m_items.clear();
                                  for (const QVariant &v : data.value(QStringLiteral("items")).toList())
                                      m_items.append(v.toMap());
                                  m_hasMore = data.value(QStringLiteral("hasMore")).toBool();
                                  m_loaded = true;
                                  NotificationCenter::instance().applyUnread(
                                      data.value(QStringLiteral("unread")).toInt());
                                  rebuildList();
                              });
}

void NotificationPanel::loadMore()
{
    if (!m_api || m_loading || !m_loaded || !m_hasMore)
        return;

    m_loading = true;
    m_moreBtn->setEnabled(false);
    m_api->fetchNotifications(0, oldestId(), kPageSize,
                              [this](bool ok, const QString &, const QVariantMap &data) {
                                  m_loading = false;
                                  m_moreBtn->setEnabled(true);
                                  if (!ok) {
                                      updateHeaderState();
                                      return;
                                  }
                                  for (const QVariant &v : data.value(QStringLiteral("items")).toList()) {
                                      const QVariantMap row = v.toMap();
                                      const int id = row.value(QStringLiteral("id")).toInt();
                                      const bool known = std::any_of(
                                          m_items.cbegin(), m_items.cend(),
                                          [id](const QVariantMap &existing) {
                                              return existing.value(QStringLiteral("id")).toInt() == id;
                                          });
                                      if (!known)
                                          m_items.append(row);
                                  }
                                  m_hasMore = data.value(QStringLiteral("hasMore")).toBool();
                                  NotificationCenter::instance().applyUnread(
                                      data.value(QStringLiteral("unread")).toInt());
                                  rebuildList();
                              });
}

void NotificationPanel::markAllRead()
{
    if (!m_api || m_unread == 0)
        return;
    m_api->markNotificationsRead({}, [this](bool ok, const QString &, const QVariantMap &data) {
        if (!ok)
            return;
        for (QVariantMap &item : m_items)
            item.insert(QStringLiteral("read"), true);
        NotificationCenter::instance().applyUnread(data.value(QStringLiteral("unread")).toInt());
        rebuildList();
    });
}

void NotificationPanel::activateItem(const QVariantMap &item)
{
    const int id = item.value(QStringLiteral("id")).toInt();
    if (!item.value(QStringLiteral("read")).toBool() && m_api && id > 0) {
        m_api->markNotificationsRead({id}, [this, id](bool ok, const QString &, const QVariantMap &data) {
            if (!ok)
                return;
            for (QVariantMap &row : m_items) {
                if (row.value(QStringLiteral("id")).toInt() == id)
                    row.insert(QStringLiteral("read"), true);
            }
            NotificationCenter::instance().applyUnread(data.value(QStringLiteral("unread")).toInt());
            rebuildList();
        });
    }
    emit messageActivated(item);
}

void NotificationPanel::onStreamMessage(const QVariantMap &item)
{
    if (!m_loaded)
        return;
    const int id = item.value(QStringLiteral("id")).toInt();
    for (const QVariantMap &existing : m_items) {
        if (existing.value(QStringLiteral("id")).toInt() == id)
            return;
    }
    prependItem(item);
}

void NotificationPanel::onStreamReady(int unread, int latestId)
{
    if (!m_drawerOpen)
        return;
    if (!m_loaded) {
        reload();
        return;
    }
    if (latestId <= newestId())
        return;

    // 断线期间漏了的消息：按本地游标补拉（服务端不重放历史帧）
    m_api->fetchNotifications(newestId(), 0, kPageSize,
                              [this, unread](bool ok, const QString &, const QVariantMap &data) {
                                  if (!ok) {
                                      NotificationCenter::instance().applyUnread(unread);
                                      return;
                                  }
                                  QList<QVariantMap> fresh;
                                  for (const QVariant &v : data.value(QStringLiteral("items")).toList()) {
                                      const QVariantMap row = v.toMap();
                                      const int id = row.value(QStringLiteral("id")).toInt();
                                      const bool known = std::any_of(
                                          m_items.cbegin(), m_items.cend(),
                                          [id](const QVariantMap &existing) {
                                              return existing.value(QStringLiteral("id")).toInt() == id;
                                          });
                                      if (!known)
                                          fresh.append(row);
                                  }
                                  if (!fresh.isEmpty())
                                      m_items = fresh + m_items;
                                  NotificationCenter::instance().applyUnread(
                                      data.value(QStringLiteral("unread")).toInt());
                                  rebuildList();
                              });
}

void NotificationPanel::paintEvent(QPaintEvent *event)
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

void NotificationPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_listContainer && m_scroll)
        m_listContainer->setFixedWidth(qMax(80, m_scroll->viewport()->width()));
}

void NotificationPanel::syncToHost()
{
    QWidget *host = parentWidget();
    if (!host)
        return;
    const int h = host->height();
    setFixedHeight(h);
    const int x = m_drawerOpen ? host->width() - kDrawerWidth : host->width();
    setGeometry(x, 0, kDrawerWidth, h);
}

void NotificationPanel::openDrawer()
{
    QWidget *host = parentWidget();
    if (!host)
        return;

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
    reload();

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

void NotificationPanel::closeDrawer()
{
    QWidget *host = parentWidget();
    if (!host) {
        hide();
        m_drawerOpen = false;
        emit drawerClosed();
        return;
    }
    if (!m_drawerOpen && !isVisible())
        return;

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
        m_slideAnim = nullptr;
        hide();
        syncToHost();
        emit drawerClosed();
    });
    m_slideAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void NotificationPanel::togglePanel()
{
    if (m_drawerOpen)
        closeDrawer();
    else
        openDrawer();
}
