/**
 * @file forgotpassworddialog.cpp
 * @brief 找回密码对话框实现
 */

#include "forgotpassworddialog.h"
#include "authdialogchrome.h"
#include "core/apiclient.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "theme/thememanager.h"

#include <QDesktopServices>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

ForgotPasswordDialog::ForgotPasswordDialog(QWidget *parent)
    : QDialog(parent)
    , m_api(new ApiClient(this))
{
    setStyleSheet(Theme::ThemeManager::instance().currentStyleSheet());
    setupUi();
    applyDialogTheme();

    setModal(true);
    setFixedWidth(AuthDialogChrome::kCompactDialogWidth);
    updateDialogSize();
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(30);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 80));
    setGraphicsEffect(shadow);
}

ForgotPasswordDialog::~ForgotPasswordDialog() = default;

void ForgotPasswordDialog::applyDialogTheme()
{
    const AuthDialogChrome::Palette p = AuthDialogChrome::currentPalette();

    if (m_card)
        m_card->setStyleSheet(AuthDialogChrome::cardStyleSheet(p)
                               + AuthDialogChrome::controlsStyleSheet(p));
    if (m_titleLabel)
        m_titleLabel->setStyleSheet(AuthDialogChrome::titleStyleSheet(p));
    if (m_hintLabel)
        m_hintLabel->setStyleSheet(AuthDialogChrome::bodyStyleSheet(p));
    if (m_msgLabel) {
        if (m_msgLabel->text().isEmpty()) {
            m_msgLabel->hide();
            m_msgLabel->setStyleSheet(AuthDialogChrome::msgStyleSheet(p.msgColor));
        } else {
            m_msgLabel->show();
        }
    }
}

void ForgotPasswordDialog::updateDialogSize()
{
    if (m_card && m_card->layout())
        m_card->layout()->activate();
    adjustSize();
    setMinimumHeight(0);
    resize(AuthDialogChrome::kCompactDialogWidth, sizeHint().height());
}

void ForgotPasswordDialog::setupUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(AuthDialogChrome::kOuterPad, AuthDialogChrome::kOuterPad,
                            AuthDialogChrome::kOuterPad, AuthDialogChrome::kOuterPad);
    outer->setSpacing(0);

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("authDialogCard"));
    auto *mainLayout = new QVBoxLayout(m_card);
    mainLayout->setContentsMargins(AuthDialogChrome::kCardPadH, AuthDialogChrome::kCardPadV,
                                   AuthDialogChrome::kCardPadH, AuthDialogChrome::kCardPadV);
    mainLayout->setSpacing(AuthDialogChrome::kSectionSpacing);

    auto *closeBtn = new QPushButton(m_card);
    closeBtn->setObjectName("dialogCloseBtn");
    closeBtn->setFixedSize(34, 34);
    closeBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    closeBtn->setToolTip(I18n::instance().tr(QStringLiteral("close")));
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto *headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->addStretch();
    headerRow->addWidget(closeBtn);
    mainLayout->addLayout(headerRow);

    m_titleLabel = new QLabel(I18n::instance().tr(QStringLiteral("forgotPasswordTitle")), m_card);
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    mainLayout->addWidget(m_titleLabel);

    m_hintLabel = new QLabel(I18n::instance().tr(QStringLiteral("forgotPasswordSsoHint")), m_card);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setAlignment(Qt::AlignLeft);
    m_hintLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLayout->addWidget(m_hintLabel);

    m_msgLabel = new QLabel(m_card);
    m_msgLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_msgLabel->setWordWrap(true);
    m_msgLabel->hide();
    mainLayout->addWidget(m_msgLabel);

    m_openBtn = new QPushButton(I18n::instance().tr(QStringLiteral("openNekoAuthForgotPassword")), m_card);
    m_openBtn->setObjectName("dialogBtn");
    m_openBtn->setFixedHeight(AuthDialogChrome::kPrimaryBtnHeight);
    m_openBtn->setCursor(Qt::PointingHandCursor);
    connect(m_openBtn, &QPushButton::clicked, this, &ForgotPasswordDialog::openNekoAuthReset);
    mainLayout->addWidget(m_openBtn);

    auto *backBtn = new QPushButton(I18n::instance().tr(QStringLiteral("backToLogin")), m_card);
    backBtn->setObjectName("dialogLinkBtn");
    backBtn->setFixedHeight(AuthDialogChrome::kLinkBtnHeight);
    backBtn->setCursor(Qt::PointingHandCursor);
    connect(backBtn, &QPushButton::clicked, this, &QDialog::reject);
    mainLayout->addWidget(backBtn);

    outer->addWidget(m_card);
}

void ForgotPasswordDialog::setMsg(const QString &text, const QColor &color)
{
    m_msgLabel->setText(text);
    if (text.isEmpty()) {
        m_msgLabel->hide();
        applyDialogTheme();
        updateDialogSize();
        return;
    }
    m_msgLabel->show();
    m_msgLabel->setStyleSheet(AuthDialogChrome::msgStyleSheet(color.name()));
    updateDialogSize();
}

void ForgotPasswordDialog::openNekoAuthReset()
{
    setMsg(QString(), Qt::transparent);
    m_openBtn->setEnabled(false);
    m_openBtn->setText(I18n::instance().tr(QStringLiteral("loadingShort")));

    m_api->fetchPasswordResetUrl([this](bool success, const QString &message, const QString &url) {
        QTimer::singleShot(0, this, [this, success, message, url]() {
            m_openBtn->setEnabled(true);
            m_openBtn->setText(I18n::instance().tr(QStringLiteral("openNekoAuthForgotPassword")));

            if (!success || url.isEmpty()) {
                const QString text = message.isEmpty()
                    ? I18n::instance().tr(QStringLiteral("passwordResetUrlFailed"))
                    : message;
                setMsg(text, Theme::kSakura);
                return;
            }

            if (!QDesktopServices::openUrl(QUrl(url))) {
                setMsg(I18n::instance().tr(QStringLiteral("openBrowserFailed")), Theme::kSakura);
                return;
            }

            setMsg(I18n::instance().tr(QStringLiteral("passwordResetOpened")), Theme::kMint);
        });
    });
}
