/**
 * @file titlebar.cpp
 * @brief 自定义标题栏实现
 *
 * 56px 紧凑，毛玻璃背景 + 底部薰衣草紫微光线。
 */

#include "titlebar.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"
#include "ui/vippillbutton.h"
#include "core/i18n.h"
#include "core/usermanager.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QStyle>
#include <QEvent>
#include <QGuiApplication>
#include <QWindow>
#include <QToolTip>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFontMetrics>
#include <QPainterPath>
#include <QUrl>
#include <QResizeEvent>
#include <QSizePolicy>

namespace {

constexpr int kTbAvatarPx = 28;
constexpr int kTbChevronPx = 12;

QColor iconNormal() {
    return Theme::ThemeManager::instance().isDarkMode() 
        ? QColor(245, 240, 255, 166) 
        : QColor(33, 37, 41, 166);
}

QColor iconActive() {
    return Theme::ThemeManager::instance().isDarkMode() 
        ? QColor(245, 240, 255, 230) 
        : QColor(33, 37, 41, 230);
}

QColor closeNormal() {
    return Theme::ThemeManager::instance().isDarkMode() 
        ? QColor(245, 240, 255, 180) 
        : QColor(33, 37, 41, 180);
}

QColor closeActive() {
    return QColor(242, 100, 100, 230); // 红色，保持不变
}

QColor chevronMuted()
{
    return Theme::ThemeManager::instance().isDarkMode() ? QColor(245, 240, 255, 140)
                                                        : QColor(33, 37, 41, 128);
}

/** 与 old TitleBar.vue 搜索图标 --text-light 一致 */
QColor searchBarIconMuted()
{
    return Theme::ThemeManager::instance().isDarkMode() ? QColor(245, 240, 255, 128)
                                                        : QColor(33, 37, 41, 140);
}
}

TitleBar::TitleBar(QWidget *parent) : QWidget(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    setupUi();
    // 安装事件过滤器到 QApplication，捕获标题栏内所有子控件的鼠标事件
    QGuiApplication::instance()->installEventFilter(this);

    // 监听用户状态变化
    connect(&UserManager::instance(), &UserManager::loginStateChanged,
            this, &TitleBar::updateAvatar);
    connect(&UserManager::instance(), &UserManager::vipStatusChanged,
            this, &TitleBar::updateVipPill);
    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged,
            this, [this](Theme::ThemeMode) {
                updateChevronPixmap();
                refreshSearchGlyph();
            });
}

TitleBar::~TitleBar()
{
    if (m_avatarReply) {
        m_avatarReply->disconnect();
        m_avatarReply->abort();
        m_avatarReply->deleteLater();
        m_avatarReply = nullptr;
    }
}

