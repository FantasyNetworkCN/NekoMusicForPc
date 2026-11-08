#include "lineinputdialog.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "theme/thememanager.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kOuterPad = 20;
constexpr int kCardPadH = 24;
constexpr int kDialogWidth = 400;

void polishFramelessDialog(QDialog *dlg)
{
    dlg->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint);
    dlg->setAttribute(Qt::WA_TranslucentBackground);
    dlg->setModal(true);

    auto *shadow = new QGraphicsDropShadowEffect(dlg);
    shadow->setBlurRadius(26);
    shadow->setOffset(0, 4);
    shadow->setColor(QColor(0, 0, 0, 100));
    dlg->setGraphicsEffect(shadow);
}

} // namespace

LineInputDialog::LineInputDialog(QWidget *parent,
                                 const QString &title,
                                 const QString &fieldLabel,
                                 const QString &placeholder,
                                 const QString &initialValue,
                                 const QString &confirmButtonText,
                                 bool allowEmptySubmit)
    : QDialog(parent)
    , m_dialogTitle(title)
    , m_fieldLabel(fieldLabel)
    , m_placeholder(placeholder)
    , m_initial(initialValue)
    , m_confirmText(confirmButtonText.isEmpty() ? I18n::instance().tr(QStringLiteral("confirm"))
                                                : confirmButtonText)
    , m_allowEmpty(allowEmptySubmit)
{
    polishFramelessDialog(this);
    setFixedWidth(kDialogWidth);
    setupUi();
    adjustSize();
    setFixedHeight(qMax(232, sizeHint().height()));

    QTimer::singleShot(0, this, [this]() {
        if (m_lineEdit) {
            m_lineEdit->setFocus();
            m_lineEdit->selectAll();
        }
    });
}

QString LineInputDialog::value() const
{
    return m_lineEdit ? m_lineEdit->text().trimmed() : QString();
}

void LineInputDialog::setupUi()
{
    const int textW = kDialogWidth - 2 * kOuterPad - 2 * kCardPadH;

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kOuterPad, kOuterPad, kOuterPad, kOuterPad);
    outer->setSpacing(0);

    auto *box = new QWidget(this);
    box->setObjectName(QStringLiteral("lineInputGlassBox"));
    box->setStyleSheet(
        QStringLiteral(
            "QWidget#lineInputGlassBox {"
            "  background: %1;"
            "  border: 1px solid %2;"
            "  border-radius: %3px;"
            "}")
            .arg(QString::fromUtf8(Theme::kGlassBg), QString::fromUtf8(Theme::kBorderGlass))
            .arg(Theme::kRMd));

    auto *lay = new QVBoxLayout(box);
    lay->setContentsMargins(kCardPadH, 22, kCardPadH, 20);
    lay->setSpacing(14);

    auto *titleLbl = new QLabel(m_dialogTitle, box);
    titleLbl->setFixedWidth(textW);
    titleLbl->setWordWrap(true);
    titleLbl->setStyleSheet(
        QStringLiteral("QLabel { font-size: 18px; font-weight: 700; color: %1; }")
            .arg(QString::fromUtf8(Theme::kTextMain)));
    lay->addWidget(titleLbl);

    auto *fieldCap = new QLabel(m_fieldLabel, box);
    fieldCap->setFixedWidth(textW);
    fieldCap->setWordWrap(true);
    fieldCap->setStyleSheet(
        QStringLiteral("QLabel { font-size: 13px; color: %1; }").arg(QString::fromUtf8(Theme::kTextSub)));
    lay->addWidget(fieldCap);

    m_lineEdit = new QLineEdit(box);
    m_lineEdit->setFixedHeight(40);
    m_lineEdit->setFixedWidth(textW);
    if (!m_placeholder.isEmpty())
        m_lineEdit->setPlaceholderText(m_placeholder);
    m_lineEdit->setText(m_initial);
    m_lineEdit->setClearButtonEnabled(true);
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    if (dark) {
        m_lineEdit->setStyleSheet(
            QStringLiteral(
                "QLineEdit {"
                "  background: rgba(36, 31, 49, 200);"
                "  border: 1px solid %1;"
                "  border-radius: 8px;"
                "  padding: 0 12px;"
                "  color: %2;"
                "  font-size: 14px;"
                "}"
                "QLineEdit:focus { border: 1px solid %3; }")
                .arg(QString::fromUtf8(Theme::kBorderGlass), QString::fromUtf8(Theme::kTextMain),
                     QString::fromUtf8(Theme::kLavender)));
    } else {
        m_lineEdit->setStyleSheet(
            QStringLiteral(
                "QLineEdit {"
                "  background: rgba(255, 255, 255, 0.94);"
                "  border: 1px solid rgba(64, 197, 241, 0.35);"
                "  border-radius: 8px;"
                "  padding: 0 12px;"
                "  color: #182B45;"
                "  font-size: 14px;"
                "}"
                "QLineEdit:focus { border: 1px solid %1; }")
                .arg(QString::fromUtf8(Theme::kLavender)));
    }
    lay->addWidget(m_lineEdit);
    connect(m_lineEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        if (m_errorLbl && m_errorLbl->isVisible())
            m_errorLbl->hide();
    });

    m_errorLbl = new QLabel(box);
    m_errorLbl->setFixedWidth(textW);
    m_errorLbl->setWordWrap(true);
    m_errorLbl->hide();
    m_errorLbl->setStyleSheet(
        QStringLiteral("QLabel { font-size: 12px; color: %1; min-height: 18px; }")
            .arg(QString::fromUtf8(Theme::kSakura)));
    lay->addWidget(m_errorLbl);

    lay->addSpacing(4);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);

    auto *cancelBtn = new QPushButton(I18n::instance().tr(QStringLiteral("cancel")), box);
    cancelBtn->setFixedHeight(40);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        QStringLiteral(
            "QPushButton {"
            "  background: rgba(255, 255, 255, 0.06);"
            "  color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 8px;"
            "  font-size: 14px;"
            "}"
            "QPushButton:hover { background: rgba(255, 255, 255, 0.1); }")
            .arg(QString::fromUtf8(Theme::kTextMain), QString::fromUtf8(Theme::kBorderGlass)));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn, 1);

    auto *okBtn = new QPushButton(m_confirmText, box);
    okBtn->setFixedHeight(40);
    okBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setStyleSheet(
        QStringLiteral(
            "QPushButton {"
            "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #8ADFF5, stop:1 #1677B8);"
            "  color: #1a1625;"
            "  border: none;"
            "  border-radius: 8px;"
            "  font-size: 14px;"
            "  font-weight: 700;"
            "}"
            "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #D4BFF0, stop:1 #B89AE8); }"));
    connect(okBtn, &QPushButton::clicked, this, [this]() {
        const QString n = value();
        if (!m_allowEmpty && n.isEmpty()) {
            m_errorLbl->setText(I18n::instance().tr(QStringLiteral("inputRequired")));
            m_errorLbl->show();
            if (m_lineEdit)
                m_lineEdit->setFocus();
            return;
        }
        accept();
    });
    connect(m_lineEdit, &QLineEdit::returnPressed, okBtn, &QPushButton::click);
    btnRow->addWidget(okBtn, 1);

    lay->addLayout(btnRow);
    outer->addWidget(box);
}
