/**
 * @file legaldialog.cpp
 * @brief 用户协议 / 隐私政策弹窗实现
 */

#include "legaldialog.h"
#include "authdialogchrome.h"
#include "scrollareafix.h"

#include "core/i18n.h"
#include "theme/theme.h"

#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QRect>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

constexpr int kDocWidth = 660;
constexpr int kConsentWidth = 520;
constexpr int kOuterPad = 16;
constexpr int kCardPadH = 28;
constexpr int kCardPadV = 22;

void polishFramelessDialog(QDialog *dlg)
{
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    dlg->setAttribute(Qt::WA_TranslucentBackground);
    dlg->setModal(true);

    auto *shadow = new QGraphicsDropShadowEffect(dlg);
    shadow->setBlurRadius(30);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, 110));
    dlg->setGraphicsEffect(shadow);
}

QString cardStyle(const AuthDialogChrome::Palette &p)
{
    return QStringLiteral(
               "QWidget#legalCard { background: %1; border: 1px solid %2; border-radius: 16px; }")
        .arg(p.cardBg, p.cardBorder);
}

QString scrollStyle(const AuthDialogChrome::Palette &p)
{
    return QStringLiteral(
               "QScrollArea { background: transparent; border: none; }"
               "QScrollBar:vertical { background: transparent; width: 8px; margin: 2px 0; }"
               "QScrollBar::handle:vertical { background: %1; border-radius: 4px; min-height: 32px; }"
               "QScrollBar::handle:vertical:hover { background: %2; }"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
        .arg(QString::fromUtf8(Theme::kBorderGlass), p.accent);
}

QString primaryButtonStyle()
{
    return QStringLiteral(
        "QPushButton {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #8ADFF5, stop:1 #1677B8);"
        "  color: #1a1625; border: none; border-radius: 10px;"
        "  font-size: 14px; font-weight: 700; padding: 0 22px; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #D4BFF0, stop:1 #B89AE8); }");
}

QString secondaryButtonStyle(const AuthDialogChrome::Palette &p)
{
    return QStringLiteral(
               "QPushButton {"
               "  background: rgba(255, 255, 255, 0.06); color: %1;"
               "  border: 1px solid %2; border-radius: 10px;"
               "  font-size: 14px; padding: 0 22px; }"
               "QPushButton:hover { background: rgba(255, 255, 255, 0.1); }")
        .arg(p.titleColor, p.cardBorder);
}

QString linkButtonStyle(const AuthDialogChrome::Palette &p)
{
    return QStringLiteral(
               "QPushButton { background: transparent; border: none; color: %1;"
               "  font-size: 13.5px; font-weight: 700; text-align: left; padding: 2px 0; }"
               "QPushButton:hover { color: %2; }")
        .arg(p.accent, p.accentHover);
}

/** HTML 转义。 */
QString escapeHtml(QString text)
{
    text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    text.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return text;
}

/**
 * 将文档正文（支持 `**加粗**`、换行）渲染为 HTML 片段。
 * @param raw       原始文本
 * @param accent    链接颜色（含 # 或 rgba）
 */
QString renderRich(const QString &raw, const QString &accent)
{
    QString t = escapeHtml(raw);

    // **加粗**
    bool bold = false;
    int idx = 0;
    while ((idx = t.indexOf(QStringLiteral("**"), idx)) >= 0) {
        t.replace(idx, 2, bold ? QStringLiteral("</b>") : QStringLiteral("<b>"));
        bold = !bold;
        idx += bold ? 3 : 4;
    }

    // 换行
    t.replace(QLatin1Char('\n'), QStringLiteral("<br>"));

    // 裸链接 → 可点击（排除中英文括号与常见标点，避免把尾随标点吸入链接）
    static const QRegularExpression urlRe(
        QStringLiteral("(https?://[^\\s<>()\\[\\]（）【】「」,，。；;、]+)"));
    t.replace(urlRe, QStringLiteral("<a href=\"\\1\" style=\"color:%1; text-decoration:none;\">\\1</a>").arg(accent));

    return t;
}

/** 组装整篇文档的 HTML。 */
QString buildDocumentHtml(const LegalDocument &doc, const AuthDialogChrome::Palette &p)
{
    QString html = QStringLiteral("<div style=\"line-height:1.7;\">");
    if (!doc.intro.isEmpty())
        html += QStringLiteral("<div>%1</div>").arg(renderRich(doc.intro, p.accent));
    for (const LegalSection &section : doc.sections) {
        html += QStringLiteral("<div style=\"margin-top:16px; font-weight:700; color:%1;\">%2</div>")
                    .arg(p.titleColor, escapeHtml(section.title));
        html += QStringLiteral("<div style=\"margin-top:4px;\">%1</div>")
                    .arg(renderRich(section.body, p.accent));
    }
    if (!doc.footer.isEmpty())
        html += QStringLiteral("<div style=\"margin-top:18px; font-size:12px; color:%1;\">%2</div>")
                    .arg(p.bodyColor, renderRich(doc.footer, p.accent));
    html += QStringLiteral("</div>");
    return html;
}

int clampedDialogHeight(QWidget *parent)
{
    const QScreen *screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
    const int available = screen ? screen->availableGeometry().height() : 800;
    return qBound(440, static_cast<int>(available * 0.85), 780);
}

void centerOnScreen(QWidget *widget)
{
    const QScreen *screen = widget->screen() ? widget->screen() : QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect available = screen->availableGeometry();
    widget->move(available.center() - widget->rect().center());
}

} // namespace

