/**
 * @file logindialog.cpp
 * @brief 登录/注册对话框实现
 */

#include "logindialog.h"
#include "authdialogchrome.h"
#include "legaldialog.h"
#include "core/apiclient.h"
#include "core/usermanager.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "theme/thememanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsDropShadowEffect>
#include <QTimer>
#include <QNetworkReply>
#include <QColor>
#include <QPixmap>
#include <QUrl>
#include <QStyle>
#include <QFrame>
#include <QCheckBox>
#include <QPalette>
#include <QDesktopServices>

namespace {

/** 解析服务端返回的 data:image/png;base64,... 图片；失败返回空 QPixmap。 */
QPixmap pixmapFromDataUrl(const QString &dataUrl)
{
    const int comma = dataUrl.indexOf(QLatin1Char(','));
    if (comma <= 0)
        return {};

    const QString header = dataUrl.left(comma);
    if (!header.startsWith(QLatin1String("data:image/")))
        return {};

    const QByteArray payload = dataUrl.mid(comma + 1).toLatin1();
    const QByteArray bytes = header.contains(QLatin1String(";base64"))
                                 ? QByteArray::fromBase64(payload)
                                 : QUrl::fromPercentEncoding(payload).toUtf8();
    if (bytes.isEmpty())
        return {};

    QPixmap pixmap;
    if (!pixmap.loadFromData(bytes))
        return {};
    return pixmap;
}

} // namespace

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , m_api(new ApiClient(this))
{
    setStyleSheet(Theme::ThemeManager::instance().currentStyleSheet());
    setupUi();
    applyDialogTheme();

    setModal(true);
    setFixedWidth(AuthDialogChrome::kDialogWidth);
    updateDialogSize();
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    refreshQrSession();

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(30);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 80));
    setGraphicsEffect(shadow);
}

LoginDialog::~LoginDialog()
{
    stopQrSession();
}

void LoginDialog::applyDialogTheme()
{
    const AuthDialogChrome::Palette p = AuthDialogChrome::currentPalette();

    if (m_card)
        m_card->setStyleSheet(AuthDialogChrome::cardStyleSheet(p)
                               + AuthDialogChrome::controlsStyleSheet(p));
    if (m_titleLabel)
        m_titleLabel->setStyleSheet(AuthDialogChrome::titleStyleSheet(p));
    if (m_subtitleLabel)
        m_subtitleLabel->setStyleSheet(AuthDialogChrome::subtitleStyleSheet(p));
    if (m_msgLabel) {
        if (m_msgLabel->text().isEmpty()) {
            m_msgLabel->hide();
            m_msgLabel->setStyleSheet(AuthDialogChrome::msgStyleSheet(p.msgColor));
        } else {
            m_msgLabel->show();
        }
    }
    if (m_qrImageLabel)
        m_qrImageLabel->setStyleSheet(QStringLiteral(
            "QLabel#qrImageBox { background: #FFFFFF; border-radius: 12px; }"));
    if (m_qrTipLabel)
        m_qrTipLabel->setStyleSheet(AuthDialogChrome::bodyStyleSheet(p));
    if (m_consentText) {
        m_consentText->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 12.5px; }").arg(p.bodyColor));
        QPalette pal = m_consentText->palette();
        pal.setColor(QPalette::Link, QColor(p.accent));
        pal.setColor(QPalette::LinkVisited, QColor(p.accent));
        m_consentText->setPalette(pal);
    }
    if (m_consentCheck)
        m_consentCheck->setStyleSheet(QStringLiteral(
            "QCheckBox { spacing: 0px; }"
            "QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid %1; border-radius: 4px; background: transparent; }"
            "QCheckBox::indicator:hover { border: 1px solid %2; }"
            "QCheckBox::indicator:checked { background: %2; border: 1px solid %2; }")
            .arg(p.cardBorder, p.accent));
}

void LoginDialog::updateDialogSize()
{
    setFixedSize(AuthDialogChrome::kDialogWidth, AuthDialogChrome::kDialogHeight + 42);
}

