#include "stemmixerdialog.h"

#include "core/apiclient.h"
#include "core/i18n.h"
#include "core/nekonetworkaccessmanager.h"
#include "core/playerengine.h"
#include "core/usermanager.h"
#include "theme/thememanager.h"

#include <QColor>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <memory>

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

StemMixerDialog::StemMixerDialog(ApiClient *apiClient, PlayerEngine *engine,
                                 const MusicInfo &music, QWidget *parent)
    : QDialog(parent)
    , m_apiClient(apiClient)
    , m_engine(engine)
    , m_music(music)
{
    setWindowTitle(I18n::instance().tr(QStringLiteral("volumeAndStem")));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setModal(true);
    setFixedSize(520, 410);
    m_cacheNam = new NekoNetworkAccessManager(this);
    setupUi();

    if (!UserManager::instance().isLoggedIn()) {
        setStatus(I18n::instance().tr(QStringLiteral("stemLoginRequired")));
        m_stemToggle->setEnabled(false);
        return;
    }
    if (!UserManager::instance().isVip()) {
        setStatus(I18n::instance().tr(QStringLiteral("stemVipRequired")));
        m_stemToggle->setEnabled(false);
        return;
    }
    setStatus(I18n::instance().tr(QStringLiteral("stemEnableHint")));
}

StemMixerDialog::~StemMixerDialog()
{
    setStemActive(false);
    stopPlayback();
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
                      "QCheckBox#stemToggle { color: %3; spacing: 8px; font-weight: 600; }"
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
                              .arg(I18n::instance().tr(QStringLiteral("volumeAndStem")),
                                   m_music.title));
    QFont titleFont = m_titleLabel->font();
    titleFont.setPixelSize(20);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    root->addWidget(m_titleLabel);

    auto *hint = new QLabel(I18n::instance().tr(QStringLiteral("volumeAndStemHint")), this);
    hint->setObjectName(QStringLiteral("stemSub"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *volumeRow = new QWidget(this);
    auto *volumeLay = new QHBoxLayout(volumeRow);
    volumeLay->setContentsMargins(0, 0, 0, 0);
    volumeLay->setSpacing(12);
    auto *volumeName = new QLabel(I18n::instance().tr(QStringLiteral("volume")), volumeRow);
    volumeName->setMinimumWidth(48);
    m_masterSlider = new QSlider(Qt::Horizontal, volumeRow);
    m_masterSlider->setRange(0, kSliderMax);
    const int initialMaster = qBound(0, qRound((m_engine ? m_engine->volume() : 0.8f)
                                               * kSliderMax), kSliderMax);
    m_masterSlider->setValue(initialMaster);
    m_masterValue = new QLabel(percentText(initialMaster), volumeRow);
    m_masterValue->setMinimumWidth(42);
    m_masterValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    volumeLay->addWidget(volumeName);
    volumeLay->addWidget(m_masterSlider, 1);
    volumeLay->addWidget(m_masterValue);
    root->addWidget(volumeRow);
    connect(m_masterSlider, &QSlider::valueChanged, this, [this](int value) {
        m_masterValue->setText(percentText(value));
        if (m_stemActive && m_stemSwitched)
            applyStemVolumes();
        else if (m_engine)
            m_engine->setVolume(value / qreal(kSliderMax));
    });

    auto *stemSection = new QLabel(I18n::instance().tr(QStringLiteral("stemSeparation")), this);
    QFont sectionFont = stemSection->font();
    sectionFont.setPixelSize(14);
    sectionFont.setBold(true);
    stemSection->setFont(sectionFont);
    root->addWidget(stemSection);

    m_stemToggle = new QCheckBox(I18n::instance().tr(QStringLiteral("stemEnable")), this);
    m_stemToggle->setObjectName(QStringLiteral("stemToggle"));
    m_stemToggle->setCursor(Qt::PointingHandCursor);
    root->addWidget(m_stemToggle);

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
    m_vocalsSlider->setEnabled(false);
    m_accompanimentSlider->setEnabled(false);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("stemSub"));
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    connect(m_vocalsSlider, &QSlider::valueChanged, this, [this](int value) {
        m_vocalsValue->setText(percentText(value));
        applyStemVolumes();
    });
    connect(m_accompanimentSlider, &QSlider::valueChanged, this, [this](int value) {
        m_accompanimentValue->setText(percentText(value));
        applyStemVolumes();
    });
    connect(m_stemToggle, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && !m_tracksReady) {
            m_pendingEnable = true;
            setStatus(I18n::instance().tr(QStringLiteral("stemPreparing")));
            m_stemToggle->setEnabled(false);
            fetchFiles();
            return;
        }
        setStemActive(checked);
    });

    auto *actions = new QHBoxLayout();
    actions->addStretch(1);
    m_resetMixButton = new QPushButton(I18n::instance().tr(QStringLiteral("stemResetMix")), this);
    m_resetMixButton->setObjectName(QStringLiteral("stemClose"));
    m_closeButton = new QPushButton(I18n::instance().tr(QStringLiteral("close")), this);
    m_closeButton->setObjectName(QStringLiteral("stemClose"));
    actions->addWidget(m_resetMixButton);
    actions->addWidget(m_closeButton);
    root->addLayout(actions);

    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_resetMixButton, &QPushButton::clicked, this, [this]() {
        m_vocalsSlider->setValue(kSliderMax);
        m_accompanimentSlider->setValue(kSliderMax);
    });
    m_syncTimer = new QTimer(this);
    m_syncTimer->setInterval(kSyncIntervalMs);
    connect(m_syncTimer, &QTimer::timeout, this, &StemMixerDialog::syncPositions);
    if (m_engine) {
        connect(m_engine, &PlayerEngine::stateChanged, this, &StemMixerDialog::syncTransport);
        connect(m_engine, &PlayerEngine::playbackFinished, this, [this]() {
            if (m_stemActive)
                setStemActive(false);
        });
    }

    setStatus(I18n::instance().tr(QStringLiteral("stemEnableHint")));
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
                m_pendingEnable = false;
                m_stemToggle->setEnabled(true);
                m_stemToggle->setChecked(false);
                setStatus(message.isEmpty() ? I18n::instance().tr(QStringLiteral("stemFailed"))
                                            : message);
                return;
            }
            loadCachedOrDownload(vocals, accompaniment);
        });
}