// ─────────────────────────────────────────────────────────────

LegalDialog::LegalDialog(const LegalDocument &document, QWidget *parent)
    : QDialog(parent)
{
    polishFramelessDialog(this);

    const AuthDialogChrome::Palette p = AuthDialogChrome::currentPalette();

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kOuterPad, kOuterPad, kOuterPad, kOuterPad);
    outer->setSpacing(0);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("legalCard"));
    card->setStyleSheet(cardStyle(p));

    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(kCardPadH, kCardPadV, kCardPadH, kCardPadV);
    cardLay->setSpacing(8);

    auto *titleLabel = new QLabel(document.title, card);
    titleLabel->setWordWrap(true);
    titleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 21px; font-weight: 700; }").arg(p.titleColor));
    cardLay->addWidget(titleLabel);

    auto *metaLabel = new QLabel(
        QStringLiteral("%1 %2  ·  %3 %4")
            .arg(I18n::instance().tr(QStringLiteral("legalVersionLabel")), document.version,
                 I18n::instance().tr(QStringLiteral("legalUpdatedLabel")), document.updated),
        card);
    metaLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 12px; }").arg(p.bodyColor));
    cardLay->addWidget(metaLabel);
    cardLay->addSpacing(4);

    auto *scroll = new QScrollArea(card);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(scrollStyle(p));
    nekoPolishScrollAreaViewport(scroll);

    auto *content = new QLabel(scroll);
    content->setTextFormat(Qt::RichText);
    content->setText(buildDocumentHtml(document, p));
    content->setWordWrap(true);
    content->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    content->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    content->setOpenExternalLinks(true);
    content->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 13px; background: transparent; }").arg(p.bodyColor));
    scroll->setWidget(content);
    cardLay->addWidget(scroll, 1);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    auto *closeBtn = new QPushButton(I18n::instance().tr(QStringLiteral("close")), card);
    closeBtn->setFixedHeight(40);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(primaryButtonStyle());
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttonRow->addWidget(closeBtn);
    cardLay->addLayout(buttonRow);

    outer->addWidget(card);

    setFixedWidth(kDocWidth);
    setFixedHeight(clampedDialogHeight(parent));
}

void LegalDialog::showUserAgreement(QWidget *parent)
{
    LegalDialog dlg(Legal::userAgreement(), parent);
    dlg.exec();
}

void LegalDialog::showPrivacyPolicy(QWidget *parent)
{
    LegalDialog dlg(Legal::privacyPolicy(), parent);
    dlg.exec();
}

// ─────────────────────────────────────────────────────────────

LegalConsentDialog::LegalConsentDialog(QWidget *parent)
    : QDialog(parent)
{
    polishFramelessDialog(this);

    const AuthDialogChrome::Palette p = AuthDialogChrome::currentPalette();

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kOuterPad, kOuterPad, kOuterPad, kOuterPad);
    outer->setSpacing(0);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("legalCard"));
    card->setStyleSheet(cardStyle(p));

    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(kCardPadH, kCardPadV, kCardPadH, kCardPadV);
    cardLay->setSpacing(12);

    auto *titleLabel = new QLabel(I18n::instance().tr(QStringLiteral("legalConsentTitle")), card);
    titleLabel->setWordWrap(true);
    titleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 20px; font-weight: 700; }").arg(p.titleColor));
    cardLay->addWidget(titleLabel);

    auto *introLabel = new QLabel(card);
    introLabel->setTextFormat(Qt::RichText);
    introLabel->setText(renderRich(I18n::instance().tr(QStringLiteral("legalConsentIntro")), p.accent));
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 13px; }").arg(p.bodyColor));
    cardLay->addWidget(introLabel);

    auto *agreementLink = new QPushButton(
        I18n::instance().tr(QStringLiteral("legalViewAgreement")), card);
    agreementLink->setCursor(Qt::PointingHandCursor);
    agreementLink->setStyleSheet(linkButtonStyle(p));
    connect(agreementLink, &QPushButton::clicked, this, [this]() {
        LegalDialog::showUserAgreement(this);
    });
    cardLay->addWidget(agreementLink);

    auto *privacyLink = new QPushButton(
        I18n::instance().tr(QStringLiteral("legalViewPrivacy")), card);
    privacyLink->setCursor(Qt::PointingHandCursor);
    privacyLink->setStyleSheet(linkButtonStyle(p));
    connect(privacyLink, &QPushButton::clicked, this, [this]() {
        LegalDialog::showPrivacyPolicy(this);
    });
    cardLay->addWidget(privacyLink);

    cardLay->addSpacing(6);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(10);

    auto *declineBtn = new QPushButton(I18n::instance().tr(QStringLiteral("legalDecline")), card);
    declineBtn->setFixedHeight(42);
    declineBtn->setCursor(Qt::PointingHandCursor);
    declineBtn->setStyleSheet(secondaryButtonStyle(p));
    connect(declineBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttonRow->addWidget(declineBtn, 1);

    auto *agreeBtn = new QPushButton(I18n::instance().tr(QStringLiteral("legalAgree")), card);
    agreeBtn->setFixedHeight(42);
    agreeBtn->setCursor(Qt::PointingHandCursor);
    agreeBtn->setStyleSheet(primaryButtonStyle());
    connect(agreeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttonRow->addWidget(agreeBtn, 1);

    cardLay->addLayout(buttonRow);

    outer->addWidget(card);

    setFixedWidth(kConsentWidth);
    adjustSize();
    setFixedHeight(sizeHint().height());
    centerOnScreen(this);
}
