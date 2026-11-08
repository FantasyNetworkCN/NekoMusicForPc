#include "videorenderdialog.h"
#include "core/apiclient.h"
#include "core/i18n.h"
#include "core/usermanager.h"
#include "theme/theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QCheckBox>
#include <QTimer>

namespace {

QString formatClipTime(int sec)
{
    sec = qMax(0, sec);
    const int m = sec / 60;
    const int s = sec % 60;
    if (m > 0)
        return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
    return QStringLiteral("0:%1").arg(s, 2, 10, QChar('0'));
}

} // namespace

VideoRenderDialog::VideoRenderDialog(ApiClient *api,
                                     int musicId,
                                     const QString &title,
                                     const QString &artist,
                                     int trackDurationSec,
                                     QWidget *parent)
    : QDialog(parent)
    , m_api(api)
    , m_musicId(musicId)
    , m_songTitle(title)
    , m_songArtist(artist)
    , m_trackDurationSec(qMax(1, trackDurationSec))
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    setFixedWidth(420);
    setupUi();
    syncVipAndApply();
}

double VideoRenderDialog::startSec() const
{
    return m_startSlider ? static_cast<double>(m_startSlider->value()) : 0.0;
}

bool VideoRenderDialog::watermarked() const
{
    if (!m_watermarkCheck)
        return true;
    return m_isVip ? m_watermarkCheck->isChecked() : true;
}

void VideoRenderDialog::setupUi()
{
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);

    auto *container = new QWidget(this);
    container->setObjectName(QStringLiteral("videoRenderDialogBg"));
    container->setStyleSheet(
        QStringLiteral(
            "QWidget#videoRenderDialogBg { "
            "  background: rgba(30, 30, 50, 0.98); "
            "  border: 1px solid rgba(22, 119, 184, 0.35); "
            "  border-radius: 16px; "
            "}"));

    auto *lay = new QVBoxLayout(container);
    lay->setContentsMargins(28, 24, 28, 22);
    lay->setSpacing(14);

    auto *titleLbl = new QLabel(I18n::instance().tr(QStringLiteral("videoRenderDialogTitle")), container);
    titleLbl->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: white;"));
    titleLbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(titleLbl);

    auto *songLbl = new QLabel(
        QStringLiteral("%1 · %2").arg(m_songTitle, m_songArtist), container);
    songLbl->setStyleSheet(QStringLiteral("font-size: 13px; color: rgba(255,255,255,0.65);"));
    songLbl->setWordWrap(true);
    songLbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(songLbl);

    m_hintLbl = new QLabel(container);
    m_hintLbl->setWordWrap(true);
    m_hintLbl->setStyleSheet(QStringLiteral("font-size: 12px; color: rgba(255,255,255,0.55); line-height: 1.4;"));
    lay->addWidget(m_hintLbl);

    auto *startRow = new QHBoxLayout();
    auto *startTitle = new QLabel(I18n::instance().tr(QStringLiteral("videoRenderStartLabel")), container);
    startTitle->setStyleSheet(QStringLiteral("font-size: 14px; color: rgba(255,255,255,0.85);"));
    m_rangeLbl = new QLabel(container);
    m_rangeLbl->setStyleSheet(
        QStringLiteral("font-size: 13px; font-weight: 600; color: %1;").arg(QString::fromUtf8(Theme::kLavender)));
    startRow->addWidget(startTitle);
    startRow->addStretch();
    startRow->addWidget(m_rangeLbl);
    lay->addLayout(startRow);

    m_startSlider = new QSlider(Qt::Horizontal, container);
    m_startSlider->setMinimum(0);
    m_startSlider->setMaximum(0);
    m_startSlider->setStyleSheet(
        QStringLiteral(
            "QSlider::groove:horizontal { height: 6px; background: rgba(255,255,255,0.12); border-radius: 3px; }"
            "QSlider::handle:horizontal { width: 14px; margin: -5px 0; background: %1; border-radius: 7px; }"
            "QSlider::sub-page:horizontal { background: rgba(22, 119, 184,0.55); border-radius: 3px; }")
            .arg(QString::fromUtf8(Theme::kLavender)));
    connect(m_startSlider, &QSlider::valueChanged, this, [this](int) { refreshRangeLabel(); });
    lay->addWidget(m_startSlider);

    m_watermarkCheck = new QCheckBox(I18n::instance().tr(QStringLiteral("videoRenderWatermark")), container);
    m_watermarkCheck->setStyleSheet(QStringLiteral("font-size: 14px; color: rgba(255,255,255,0.88);"));
    lay->addWidget(m_watermarkCheck);

    auto *noticeLbl = new QLabel(I18n::instance().tr(QStringLiteral("videoRenderEmailNotice")), container);
    noticeLbl->setWordWrap(true);
    noticeLbl->setStyleSheet(QStringLiteral("font-size: 11px; color: rgba(255,255,255,0.45);"));
    lay->addWidget(noticeLbl);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    auto *cancelBtn = new QPushButton(I18n::instance().tr(QStringLiteral("cancel")), container);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        QStringLiteral(
            "QPushButton { padding: 10px 18px; border-radius: 10px; "
            "background: rgba(255,255,255,0.08); color: rgba(255,255,255,0.85); border: 1px solid rgba(255,255,255,0.15); }"
            "QPushButton:hover { background: rgba(255,255,255,0.14); }"));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_submitBtn = new QPushButton(I18n::instance().tr(QStringLiteral("videoRenderSubmit")), container);
    m_submitBtn->setCursor(Qt::PointingHandCursor);
    m_submitBtn->setStyleSheet(
        QStringLiteral(
            "QPushButton { padding: 10px 18px; border-radius: 10px; "
            "background: rgba(22, 119, 184,0.75); color: white; font-weight: 600; border: none; }"
            "QPushButton:hover { background: rgba(22, 119, 184,0.92); }"
            "QPushButton:disabled { background: rgba(120,120,120,0.4); color: rgba(255,255,255,0.4); }"));
    connect(m_submitBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnRow->addWidget(cancelBtn, 1);
    btnRow->addWidget(m_submitBtn, 1);
    lay->addLayout(btnRow);

    mainLay->addWidget(container);
}