void LoginDialog::setupUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(AuthDialogChrome::kOuterPad, AuthDialogChrome::kOuterPad,
                              AuthDialogChrome::kOuterPad, AuthDialogChrome::kOuterPad);
    outer->setSpacing(0);

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("authDialogCard"));
    auto *mainLayout = new QVBoxLayout(m_card);
    mainLayout->setContentsMargins(28, 24, 28, 24);
    mainLayout->setSpacing(0);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 12);
    header->addStretch();
    auto *closeBtn = new QPushButton(m_card);
    closeBtn->setObjectName("dialogCloseBtn");
    closeBtn->setFixedSize(32, 32);
    closeBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    closeBtn->setToolTip(I18n::instance().tr(QStringLiteral("close")));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    header->addWidget(closeBtn);
    mainLayout->addLayout(header);

    auto *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(26);

    auto *qrPane = new QWidget(m_card);
    qrPane->setFixedWidth(226);
    auto *qrLayout = new QVBoxLayout(qrPane);
    qrLayout->setContentsMargins(0, 0, 0, 0);
    qrLayout->setSpacing(10);
    auto *qrTitle = new QLabel(I18n::instance().tr(QStringLiteral("qrLoginTitle")), qrPane);
    qrTitle->setObjectName(QStringLiteral("authQrTitle"));
    qrTitle->setAlignment(Qt::AlignCenter);
    qrLayout->addWidget(qrTitle);
    m_qrImageLabel = new QLabel(qrPane);
    m_qrImageLabel->setObjectName(QStringLiteral("qrImageBox"));
    m_qrImageLabel->setAlignment(Qt::AlignCenter);
    m_qrImageLabel->setFixedSize(204, 204);
    qrLayout->addWidget(m_qrImageLabel, 0, Qt::AlignHCenter);
    m_qrTipLabel = new QLabel(I18n::instance().tr(QStringLiteral("qrLoginHint")), qrPane);
    m_qrTipLabel->setAlignment(Qt::AlignCenter);
    m_qrTipLabel->setWordWrap(true);
    qrLayout->addWidget(m_qrTipLabel);
    body->addWidget(qrPane);

    auto *divider = new QFrame(m_card);
    divider->setFrameShape(QFrame::VLine);
    divider->setFrameShadow(QFrame::Plain);
    divider->setFixedWidth(1);
    body->addWidget(divider);

    auto *formPane = new QWidget(m_card);
    auto *formLayout = new QVBoxLayout(formPane);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(10);
    m_titleLabel = new QLabel(I18n::instance().tr("login"), formPane);
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formLayout->addWidget(m_titleLabel);
    m_subtitleLabel = new QLabel(I18n::instance().tr(QStringLiteral("loginSubtitle")), formPane);
    m_subtitleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_subtitleLabel->setWordWrap(true);
    formLayout->addWidget(m_subtitleLabel);
    m_ssoBtn = new QPushButton(
            I18n::instance().tr(QStringLiteral("nekoAuthSsoLogin")), formPane);
    m_ssoBtn->setObjectName("dialogBtn");
    m_ssoBtn->setFixedHeight(AuthDialogChrome::kPrimaryBtnHeight);
    connect(m_ssoBtn, &QPushButton::clicked, this, &LoginDialog::doSsoLogin);
    formLayout->addWidget(m_ssoBtn);
    m_msgLabel = new QLabel(formPane);
    m_msgLabel->setWordWrap(true);
    m_msgLabel->hide();
    formLayout->addWidget(m_msgLabel);

    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 6, 0, 0);
    actionRow->setSpacing(10);
    m_switchBtn = new QPushButton(I18n::instance().tr(QStringLiteral("nekoAuthRegister")), formPane);
    m_switchBtn->setObjectName("dialogSecondaryBtn");
    m_switchBtn->setFixedHeight(AuthDialogChrome::kPrimaryBtnHeight);
    connect(m_switchBtn, &QPushButton::clicked, this, &LoginDialog::doOpenRegister);
    actionRow->addWidget(m_switchBtn, 1);
    m_forgotBtn = new QPushButton(I18n::instance().tr(QStringLiteral("nekoAuthAccountCenter")), formPane);
    m_forgotBtn->setObjectName("dialogSecondaryBtn");
    m_forgotBtn->setFixedHeight(AuthDialogChrome::kPrimaryBtnHeight);
    connect(m_forgotBtn, &QPushButton::clicked, this, &LoginDialog::doOpenAccountCenter);
    actionRow->addWidget(m_forgotBtn, 1);
    formLayout->addLayout(actionRow);

    // 协议勾选：登录/注册前须阅读并同意用户协议与隐私政策
    auto *consentRow = new QHBoxLayout();
    consentRow->setContentsMargins(0, 6, 0, 0);
    consentRow->setSpacing(8);

    m_consentCheck = new QCheckBox(formPane);
    m_consentCheck->setCursor(Qt::PointingHandCursor);
    connect(m_consentCheck, &QCheckBox::toggled, this, [this](bool) { refreshSubmitEnabled(); });
    consentRow->addWidget(m_consentCheck, 0, Qt::AlignTop);

    m_consentText = new QLabel(formPane);
    m_consentText->setTextFormat(Qt::RichText);
    m_consentText->setText(I18n::instance().tr(QStringLiteral("consentLoginText")));
    m_consentText->setWordWrap(true);
    m_consentText->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(m_consentText, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link == QLatin1String("agreement"))
            LegalDialog::showUserAgreement(this);
        else
            LegalDialog::showPrivacyPolicy(this);
    });
    consentRow->addWidget(m_consentText, 1);
    formLayout->addLayout(consentRow);

    body->addWidget(formPane, 1);
    mainLayout->addLayout(body, 1);
    outer->addWidget(m_card);

    refreshSubmitEnabled();
}

