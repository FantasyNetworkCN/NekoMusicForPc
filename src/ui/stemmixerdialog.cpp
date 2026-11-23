#include "stemmixerdialog.h"

#include "core/apiclient.h"
#include "core/httpmediadevice.h"
#include "core/i18n.h"
#include "core/usermanager.h"
#include "theme/thememanager.h"

#include <QAudioOutput>
#include <QColor>
#include <QFile>
#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace {

constexpr int kSliderMax = 100;
constexpr int kDefaultGain = 80;
constexpr int kSyncIntervalMs = 500;
constexpr qint64 kSyncDriftMs = 120;

QString percentText(int value)
{
    return QStringLiteral("%1%").arg(value);
}

} // namespace

StemMixerDialog::StemMixerDialog(ApiClient *apiClient, const MusicInfo &music, QWidget *parent)
    : QDialog(parent)
    , m_apiClient(apiClient)
    , m_music(music)
{
    setWindowTitle(I18n::instance().tr(QStringLiteral("stemSeparation")));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setModal(true);
    setFixedSize(460, 292);
    setupUi();

    if (!UserManager::instance().isLoggedIn()) {
        setStatus(I18n::instance().tr(QStringLiteral("stemLoginRequired")));
        return;
    }
    if (!UserManager::instance().isVip()) {
        setStatus(I18n::instance().tr(QStringLiteral("stemVipRequired")));
        return;
    }
    fetchFiles();
}

StemMixerDialog::~StemMixerDialog()
{
    stopPlayback();
    if (m_vocalsDevice)
        m_vocalsDevice->shutdown();
    if (m_accompanimentDevice)
        m_accompanimentDevice->shutdown();
}

void StemMixerDialog::setupUi()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QColor bg = dark ? QColor(26, 32, 48, 248) : QColor(250, 252, 255, 248);
    const QColor fg = dark ? QColor(240, 244, 252) : QColor(30, 38, 52);
    const QColor sub = dark ? QColor(174, 190, 214) : QColor(88, 104, 126);
    const QColor accent(64, 197, 241);
    setStyleSheet(QStringLiteral(
                      "QDialog { background: %1; border: 1px solid %2; border-radius: 18px; }"
                      "QLabel { color: %3; }"
                      "QLabel#stemSub { color: %4; font-size: 13px; }"
                      "QSlider::groove:horizontal { height: 6px; border-radius: 3px; background: %5; }"
                      "QSlider::sub-page:horizontal { border-radius: 3px; background: %6; }"
                      "QSlider::handle:horizontal { width: 16px; margin: -5px 0; border-radius: 8px; background: %6; }"
                      "QPushButton#stemPlay { background: %6; color: #10202a; border: none; border-radius: 8px; padding: 8px 20px; font-weight: 600; }"
                      "QPushButton#stemClose { background: transparent; color: %4; border: 1px solid %5; border-radius: 8px; padding: 8px 16px; }")
                      .arg(bg.name(QColor::HexArgb), dark ? QStringLiteral("rgba(255,255,255,30)")
                                                           : QStringLiteral("rgba(30,60,90,22)"),
                           fg.name(QColor::HexArgb), sub.name(QColor::HexArgb),
                           dark ? QStringLiteral("rgba(255,255,255,28)")
                                : QStringLiteral("rgba(30,60,90,18)"),
                           accent.name()));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 24, 26, 22);
    root->setSpacing(14);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setText(QStringLiteral("%1 · %2")
                              .arg(I18n::instance().tr(QStringLiteral("stemSeparation")),
                                   m_music.title));
    QFont titleFont = m_titleLabel->font();
    titleFont.setPixelSize(20);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    root->addWidget(m_titleLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("stemSub"));
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    auto addGainRow = [&](const QString &label, QSlider *&slider, QLabel *&value) {
        auto *row = new QWidget(this);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(12);
        auto *name = new QLabel(label, row);
        name->setMinimumWidth(48);
        slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, kSliderMax);
        slider->setValue(kDefaultGain);
        value = new QLabel(percentText(kDefaultGain), row);
        value->setMinimumWidth(42);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lay->addWidget(name);
        lay->addWidget(slider, 1);
        lay->addWidget(value);
        root->addWidget(row);
    };
    addGainRow(I18n::instance().tr(QStringLiteral("stemVocals")), m_vocalsSlider, m_vocalsValue);
    addGainRow(I18n::instance().tr(QStringLiteral("stemAccompaniment")),
               m_accompanimentSlider, m_accompanimentValue);

    connect(m_vocalsSlider, &QSlider::valueChanged, this, [this](int value) {
        m_vocalsValue->setText(percentText(value));
        if (m_vocalsOutput)
            m_vocalsOutput->setVolume(value / qreal(kSliderMax));
    });
    connect(m_accompanimentSlider, &QSlider::valueChanged, this, [this](int value) {
        m_accompanimentValue->setText(percentText(value));
        if (m_accompanimentOutput)
            m_accompanimentOutput->setVolume(value / qreal(kSliderMax));
    });

    auto *actions = new QHBoxLayout();
    actions->addStretch(1);
    m_closeButton = new QPushButton(I18n::instance().tr(QStringLiteral("close")), this);
    m_closeButton->setObjectName(QStringLiteral("stemClose"));
    m_playButton = new QPushButton(I18n::instance().tr(QStringLiteral("play")), this);
    m_playButton->setObjectName(QStringLiteral("stemPlay"));
    m_playButton->setEnabled(false);
    actions->addWidget(m_closeButton);
    actions->addWidget(m_playButton);
    root->addLayout(actions);

    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_playButton, &QPushButton::clicked, this, &StemMixerDialog::togglePlayback);

    m_syncTimer = new QTimer(this);
    m_syncTimer->setInterval(kSyncIntervalMs);
    connect(m_syncTimer, &QTimer::timeout, this, &StemMixerDialog::syncPositions);

    setStatus(I18n::instance().tr(QStringLiteral("stemPreparing")));
}