bool TitleBar::eventFilter(QObject *watched, QEvent *event)
{
    // 只处理 TitleBar 及其子控件的事件
    auto *w = qobject_cast<QWidget *>(watched);
    if (!w || !isAncestorOf(w)) {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::FocusIn: {
        if (watched == m_search && m_searchWrap) {
            m_searchWrap->setProperty("searchFocus", true);
            m_searchWrap->style()->unpolish(m_searchWrap);
            m_searchWrap->style()->polish(m_searchWrap);
        }
        break;
    }
    case QEvent::FocusOut: {
        if (watched == m_search && m_searchWrap) {
            m_searchWrap->setProperty("searchFocus", false);
            m_searchWrap->style()->unpolish(m_searchWrap);
            m_searchWrap->style()->polish(m_searchWrap);
        }
        break;
    }
    case QEvent::MouseButtonPress: {
        auto *e = static_cast<QMouseEvent *>(event);
        if (e->button() == Qt::LeftButton) {
            // 按钮和输入框不拦截，让它们自己处理
            if (qobject_cast<QPushButton *>(w) || qobject_cast<QLineEdit *>(w)) {
                return false;
            }
            // 点击搜索条空白处（如放大镜旁）时让输入框获得焦点
            if (m_searchWrap && (w == m_searchWrap || w == m_searchGlyph)) {
                if (m_search)
                    m_search->setFocus(Qt::MouseFocusReason);
                return true;
            }
            // 头像点击 - 扩大判定范围到整个头像区域及其子控件
            if (w == m_avatarWidget || 
                w == m_avatarIcon || 
                w == m_usernameLabel || 
                w == m_dropdownIcon ||
                (m_avatarWidget && m_avatarWidget->isAncestorOf(w))) {
                emit avatarClicked();
                return true;
            }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            if (window()) window()->windowHandle()->startSystemMove();
#endif
            return true;
        }
        break;
    }
    case QEvent::MouseButtonDblClick: {
        auto *e = static_cast<QMouseEvent *>(event);
        if (e->button() == Qt::LeftButton && window()) {
            if (qobject_cast<QPushButton *>(w)) return false;
            window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
            return true;
        }
        break;
    }
    case QEvent::Enter: {
        if (w == m_avatarWidget) {
            QString tip = UserManager::instance().isLoggedIn()
                ? UserManager::instance().userInfo().value("username").toString()
                : I18n::instance().tr("goToLogin");
            QToolTip::showText(QCursor::pos(), tip);
        }
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && window())
        window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
}

void TitleBar::setupUi()
{
    setObjectName(QStringLiteral("titleBar"));
    setFixedHeight(Theme::kTitleBarH);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(24, 0, 16, 0);
    lay->setSpacing(12);

    // ─── 搜索框（SPlayer Nav 居中条）────────────────
    m_searchWrap = new QWidget(this);
    m_searchWrap->setObjectName("tbSearchWrap");
    m_searchWrap->setAttribute(Qt::WA_StyledBackground, true);
    m_searchWrap->setCursor(Qt::PointingHandCursor);
    m_searchWrap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_searchWrap->setFixedHeight(36);
    m_searchWrap->setMinimumWidth(220);
    m_searchWrap->setMaximumWidth(560);
    auto *searchLay = new QHBoxLayout(m_searchWrap);
    searchLay->setContentsMargins(14, 0, 14, 0);
    searchLay->setSpacing(8);

    m_searchGlyph = new QLabel(m_searchWrap);
    m_searchGlyph->setObjectName("tbSearchGlyph");
    m_searchGlyph->setFixedSize(16, 16);
    m_searchGlyph->setScaledContents(false);
    m_searchGlyph->setCursor(Qt::PointingHandCursor);
    searchLay->addWidget(m_searchGlyph, 0, Qt::AlignVCenter);

    m_search = new QLineEdit(m_searchWrap);
    m_search->setObjectName("tbSearchInner");
    m_search->setAttribute(Qt::WA_StyledBackground, true);
    m_search->setFrame(false);
    m_search->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_search->setPlaceholderText(I18n::instance().tr("searchPlaceholder"));
    m_search->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_search->setMinimumHeight(24);
    m_search->setCursor(Qt::IBeamCursor);
    searchLay->addWidget(m_search, 1, Qt::AlignVCenter);

    refreshSearchGlyph();
    connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        const QString q = m_search->text().trimmed();
        if (!q.isEmpty())
            emit searchRequested(q);
    });
    lay->addStretch(1);
    lay->addWidget(m_searchWrap, 0, Qt::AlignVCenter);
    lay->addStretch(1);

    // 账号区与设置、窗口按钮分组留白
    lay->addSpacing(8);

    // ─── 右侧控制 ────────────────────────────────────
    m_vipPill = new VipPillButton(this);
    connect(m_vipPill, &QPushButton::clicked, this, &TitleBar::vipClicked);
    lay->addWidget(m_vipPill, 0, Qt::AlignVCenter);
    lay->addSpacing(8);

    // 账号胶囊：头像 + 用户名 + 下拉提示
    m_avatarWidget = new QWidget(this);
    m_avatarWidget->setObjectName("tbAvatarWidget");
    m_avatarWidget->setCursor(Qt::PointingHandCursor);
    m_avatarWidget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *avatarLay = new QHBoxLayout(m_avatarWidget);
    avatarLay->setContentsMargins(10, 5, 10, 5);
    avatarLay->setSpacing(8);
    avatarLay->setAlignment(Qt::AlignVCenter);

    m_avatarIcon = new QLabel(m_avatarWidget);
    m_avatarIcon->setFixedSize(kTbAvatarPx, kTbAvatarPx);
    m_avatarIcon->setScaledContents(false);
    m_avatarIcon->setPixmap(QPixmap(QStringLiteral(":/icons/app.png"))
                               .scaled(kTbAvatarPx, kTbAvatarPx, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    avatarLay->addWidget(m_avatarIcon, 0, Qt::AlignVCenter);

    m_usernameLabel = new QLabel(m_avatarWidget);
    m_usernameLabel->setObjectName("tbUsername");
    m_usernameLabel->setCursor(Qt::PointingHandCursor);
    m_usernameLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_usernameLabel->setMinimumWidth(48);
    m_usernameLabel->setMaximumWidth(200);
    m_usernameLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_usernameLabel->setMinimumHeight(kTbAvatarPx);
    avatarLay->addWidget(m_usernameLabel, 0, Qt::AlignVCenter);

    m_dropdownIcon = new QLabel(m_avatarWidget);
    m_dropdownIcon->setObjectName("tbAccountChevron");
    m_dropdownIcon->setFixedSize(kTbChevronPx, kTbChevronPx);
    m_dropdownIcon->setCursor(Qt::PointingHandCursor);
    m_dropdownIcon->setScaledContents(false);
    avatarLay->addWidget(m_dropdownIcon, 0, Qt::AlignVCenter);

    updateAvatar();
    updateVipPill();
    lay->addWidget(m_avatarWidget, 0, Qt::AlignVCenter);

    lay->addSpacing(10);

    auto *settingsBtn = new QPushButton(this);
    settingsBtn->setObjectName("tbIconBtn");
    settingsBtn->setFixedSize(36, 36);
    settingsBtn->setIcon(Icons::iconNamed("Settings", 18, iconNormal(), iconActive()));
    settingsBtn->setCursor(Qt::PointingHandCursor);
    settingsBtn->setToolTip(I18n::instance().tr("settings"));
    connect(settingsBtn, &QPushButton::clicked, this, &TitleBar::settingsClicked);
    lay->addWidget(settingsBtn, 0, Qt::AlignVCenter);

    lay->addSpacing(6);

    // 窗口控制（圆形彩色按钮）
    const QColor kMinHover = QColor(230, 57, 80);
    const QColor kMaxHover = QColor(126, 200, 200);
    const QColor kCloseHover = QColor(232, 93, 117); // 红色

    auto *minBtn = new QPushButton(this);
    minBtn->setObjectName("tbMinBtn");
    minBtn->setFixedSize(32, 32);
    minBtn->setIcon(Icons::iconNamed("WindowMinimize", 20, iconNormal(), kMinHover));
    minBtn->setCursor(Qt::PointingHandCursor);
    minBtn->setToolTip(QStringLiteral("最小化"));
    connect(minBtn, &QPushButton::clicked, this, [this]() { if (window()) window()->showMinimized(); });
    lay->addWidget(minBtn, 0, Qt::AlignVCenter);

    auto *maxBtn = new QPushButton(this);
    maxBtn->setObjectName("tbMaxBtn");
    maxBtn->setFixedSize(32, 32);
    maxBtn->setIcon(Icons::iconNamed("WindowMaximize", 20, iconNormal(), kMaxHover));
    maxBtn->setCursor(Qt::PointingHandCursor);
    maxBtn->setToolTip(QStringLiteral("最大化"));
    connect(maxBtn, &QPushButton::clicked, this, [this]() {
        if (window()) window()->isMaximized() ? window()->showNormal() : window()->showMaximized();
    });
    lay->addWidget(maxBtn, 0, Qt::AlignVCenter);

    auto *closeBtn = new QPushButton(this);
    closeBtn->setObjectName("tbCloseBtn");
    closeBtn->setFixedSize(32, 32);
    closeBtn->setIcon(Icons::iconNamed("WindowClose", 20, closeNormal(), kCloseHover));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setToolTip(QStringLiteral("关闭"));
    connect(closeBtn, &QPushButton::clicked, this, [this]() { if (window()) window()->close(); });
    lay->addWidget(closeBtn, 0, Qt::AlignVCenter);
}

void TitleBar::retranslate()
{
    auto *search = findChild<QLineEdit *>("tbSearchInner");
    if (search) search->setPlaceholderText(I18n::instance().tr("searchPlaceholder"));

    auto *settingsBtn = findChild<QPushButton *>("tbIconBtn");
    if (settingsBtn) settingsBtn->setToolTip(I18n::instance().tr("settings"));

    updateAvatar();
    updateVipPill();
}

void TitleBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    elideUsername();
}