void LoginDialog::refreshQrSession()
{
    stopQrSession();

    const int generation = m_qrGeneration;

    m_api->createQrLoginSession(
        [this, generation](bool ok, const QString &message, const ApiClient::QrLoginSession &session) {
            QTimer::singleShot(0, this, [this, generation, ok, message, session]() {
                if (generation != m_qrGeneration)
                    return;

                if (!ok || session.sessionId.isEmpty()) {
                    m_qrImageLabel->clear();
                    return;
                }

                // 纯服务端渲染：只展示后端返回的成品二维码（中心已合成软件图标）
                const QPixmap qr = pixmapFromDataUrl(session.qrImage);
                if (qr.isNull()) {
                    m_qrImageLabel->clear();
                    return;
                }

                m_qrImageLabel->setPixmap(qr.scaled(204, 204, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                startQrWatch(session.sessionId, session.deliveryKey, generation);
            });
        });
}

void LoginDialog::startQrWatch(const QString &sessionId, const QString &deliveryKey, int generation)
{
    ApiClient::QrLoginSseCallbacks callbacks;

    callbacks.onStatus = [this, generation, sessionId, deliveryKey](const ApiClient::QrLoginStatus &status) {
        QTimer::singleShot(0, this, [this, generation, status, sessionId, deliveryKey]() {
            if (generation != m_qrGeneration)
                return;

            if (status.status == QLatin1String("confirmed")) {
                // 状态帧不带 token：只断本地流，再由创建方带投递密钥单独领取一次（不能再动 generation）
                if (m_qrReply) {
                    QNetworkReply *reply = m_qrReply;
                    m_qrReply = nullptr;
                    if (!reply->isFinished())
                        reply->abort();
                }
                m_api->claimQrLoginToken(sessionId, deliveryKey,
                    [this, generation](const ApiClient::QrLoginStatus &claimed) {
                        QTimer::singleShot(0, this, [this, generation, claimed]() {
                            if (generation != m_qrGeneration)
                                return;
                            if (claimed.status == QLatin1String("confirmed")
                                && !claimed.token.isEmpty() && !claimed.user.isEmpty()) {
                                UserManager::instance().setLoginInfo(claimed.token, claimed.user);
                                accept();
                                return;
                            }
                            m_qrImageLabel->clear();
                            refreshQrSession();
                        });
                    });
            } else if (status.status == QLatin1String("expired")) {
                QTimer::singleShot(0, this, [this, generation]() {
                    if (generation == m_qrGeneration)
                        refreshQrSession();
                });
            } else if (status.status == QLatin1String("canceled")) {
                m_qrImageLabel->clear();
            }
        });
    };

    callbacks.onError = [this, generation](const QString &) {
        QTimer::singleShot(0, this, [this, generation]() {
            if (generation != m_qrGeneration)
                return;
            QTimer::singleShot(1500, this, [this, generation]() {
                if (generation == m_qrGeneration)
                    refreshQrSession();
            });
        });
    };

    m_qrReply = m_api->watchQrLoginStatus(sessionId, callbacks);
    if (m_qrReply) {
        QNetworkReply *reply = m_qrReply;
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            if (m_qrReply == reply)
                m_qrReply = nullptr;
        });
    }
}