void StemMixerDialog::setStatus(const QString &text)
{
    if (m_statusLabel)
        m_statusLabel->setText(text);
}

void StemMixerDialog::fetchFiles()
{
    if (!m_apiClient) {
        setStatus(I18n::instance().tr(QStringLiteral("stemFailed")));
        return;
    }
    m_apiClient->fetchStemFiles(
        m_music.id,
        [this](bool ok, const QString &message, const QString &vocals, const QString &accompaniment) {
            if (!ok) {
                setStatus(message.isEmpty() ? I18n::instance().tr(QStringLiteral("stemFailed"))
                                            : message);
                return;
            }
            preparePlayers(QUrl(vocals), QUrl(accompaniment));
        });
}

void StemMixerDialog::preparePlayers(const QUrl &vocalsUrl, const QUrl &accompanimentUrl)
{
    m_vocalsPlayer = new QMediaPlayer(this);
    m_vocalsOutput = new QAudioOutput(this);
    m_vocalsPlayer->setAudioOutput(m_vocalsOutput);
    m_vocalsOutput->setVolume(m_vocalsSlider->value() / qreal(kSliderMax));

    m_accompanimentPlayer = new QMediaPlayer(this);
    m_accompanimentOutput = new QAudioOutput(this);
    m_accompanimentPlayer->setAudioOutput(m_accompanimentOutput);
    m_accompanimentOutput->setVolume(m_accompanimentSlider->value() / qreal(kSliderMax));

    m_vocalsDevice = new HttpMediaDevice(vocalsUrl, this);
    m_accompanimentDevice = new HttpMediaDevice(accompanimentUrl, this);
    connect(m_vocalsDevice, &HttpMediaDevice::fetchError, this, [this](const QString &error) {
        setStatus(error);
    });
    connect(m_accompanimentDevice, &HttpMediaDevice::fetchError, this, [this](const QString &error) {
        setStatus(error);
    });

    m_vocalsDevice->open(QIODevice::ReadOnly);
    m_vocalsDevice->start();
    m_vocalsPlayer->setSourceDevice(m_vocalsDevice, vocalsUrl);
    m_accompanimentDevice->open(QIODevice::ReadOnly);
    m_accompanimentDevice->start();
    m_accompanimentPlayer->setSourceDevice(m_accompanimentDevice, accompanimentUrl);

    connect(m_vocalsPlayer, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
                    m_vocalsReady = true;
                    updatePlayButton();
                } else if (status == QMediaPlayer::EndOfMedia) {
                    if (m_accompanimentPlayer)
                        m_accompanimentPlayer->stop();
                    if (m_syncTimer)
                        m_syncTimer->stop();
                    if (m_playButton)
                        m_playButton->setText(I18n::instance().tr(QStringLiteral("play")));
                }
            });
    connect(m_accompanimentPlayer, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
                    m_accompanimentReady = true;
                    updatePlayButton();
                }
            });
    connect(m_vocalsPlayer, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &message) {
                setStatus(message.isEmpty() ? I18n::instance().tr(QStringLiteral("stemFailed"))
                                            : message);
            });
    connect(m_accompanimentPlayer, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &message) {
                setStatus(message.isEmpty() ? I18n::instance().tr(QStringLiteral("stemFailed"))
                                            : message);
            });

    setStatus(I18n::instance().tr(QStringLiteral("stemLoadingAudio")));
}

void StemMixerDialog::updatePlayButton()
{
    if (!m_playButton || !m_vocalsReady || !m_accompanimentReady)
        return;
    m_playButton->setEnabled(true);
    setStatus(I18n::instance().tr(QStringLiteral("stemReady")));
}

void StemMixerDialog::togglePlayback()
{
    if (!m_vocalsPlayer || !m_accompanimentPlayer)
        return;
    if (m_vocalsPlayer->playbackState() == QMediaPlayer::PlayingState) {
        m_vocalsPlayer->pause();
        m_accompanimentPlayer->pause();
        m_syncTimer->stop();
        m_playButton->setText(I18n::instance().tr(QStringLiteral("play")));
        return;
    }
    if (m_vocalsPlayer->position() > 0)
        m_accompanimentPlayer->setPosition(m_vocalsPlayer->position());
    m_vocalsPlayer->play();
    m_accompanimentPlayer->play();
    m_syncTimer->start();
    m_playButton->setText(I18n::instance().tr(QStringLiteral("pause")));
}

void StemMixerDialog::syncPositions()
{
    if (!m_vocalsPlayer || !m_accompanimentPlayer)
        return;
    if (m_vocalsPlayer->playbackState() != QMediaPlayer::PlayingState)
        return;
    const qint64 vocalsPos = m_vocalsPlayer->position();
    const qint64 accompanimentPos = m_accompanimentPlayer->position();
    if (qAbs(vocalsPos - accompanimentPos) > kSyncDriftMs)
        m_accompanimentPlayer->setPosition(vocalsPos);
}

void StemMixerDialog::stopPlayback()
{
    if (m_syncTimer)
        m_syncTimer->stop();
    if (m_vocalsPlayer)
        m_vocalsPlayer->stop();
    if (m_accompanimentPlayer)
        m_accompanimentPlayer->stop();
}