QPoint TitleBar::avatarPos() const
{
    if (m_avatarWidget) {
        return m_avatarWidget->mapToGlobal(QPoint(m_avatarWidget->width() / 2, m_avatarWidget->height()));
    }
    return mapToGlobal(QPoint(width() - 40, height()));
}

void TitleBar::elideUsername()
{
    if (!m_usernameLabel)
        return;

    if (!UserManager::instance().isLoggedIn()) {
        m_usernameLabel->setText(I18n::instance().tr("goToLogin"));
        m_usernameLabel->setToolTip(I18n::instance().tr("goToLogin"));
        return;
    }

    QString username = UserManager::instance().userInfo().value("username").toString();
    if (username.isEmpty())
        username = QStringLiteral("User");
    m_usernameLabel->setToolTip(username);

    int avail = m_usernameLabel->width() - 2;
    if (avail < 32)
        avail = 140;
    QFontMetrics fm(m_usernameLabel->font());
    m_usernameLabel->setText(fm.elidedText(username, Qt::ElideRight, avail));
}

void TitleBar::refreshSearchGlyph()
{
    if (!m_searchGlyph)
        return;
    m_searchGlyph->setPixmap(Icons::renderNamed("Search", 16, searchBarIconMuted()));
}

void TitleBar::updateChevronPixmap()
{
    if (!m_dropdownIcon)
        return;
    QPixmap arrow(kTbChevronPx, kTbChevronPx);
    arrow.fill(Qt::transparent);
    QPainter p(&arrow);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = chevronMuted();
    p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const qreal cx = kTbChevronPx * 0.5;
    const qreal y1 = kTbChevronPx * 0.38;
    const qreal y2 = kTbChevronPx * 0.62;
    p.drawLine(QPointF(cx - 3.0, y1), QPointF(cx, y2));
    p.drawLine(QPointF(cx, y2), QPointF(cx + 3.0, y1));
    m_dropdownIcon->setPixmap(arrow);
}