QString StemMixerDialog::stemCacheDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return base + QStringLiteral("/stem-cache");
}

void StemMixerDialog::loadCachedOrDownload(const QString &vocalsUrl,
                                           const QString &accompanimentUrl)
{
    if (!m_stemActive) {
        m_pendingEnable = false;
        m_stemToggle->setEnabled(true);
        m_stemToggle->setChecked(false);
        return;
    }
    const QString keySeed = QString::number(m_music.id) + '|' + vocalsUrl + '|' + accompanimentUrl;
    const QString key = QString::number(m_music.id) + '-'
            + QString::fromLatin1(QCryptographicHash::hash(
                keySeed.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    QDir().mkpath(stemCacheDir());
    const QString vocalsPath = stemCacheDir() + '/' + key + QStringLiteral("-vocals.mp3");
    const QString accompanimentPath = stemCacheDir() + '/' + key + QStringLiteral("-accompaniment.mp3");

    if (QFileInfo::exists(vocalsPath) && QFileInfo(vocalsPath).size() > 0
        && QFileInfo::exists(accompanimentPath) && QFileInfo(accompanimentPath).size() > 0) {
        prepareTracks(QUrl::fromLocalFile(vocalsPath), QUrl::fromLocalFile(accompanimentPath));
        return;
    }

    setStatus(I18n::instance().tr(QStringLiteral("stemLoadingCache")));
    auto finishDownload = [this, vocalsPath, accompanimentPath](bool ok, const QString &message) {
        if (!ok) {
            m_pendingEnable = false;
            m_stemActive = false;
            m_stemToggle->setEnabled(true);
            m_stemToggle->setChecked(false);
            setStatus(message.isEmpty() ? I18n::instance().tr(QStringLiteral("stemFailed"))
                                        : message);
            return;
        }
        prepareTracks(QUrl::fromLocalFile(vocalsPath), QUrl::fromLocalFile(accompanimentPath));
    };

    auto downloadOne = [this](const QString &url, const QString &destination,
                              std::function<void(bool, const QString &)> done) {
        const QString part = destination + QStringLiteral(".part");
        QFile::remove(part);
        auto *reply = m_cacheNam->get(QNetworkRequest(QUrl(url)));
        connect(reply, &QNetworkReply::finished, this,
                [reply, part, destination, done]() {
                    reply->deleteLater();
                    if (reply->error() != QNetworkReply::NoError) {
                        QFile::remove(part);
                        done(false, reply->errorString());
                        return;
                    }
                    QFile file(part);
                    if (!file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) <= 0) {
                        file.close();
                        QFile::remove(part);
                        done(false, QStringLiteral("分离音频缓存写入失败"));
                        return;
                    }
                    file.close();
                    QFile::remove(destination);
                    if (!QFile::rename(part, destination)) {
                        QFile::remove(part);
                        done(false, QStringLiteral("分离音频缓存保存失败"));
                        return;
                    }
                    done(true, {});
                });
    };

    auto state = std::make_shared<int>(0);
    auto firstError = std::make_shared<QString>();
    auto complete = [state, firstError, finishDownload](bool ok, const QString &message) {
        if (!ok && firstError->isEmpty())
            *firstError = message;
        ++*state;
        if (*state == 2)
            finishDownload(firstError->isEmpty(), *firstError);
    };
    downloadOne(vocalsUrl, vocalsPath, complete);
    downloadOne(accompanimentUrl, accompanimentPath, complete);
}

void StemMixerDialog::prepareTracks(const QUrl &vocalsUrl, const QUrl &accompanimentUrl)
{
    m_vocalsUrl = vocalsUrl;
    m_accompanimentUrl = accompanimentUrl;
    m_tracksReady = !vocalsUrl.isEmpty() && !accompanimentUrl.isEmpty();
    updateStemControls();
    if (m_pendingEnable) {
        m_pendingEnable = false;
        setStemActive(true);
    }
}

void StemMixerDialog::updateStemControls()
{
    if (!m_tracksReady)
        return;
    m_vocalsSlider->setEnabled(true);
    m_accompanimentSlider->setEnabled(true);
    m_stemToggle->setEnabled(true);
    setStatus(I18n::instance().tr(QStringLiteral("stemReady")));
}

void StemMixerDialog::setStemActive(bool active)
{
    if (active == m_stemActive)
        return;
    if (!active) {
        m_stemActive = false;
        m_stemSwitched = false;
        m_vocalsLoaded = false;
        m_accompanimentLoaded = false;
        m_syncTimer->stop();
        if (m_vocalsEngine)
            m_vocalsEngine->stop();
        if (m_accompanimentEngine)
            m_accompanimentEngine->stop();
        if (m_engine)
            m_engine->setVolume(m_masterSlider->value() / qreal(kSliderMax));
        setStatus(I18n::instance().tr(QStringLiteral("stemDisabled")));
        return;
    }
    if (!m_tracksReady) {
        m_pendingEnable = true;
        return;
    }

    if (!m_vocalsEngine) {
        m_vocalsEngine = new PlayerEngine(this);
        m_accompanimentEngine = new PlayerEngine(this);
        connect(m_vocalsEngine, &PlayerEngine::mediaError, this, [this](const QString &message) {
            m_stemToggle->setChecked(false);
            setStatus(message.isEmpty() ? I18n::instance().tr(QStringLiteral("stemFailed"))
                                        : message);
        });
        connect(m_accompanimentEngine, &PlayerEngine::mediaError, this, [this](const QString &message) {
            m_stemToggle->setChecked(false);
            setStatus(message.isEmpty() ? I18n::instance().tr(QStringLiteral("stemFailed"))
                                        : message);
        });
        connect(m_vocalsEngine, &PlayerEngine::playbackFinished, this, [this]() {
            if (m_accompanimentEngine)
                m_accompanimentEngine->stop();
            if (m_syncTimer)
                m_syncTimer->stop();
        });
        connect(m_vocalsEngine, &PlayerEngine::durationChanged, this, [this](qint64 duration) {
            if (duration <= 0)
                return;
            m_vocalsLoaded = true;
            if (m_vocalsLoaded && m_accompanimentLoaded)
                finishStemSwitch();
        });
        connect(m_accompanimentEngine, &PlayerEngine::durationChanged, this, [this](qint64 duration) {
            if (duration <= 0)
                return;
            m_accompanimentLoaded = true;
            if (m_vocalsLoaded && m_accompanimentLoaded)
                finishStemSwitch();
        });
    }

    m_stemActive = true;
    m_stemSwitched = false;
    m_vocalsLoaded = false;
    m_accompanimentLoaded = false;
    m_vocalsEngine->setVolume(0.0f);
    m_accompanimentEngine->setVolume(0.0f);
    const qint64 position = m_engine ? m_engine->position() : 0;
    m_vocalsEngine->playResuming(m_vocalsUrl, position);
    m_accompanimentEngine->playResuming(m_accompanimentUrl, position);
    syncTransport();
    m_syncTimer->start();
    setStatus(I18n::instance().tr(QStringLiteral("stemLoadingAudio")));
}

void StemMixerDialog::finishStemSwitch()
{
    if (!m_stemActive || m_stemSwitched || !m_vocalsEngine || !m_accompanimentEngine)
        return;
    m_stemSwitched = true;
    if (m_engine)
        m_engine->setVolume(0.0f);
    applyStemVolumes();
    syncPositions();
    syncTransport();
    setStatus(I18n::instance().tr(QStringLiteral("stemActive")));
}

void StemMixerDialog::applyStemVolumes()
{
    if (!m_stemActive || !m_stemSwitched || !m_vocalsEngine || !m_accompanimentEngine)
        return;
    const qreal master = m_masterSlider->value() / qreal(kSliderMax);
    m_vocalsEngine->setVolume(master * m_vocalsSlider->value() / qreal(kSliderMax));
    m_accompanimentEngine->setVolume(master * m_accompanimentSlider->value() / qreal(kSliderMax));
}

void StemMixerDialog::syncTransport()
{
    if (!m_stemActive || !m_engine || !m_vocalsEngine || !m_accompanimentEngine)
        return;
    switch (m_engine->playbackState()) {
    case PlayerEngine::Playing:
        m_vocalsEngine->play();
        m_accompanimentEngine->play();
        break;
    case PlayerEngine::Paused:
        m_vocalsEngine->pause();
        m_accompanimentEngine->pause();
        break;
    case PlayerEngine::Stopped:
        m_vocalsEngine->stop();
        m_accompanimentEngine->stop();
        break;
    }
}

void StemMixerDialog::syncPositions()
{
    if (!m_stemActive || !m_stemSwitched || !m_engine || !m_vocalsEngine || !m_accompanimentEngine)
        return;
    if (m_engine->playbackState() != PlayerEngine::Playing)
        return;
    const qint64 clock = m_engine->position();
    if (qAbs(m_vocalsEngine->position() - clock) > kSyncDriftMs)
        m_vocalsEngine->setPosition(clock);
    if (qAbs(m_accompanimentEngine->position() - clock) > kSyncDriftMs)
        m_accompanimentEngine->setPosition(clock);
}

void StemMixerDialog::stopPlayback()
{
    if (m_syncTimer)
        m_syncTimer->stop();
    if (m_vocalsEngine)
        m_vocalsEngine->stop();
    if (m_accompanimentEngine)
        m_accompanimentEngine->stop();
}