void VideoRenderDialog::refreshRangeLabel()
{
    if (!m_rangeLbl || !m_startSlider)
        return;
    const int start = m_startSlider->value();
    const int end = qMin(m_trackDurationSec, start + m_clipSec);
    m_rangeLbl->setText(
        I18n::instance().tr(QStringLiteral("videoRenderRangeValue"))
            .arg(formatClipTime(start))
            .arg(formatClipTime(end)));
}

void VideoRenderDialog::syncVipAndApply()
{
    m_isVip = UserManager::instance().isVip();
    m_clipSec = m_isVip ? m_trackDurationSec : 30;
    const int maxStart = qMax(0, m_trackDurationSec - m_clipSec);

    if (m_hintLbl) {
        m_hintLbl->setText(m_isVip
                               ? I18n::instance().tr(QStringLiteral("videoRenderVipHint"))
                               : I18n::instance().tr(QStringLiteral("videoRenderFreeHint")));
    }
    if (m_startSlider) {
        m_startSlider->setMaximum(maxStart);
        m_startSlider->setValue(qMin(m_startSlider->value(), maxStart));
        m_startSlider->setEnabled(maxStart > 0 || m_trackDurationSec > m_clipSec);
    }
    if (m_watermarkCheck) {
        m_watermarkCheck->setChecked(!m_isVip);
        m_watermarkCheck->setEnabled(m_isVip);
    }
    refreshRangeLabel();

    if (!m_api || !UserManager::instance().isLoggedIn())
        return;

    m_api->syncSessionVipStatus([this](bool ok, bool isVip) {
        if (!ok)
            return;
        m_isVip = isVip;
        m_clipSec = m_isVip ? m_trackDurationSec : 30;
        const int maxStart = qMax(0, m_trackDurationSec - m_clipSec);
        if (m_hintLbl) {
            m_hintLbl->setText(m_isVip
                                   ? I18n::instance().tr(QStringLiteral("videoRenderVipHint"))
                                   : I18n::instance().tr(QStringLiteral("videoRenderFreeHint")));
        }
        if (m_startSlider) {
            m_startSlider->setMaximum(maxStart);
            m_startSlider->setValue(qMin(m_startSlider->value(), maxStart));
        }
        if (m_watermarkCheck) {
            m_watermarkCheck->setChecked(!m_isVip);
            m_watermarkCheck->setEnabled(m_isVip);
        }
        refreshRangeLabel();
    });
}