void TitleBar::updateAvatar()
{
    if (!m_avatarIcon || !m_usernameLabel || !m_dropdownIcon) return;

    if (UserManager::instance().isLoggedIn()) {
        int userId = UserManager::instance().userInfo().value("id").toInt();
        if (userId > 0) {
            QString avatarUrl = QString::fromUtf8("%1/api/user/avatar/%2")
                .arg(Theme::kApiBase).arg(userId);
            loadAvatarAsync(avatarUrl, userId);
        } else {
            m_avatarIcon->setPixmap(QPixmap(QStringLiteral(":/icons/app.png"))
                               .scaled(kTbAvatarPx, kTbAvatarPx, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        elideUsername();
    } else {
        if (m_avatarReply) {
            m_avatarReply->disconnect();
            m_avatarReply->abort();
            m_avatarReply->deleteLater();
            m_avatarReply = nullptr;
        }
        m_avatarIcon->setPixmap(QPixmap(QStringLiteral(":/icons/app.png"))
                               .scaled(kTbAvatarPx, kTbAvatarPx, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        m_usernameLabel->setText(I18n::instance().tr("goToLogin"));
        m_usernameLabel->setToolTip(I18n::instance().tr("goToLogin"));
    }

    updateChevronPixmap();
    updateVipPill();
}

void TitleBar::updateVipPill()
{
    if (!m_vipPill)
        return;

    const bool loggedIn = UserManager::instance().isLoggedIn();
    m_vipPill->setVisible(loggedIn);
    if (!loggedIn)
        return;

    const bool isVip = UserManager::instance().isVip();
    m_vipPill->setVipActive(isVip);
    m_vipPill->setToolTip(isVip ? I18n::instance().tr(QStringLiteral("vipPillTooltipActive"))
                                : I18n::instance().tr(QStringLiteral("vipPillTooltipInactive")));
}

void TitleBar::loadAvatarAsync(const QString &url, int userId)
{
    m_avatarIcon->setPixmap(QPixmap(QStringLiteral(":/icons/app.png"))
                               .scaled(kTbAvatarPx, kTbAvatarPx, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    if (m_avatarReply) {
        m_avatarReply->disconnect();
        m_avatarReply->abort();
        m_avatarReply->deleteLater();
        m_avatarReply = nullptr;
    }

    QNetworkRequest req{QUrl(url)};
    req.setTransferTimeout(5000);
    QNetworkReply *reply = m_nam->get(req);
    m_avatarReply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, userId]() {
        if (m_avatarReply == reply)
            m_avatarReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        if (!UserManager::instance().isLoggedIn())
            return;
        if (UserManager::instance().userInfo().value("id").toInt() != userId)
            return;
        QPixmap pixmap;
        if (!pixmap.loadFromData(reply->readAll()))
            return;
        QPixmap scaled = pixmap.scaled(kTbAvatarPx, kTbAvatarPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap rounded(kTbAvatarPx, kTbAvatarPx);
        rounded.fill(Qt::transparent);
        QPainter p(&rounded);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(0, 0, kTbAvatarPx, kTbAvatarPx, kTbAvatarPx / 2, kTbAvatarPx / 2);
        p.setClipPath(path);
        p.drawPixmap(0, 0, scaled);
        m_avatarIcon->setPixmap(rounded);
    });
}

void TitleBar::paintEvent(QPaintEvent *)
{
    /* 不绘制底色，透出整窗底图；搜索框等子控件仍由 QSS 单独样式 */
}