void LoginDialog::stopQrSession()
{
    ++m_qrGeneration; // 让在途回调失效

    if (!m_qrReply)
        return;
    QNetworkReply *reply = m_qrReply;
    m_qrReply = nullptr;
    if (!reply->isFinished())
        reply->abort();
}

void LoginDialog::doSsoLogin()
{
    if (!ensureConsent())
        return;
    if (m_loading)
        return;
    m_loading = true;
    m_ssoBtn->setEnabled(false);
    m_ssoBtn->setText(I18n::instance().tr(QStringLiteral("loadingShort")));
    setMsg(I18n::instance().tr(QStringLiteral("nekoAuthSsoOpening")), Theme::kSakura);
    m_api->startSso(QStringLiteral("pc"), QStringLiteral("/"),
                    [this](bool ok, const QString &message, const QString &url) {
        m_loading = false;
        m_ssoBtn->setEnabled(true);
        m_ssoBtn->setText(I18n::instance().tr(QStringLiteral("nekoAuthSsoLogin")));
        if (!ok || url.isEmpty()) {
            setMsg(message.isEmpty()
                           ? I18n::instance().tr(QStringLiteral("nekoAuthSsoFailed"))
                           : message,
                   Theme::kSakura);
            return;
        }
        if (!QDesktopServices::openUrl(QUrl(url))) {
            setMsg(I18n::instance().tr(QStringLiteral("nekoAuthSsoFailed")), Theme::kSakura);
            return;
        }
        setMsg(I18n::instance().tr(QStringLiteral("nekoAuthSsoWaiting")), Theme::kSakura);
    });
}

void LoginDialog::doOpenRegister()
{
    if (!m_api)
        return;
    setMsg(I18n::instance().tr(QStringLiteral("nekoAuthSsoOpening")), Theme::kSakura);
    m_api->fetchPortalUrl(QStringLiteral("register"),
                          [this](bool ok, const QString &message, const QString &url) {
        if (!ok || url.isEmpty()) {
            setMsg(message.isEmpty()
                           ? I18n::instance().tr(QStringLiteral("nekoAuthSsoFailed"))
                           : message,
                   Theme::kSakura);
            return;
        }
        if (!QDesktopServices::openUrl(QUrl(url))) {
            setMsg(I18n::instance().tr(QStringLiteral("nekoAuthSsoFailed")), Theme::kSakura);
            return;
        }
        setMsg(I18n::instance().tr(QStringLiteral("nekoAuthRegisterOpened")), Theme::kSakura);
    });
}

void LoginDialog::doOpenAccountCenter()
{
    if (!m_api)
        return;
    m_api->fetchPortalUrl(QStringLiteral("account"),
                          [this](bool ok, const QString &message, const QString &url) {
        if (!ok || url.isEmpty()) {
            setMsg(message.isEmpty()
                           ? I18n::instance().tr(QStringLiteral("nekoAuthSsoFailed"))
                           : message,
                   Theme::kSakura);
            return;
        }
        if (!QDesktopServices::openUrl(QUrl(url))) {
            setMsg(I18n::instance().tr(QStringLiteral("nekoAuthSsoFailed")), Theme::kSakura);
            return;
        }
        setMsg(I18n::instance().tr(QStringLiteral("nekoAuthAccountOpened")), Theme::kSakura);
    });
}

bool LoginDialog::ensureConsent()
{
    if (m_consentCheck && m_consentCheck->isChecked())
        return true;
    setMsg(I18n::instance().tr(QStringLiteral("consentRequired")), Theme::kSakura);
    return false;
}

void LoginDialog::refreshSubmitEnabled()
{
    const bool agreed = m_consentCheck ? m_consentCheck->isChecked() : true;
    if (m_ssoBtn && !m_loading)
        m_ssoBtn->setEnabled(agreed);
}

void LoginDialog::setMsg(const QString &text, const QColor &color)
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
