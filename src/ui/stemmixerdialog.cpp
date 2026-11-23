#include "stemmixerdialog.h"

#include "core/apiclient.h"
#include "core/i18n.h"
#include "core/nekonetworkaccessmanager.h"
#include "core/playerengine.h"
#include "core/usermanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"

#include <QCheckBox>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

namespace {

constexpr int kSliderMax = 100;
constexpr int kDefaultGain = 80;
constexpr int kSyncIntervalMs = 500;
constexpr qint64 kSyncDriftMs = 120;
constexpr int kStemSwitchTimeoutMs = 20000;
constexpr qint64 kCacheTtlMs = 180LL * 24 * 60 * 60 * 1000;

QString percentText(int value)
{
    return QStringLiteral("%1%").arg(value);
}

bool cachedFileReady(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0)
        return false;
    const QDateTime modified = info.lastModified();
    if (!modified.isValid())
        return true;
    return QDateTime::currentMSecsSinceEpoch() - modified.toMSecsSinceEpoch() < kCacheTtlMs;
}

QString absoluteMediaUrl(QString value)
{
    value = value.trimmed();
    if (value.startsWith(QStringLiteral("http://"))
        || value.startsWith(QStringLiteral("https://"))) {
        return value;
    }
    const QString base = QString::fromUtf8(Theme::kApiBase);
    return base + (value.startsWith('/') ? value : QStringLiteral("/") + value);
}

qint64 secondsTextToMs(const QVariant &value)
{
    bool ok = false;
    const double seconds = value.toDouble(&ok);
    return ok ? qMax<qint64>(0, qRound64(seconds * 1000.0)) : 0;
}

QString chunkPathsKey(int index)
{
    return QString::number(index);
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
    setModal(false);
    setWindowModality(Qt::NonModal);
    setFixedSize(520, 452);
    m_cacheNam = new NekoNetworkAccessManager(this);
    setupUi();
    loadCachedStems();

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

    updateStemControls();
    if (m_fullReady || !m_readyChunks.isEmpty())
        setStatus(I18n::instance().tr(QStringLiteral("stemCacheRestored")));
    else
        setStatus(I18n::instance().tr(QStringLiteral("stemEnableHint")));
}

StemMixerDialog::~StemMixerDialog()
{
    m_stemActive = false;
    stopStemStream();
    stopPlayback();
    if (m_engine)
        m_engine->setVolume(m_masterSlider ? m_masterSlider->value() / qreal(kSliderMax)
                                           : 1.0f);
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
                      "QProgressBar { max-height: 6px; border: 0; border-radius: 3px; background: %5; }"
                      "QProgressBar::chunk { border-radius: 3px; background: %6; }"
                      "QPushButton#stemClose { background: transparent; color: %4; border: 1px solid %5; border-radius: 8px; padding: 8px 16px; }")
                      .arg(bg.name(QColor::HexArgb), dark ? QStringLiteral("rgba(255,255,255,30)")
                                                           : QStringLiteral("rgba(30,60,90,22)"),
                           fg.name(QColor::HexArgb), sub.name(QColor::HexArgb),
                           dark ? QStringLiteral("rgba(255,255,255,28)")
                                : QStringLiteral("rgba(30,60,90,18)"),
                           accent.name()));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(26, 24, 26, 22);
    root->setSpacing(13);

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
    QSettings savedGains;
    const int defaultMaster = qBound(0, qRound((m_engine ? m_engine->volume() : 0.8f)
                                               * kSliderMax), kSliderMax);
    const int initialMaster = qBound(0, savedGains.value(
        QStringLiteral("stem/masterGain"), defaultMaster).toInt(), kSliderMax);
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
        persistGains();
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

    auto addGainRow = [&](const QString &label, int initialValue,
                          QSlider *&slider, QLabel *&value) {
        auto *row = new QWidget(this);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(12);
        auto *name = new QLabel(label, row);
        name->setMinimumWidth(48);
        slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, kSliderMax);
        slider->setValue(initialValue);
        value = new QLabel(percentText(initialValue), row);
        value->setMinimumWidth(42);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lay->addWidget(name);
        lay->addWidget(slider, 1);
        lay->addWidget(value);
        root->addWidget(row);
    };
    const int initialVocals = qBound(0, savedGains.value(
        QStringLiteral("stem/vocalsGain"), kDefaultGain).toInt(), kSliderMax);
    const int initialAccompaniment = qBound(0, savedGains.value(
        QStringLiteral("stem/accompanimentGain"), kDefaultGain).toInt(), kSliderMax);
    addGainRow(I18n::instance().tr(QStringLiteral("stemVocals")), initialVocals,
               m_vocalsSlider, m_vocalsValue);
    addGainRow(I18n::instance().tr(QStringLiteral("stemAccompaniment")), initialAccompaniment,
               m_accompanimentSlider, m_accompanimentValue);
    m_vocalsSlider->setEnabled(false);
    m_accompanimentSlider->setEnabled(false);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setTextVisible(false);
    m_progressBar->setVisible(false);
    root->addWidget(m_progressBar);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("stemSub"));
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    connect(m_vocalsSlider, &QSlider::valueChanged, this, [this](int value) {
        m_vocalsValue->setText(percentText(value));
        persistGains();
        applyStemVolumes();
    });
    connect(m_accompanimentSlider, &QSlider::valueChanged, this, [this](int value) {
        m_accompanimentValue->setText(percentText(value));
        persistGains();
        applyStemVolumes();
    });
    connect(m_stemToggle, &QCheckBox::toggled, this, [this](bool checked) {
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

    m_switchTimeout = new QTimer(this);
    m_switchTimeout->setSingleShot(true);
    m_switchTimeout->setInterval(kStemSwitchTimeoutMs);
    connect(m_switchTimeout, &QTimer::timeout, this, [this]() {
        if (!m_switchPending)
            return;
        const int failedChunk = m_loadingChunkIndex;
        const StemSource failedSource = m_loadingSource;
        if (failedSource == StemSource::Chunk && failedChunk >= 0)
            m_failedChunks.insert(failedChunk);
        else if (failedSource == StemSource::Full)
            m_fullFailed = true;
        stopStemPlayback(true);
        setStatus(I18n::instance().tr(QStringLiteral("stemPlaybackFallback")));
    });

    if (m_engine) {
        connect(m_engine, &PlayerEngine::stateChanged, this, [this]() {
            if (!m_stemActive)
                return;
            syncTransport();
            syncPositions();
        });
        connect(m_engine, &PlayerEngine::positionChanged, this, [this]() {
            if (m_stemActive && !m_switchPending)
                syncPositions();
        });
        connect(m_engine, &PlayerEngine::playbackFinished, this, [this]() {
            if (m_stemActive) {
                stopStemPlayback(false);
                setStatus(I18n::instance().tr(QStringLiteral("stemTrackFinished")));
            }
        });
        connect(m_engine, &PlayerEngine::musicStarted, this,
                [this](const MusicInfo &info) {
                    if (m_stemActive && info.id != m_music.id)
                        m_stemToggle->setChecked(false);
                });
    }
}

void StemMixerDialog::ensurePlayers()
{
    if (m_vocalsEngine && m_accompanimentEngine)
        return;

    m_vocalsEngine = new PlayerEngine(this);
    m_accompanimentEngine = new PlayerEngine(this);
    if (m_engine) {
        m_vocalsEngine->setOutputDevice(m_engine->outputDevice());
        m_accompanimentEngine->setOutputDevice(m_engine->outputDevice());
    }

    auto onDuration = [this](bool vocals, qint64 duration) {
        if (!m_switchPending || duration <= 0)
            return;
        if (vocals)
            m_vocalsLoaded = true;
        else
            m_accompanimentLoaded = true;
        if (m_vocalsLoaded && m_accompanimentLoaded)
            finishStemSwitch();
    };
    connect(m_vocalsEngine, &PlayerEngine::durationChanged, this,
            [onDuration](qint64 duration) { onDuration(true, duration); });
    connect(m_accompanimentEngine, &PlayerEngine::durationChanged, this,
            [onDuration](qint64 duration) { onDuration(false, duration); });
    connect(m_vocalsEngine, &PlayerEngine::mediaError, this,
            [this](const QString &message) { handlePlaybackFailure(message); });
    connect(m_accompanimentEngine, &PlayerEngine::mediaError, this,
            [this](const QString &message) { handlePlaybackFailure(message); });
    connect(m_vocalsEngine, &PlayerEngine::playbackFinished, this, [this]() {
        if (m_stemActive && m_source == StemSource::Chunk)
            syncPositions();
    });
    connect(m_accompanimentEngine, &PlayerEngine::playbackFinished, this, [this]() {
        if (m_stemActive && m_source == StemSource::Chunk)
            syncPositions();
    });
}

void StemMixerDialog::loadCachedStems()
{
    const QDir dir(musicCacheDir());
    if (!dir.exists())
        return;

    if (cachedFileReady(fullVocalsPath()) && cachedFileReady(fullAccompanimentPath())) {
        m_fullReady = true;
        m_fullFailed = false;
    }

    const QStringList manifests =
        dir.entryList({QStringLiteral("chunk-*.json")}, QDir::Files, QDir::Name);
    for (const QString &name : manifests) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonValue indexValue = object.value(QStringLiteral("index"));
        if (!indexValue.isDouble())
            continue;
        const int index = indexValue.toInt(-1);
        if (index < 0)
            continue;
        if (!cachedFileReady(chunkVocalsPath(index))
            || !cachedFileReady(chunkAccompanimentPath(index))) {
            continue;
        }
        QVariantMap chunk = object.toVariantMap();
        chunk.insert(QStringLiteral("index"), index);
        chunk.insert(QStringLiteral("cached"), true);
        m_readyChunks.insert(index, chunk);
        m_totalChunks = qMax(m_totalChunks,
                             object.value(QStringLiteral("totalChunks")).toInt());
    }
    m_totalChunks = qMax(m_totalChunks, m_readyChunks.size());
    updateProgress();
}

void StemMixerDialog::startStemStream()
{
    if (!m_stemActive || !m_apiClient || m_streamReply || m_streamDone || m_fullReady
        || m_fullDownloading) {
        return;
    }
    setStatus(I18n::instance().tr(QStringLiteral("stemPreparing")));
    QPointer<StemMixerDialog> self(this);
    m_streamReply = m_apiClient->streamStemSeparation(
        m_music.id,
        [self](const QString &event, const QVariantMap &data) {
            if (self)
                self->onStemStreamEvent(event, data);
        },
        [self](int statusCode, const QString &message) {
            if (self)
                self->onStemStreamError(statusCode, message);
        });
    if (!m_streamReply)
        return;

    QNetworkReply *reply = m_streamReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_streamReply == reply)
            m_streamReply = nullptr;
    });
}

void StemMixerDialog::stopStemStream()
{
    if (!m_streamReply)
        return;
    QNetworkReply *reply = m_streamReply;
    m_streamReply = nullptr;
    reply->abort();
}

void StemMixerDialog::onStemStreamError(int statusCode, const QString &message)
{
    if (!m_stemActive)
        return;
    if (statusCode == 401) {
        m_stemToggle->setChecked(false);
        m_stemToggle->setEnabled(false);
        setStatus(I18n::instance().tr(QStringLiteral("stemLoginRequired")));
        return;
    }
    if (statusCode == 403) {
        m_stemToggle->setChecked(false);
        m_stemToggle->setEnabled(false);
        setStatus(I18n::instance().tr(QStringLiteral("stemVipRequired")));
        return;
    }

    const QString detail = message.trimmed();
    setStatus(detail.isEmpty()
                  ? I18n::instance().tr(QStringLiteral("stemServiceUnavailable"))
                  : I18n::instance().tr(QStringLiteral("stemServiceUnavailableDetail"))
                        .arg(detail));
}

void StemMixerDialog::onStemStreamEvent(const QString &event, const QVariantMap &data)
{
    if (!m_stemActive)
        return;

    if (event == QLatin1String("queued")) {
        const QString jobId = data.value(QStringLiteral("jobId")).toString();
        if (!jobId.isEmpty())
            m_jobId = jobId;
        if (m_readyChunks.isEmpty() && !m_fullReady) {
            const int queuePosition = qMax(0, data.value(QStringLiteral("queuePosition")).toInt());
            setStatus(I18n::instance().tr(QStringLiteral("stemQueued")).arg(queuePosition));
        }
        return;
    }

    if (event == QLatin1String("start") || event == QLatin1String("meta")) {
        m_totalChunks = qMax(m_totalChunks,
                             data.value(QStringLiteral("totalChunks")).toInt());
        updateProgress();
        if (!m_fullReady && m_readyChunks.isEmpty()) {
            setStatus(m_totalChunks > 0
                          ? I18n::instance().tr(QStringLiteral("stemProcessing")).arg(m_totalChunks)
                          : I18n::instance().tr(QStringLiteral("stemPreparing")));
        }
        return;
    }

    if (event == QLatin1String("chunk")) {
        bool indexOk = false;
        const int index = data.value(QStringLiteral("index")).toInt(&indexOk);
        if (!indexOk || index < 0)
            return;
        m_totalChunks = qMax(m_totalChunks,
                             data.value(QStringLiteral("totalChunks")).toInt());
        m_totalChunks = qMax(m_totalChunks, index + 1);
        const QString key = m_jobId + ':' + chunkPathsKey(index);
        if (m_seenChunkEvents.contains(key))
            return;
        m_seenChunkEvents.insert(key);
        if (m_readyChunks.contains(index) || cachedFileReady(chunkVocalsPath(index))) {
            // 缓存和 SSE 重放都可能先于本次连接到达；统一走 cacheChunk 校验。
        }
        if (!m_stemSwitched && !m_switchPending && !m_fullReady) {
            setStatus(I18n::instance().tr(QStringLiteral("stemChunkDownloading"))
                          .arg(index + 1)
                          .arg(qMax(m_totalChunks, index + 1)));
        }
        cacheChunk(index, data);
        return;
    }

    if (event == QLatin1String("done")) {
        m_streamDone = true;
        cacheFullTracks(data);
        return;
    }

    if (event == QLatin1String("error")) {
        m_streamDone = true;
        const QString detail = data.value(QStringLiteral("message")).toString().trimmed();
        setStatus(detail.isEmpty()
                      ? I18n::instance().tr(QStringLiteral("stemServiceUnavailable"))
                      : I18n::instance().tr(QStringLiteral("stemServiceUnavailableDetail"))
                            .arg(detail));
    }
}

void StemMixerDialog::cacheChunk(int index, const QVariantMap &data)
{
    if (index < 0 || m_chunkDownloads.contains(index))
        return;

    const QString vocalsPath = chunkVocalsPath(index);
    const QString accompanimentPath = chunkAccompanimentPath(index);
    if (cachedFileReady(vocalsPath) && cachedFileReady(accompanimentPath)) {
        QVariantMap chunk = data;
        chunk.insert(QStringLiteral("index"), index);
        chunk.insert(QStringLiteral("cached"), true);
        m_readyChunks.insert(index, chunk);
        saveChunkManifest(index, chunk);
        m_failedChunks.remove(index);
        updateProgress();
        if (m_stemActive)
            syncPositions();
        return;
    }

    const QString vocalsUrl = data.value(QStringLiteral("vocalsUrl")).toString();
    const QString accompanimentUrl =
        data.value(QStringLiteral("accompanimentUrl")).toString();
    if (vocalsUrl.isEmpty() || accompanimentUrl.isEmpty())
        return;

    m_chunkDownloads.insert(index);
    downloadPair(
        vocalsUrl, accompanimentUrl, vocalsPath, accompanimentPath,
        [this, index, data](bool ok, const QString &message) {
            m_chunkDownloads.remove(index);
            if (!ok) {
                m_failedChunks.insert(index);
                if (m_stemActive && !m_stemSwitched) {
                    const QString detail = message.trimmed();
                    setStatus(detail.isEmpty()
                                  ? I18n::instance().tr(QStringLiteral("stemPlaybackFallback"))
                                  : I18n::instance()
                                        .tr(QStringLiteral("stemPlaybackFallbackDetail"))
                                        .arg(detail));
                }
                return;
            }
            QVariantMap chunk = data;
            chunk.insert(QStringLiteral("index"), index);
            chunk.insert(QStringLiteral("cached"), true);
            m_readyChunks.insert(index, chunk);
            m_failedChunks.remove(index);
            saveChunkManifest(index, chunk);
            updateProgress();
            if (m_stemActive)
                syncPositions();
        });
}

void StemMixerDialog::cacheFullTracks(const QVariantMap &data)
{
    if (m_fullReady || m_fullDownloading)
        return;

    const QString vocalsUrl = data.value(QStringLiteral("vocalsUrl")).toString();
    const QString accompanimentUrl =
        data.value(QStringLiteral("accompanimentUrl")).toString();
    if (cachedFileReady(fullVocalsPath()) && cachedFileReady(fullAccompanimentPath())) {
        m_fullReady = true;
        m_fullFailed = false;
        updateProgress();
        if (m_stemActive)
            syncPositions();
        return;
    }
    if (vocalsUrl.isEmpty() || accompanimentUrl.isEmpty())
        return;

    m_fullDownloading = true;
    if (m_stemActive && !m_stemSwitched) {
        setStatus(I18n::instance().tr(QStringLiteral("stemFullDownloading")));
    }
    downloadPair(
        vocalsUrl, accompanimentUrl, fullVocalsPath(), fullAccompanimentPath(),
        [this](bool ok, const QString &message) {
            m_fullDownloading = false;
            if (!ok) {
                m_fullFailed = true;
                if (m_stemActive && !m_stemSwitched) {
                    const QString detail = message.trimmed();
                    setStatus(detail.isEmpty()
                                  ? I18n::instance().tr(QStringLiteral("stemPlaybackFallback"))
                                  : I18n::instance()
                                        .tr(QStringLiteral("stemPlaybackFallbackDetail"))
                                        .arg(detail));
                }
                return;
            }
            m_fullReady = true;
            m_fullFailed = false;
            updateProgress();
            if (m_stemActive)
                syncPositions();
        });
}

void StemMixerDialog::saveChunkManifest(int index, const QVariantMap &chunk) const
{
    QJsonObject object;
    object.insert(QStringLiteral("index"), index);
    object.insert(QStringLiteral("startSec"), chunk.value(QStringLiteral("startSec")).toDouble());
    object.insert(QStringLiteral("endSec"), chunk.value(QStringLiteral("endSec")).toDouble());
    object.insert(QStringLiteral("totalChunks"),
                  qMax(m_totalChunks, chunk.value(QStringLiteral("totalChunks")).toInt()));
    object.insert(QStringLiteral("vocalsUrl"),
                  chunk.value(QStringLiteral("vocalsUrl")).toString());
    object.insert(QStringLiteral("accompanimentUrl"),
                  chunk.value(QStringLiteral("accompanimentUrl")).toString());

    QSaveFile file(chunkManifestPath(index));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.commit();
}

void StemMixerDialog::updateProgress()
{
    if (!m_progressBar)
        return;
    m_progressBar->setVisible(m_stemActive);
    const int total = qMax(m_totalChunks, m_readyChunks.size());
    if (m_fullReady) {
        const int maximum = qMax(1, total);
        m_progressBar->setRange(0, maximum);
        m_progressBar->setValue(maximum);
    } else if (total > 0) {
        m_progressBar->setRange(0, total);
        m_progressBar->setValue(m_readyChunks.size());
    } else {
        m_progressBar->setRange(0, 0);
    }
}

void StemMixerDialog::updateStemControls()
{
    const bool canEdit = m_stemActive && (m_stemSwitched || m_switchPending
                                          || m_fullReady || !m_readyChunks.isEmpty());
    m_vocalsSlider->setEnabled(canEdit);
    m_accompanimentSlider->setEnabled(canEdit);
    m_resetMixButton->setEnabled(canEdit);
    m_stemToggle->setEnabled(UserManager::instance().isLoggedIn()
                             && UserManager::instance().isVip());
}

void StemMixerDialog::persistGains() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("stem/masterGain"), m_masterSlider->value());
    settings.setValue(QStringLiteral("stem/vocalsGain"), m_vocalsSlider->value());
    settings.setValue(QStringLiteral("stem/accompanimentGain"),
                      m_accompanimentSlider->value());
}

void StemMixerDialog::setStatus(const QString &text)
{
    if (m_statusLabel)
        m_statusLabel->setText(text);
}

void StemMixerDialog::setStemActive(bool active)
{
    if (active == m_stemActive) {
        if (active)
            syncPositions();
        return;
    }

    if (!active) {
        m_stemActive = false;
        m_stemSwitched = false;
        m_switchPending = false;
        m_source = StemSource::None;
        m_loadingSource = StemSource::None;
        m_activeChunkIndex = -1;
        m_loadingChunkIndex = -1;
        m_vocalsLoaded = false;
        m_accompanimentLoaded = false;
        if (m_syncTimer)
            m_syncTimer->stop();
        if (m_switchTimeout)
            m_switchTimeout->stop();
        stopStemStream();
        stopPlayback();
        if (m_engine)
            m_engine->setVolume(m_masterSlider->value() / qreal(kSliderMax));
        updateStemControls();
        updateProgress();
        setStatus(I18n::instance().tr(QStringLiteral("stemDisabled")));
        return;
    }

    if (!UserManager::instance().isLoggedIn()) {
        m_stemToggle->setEnabled(false);
        return;
    }
    if (!UserManager::instance().isVip()) {
        m_stemToggle->setEnabled(false);
        return;
    }

    m_stemActive = true;
    m_stemSwitched = false;
    m_switchPending = false;
    m_source = StemSource::None;
    m_loadingSource = StemSource::None;
    m_activeChunkIndex = -1;
    m_loadingChunkIndex = -1;
    m_failedChunks.clear();
    m_seenChunkEvents.clear();
    m_fullFailed = false;
    ensurePlayers();
    updateStemControls();
    updateProgress();
    m_syncTimer->start();
    const bool allChunksReady = m_totalChunks > 0
            && m_readyChunks.size() >= m_totalChunks;
    if (!m_fullReady && !m_fullDownloading && !allChunksReady) {
        m_streamDone = false;
        startStemStream();
    }
    syncPositions();
    if (!m_stemSwitched && !m_switchPending) {
        setStatus(m_fullReady || !m_readyChunks.isEmpty()
                      ? I18n::instance().tr(QStringLiteral("stemCacheRestored"))
                      : I18n::instance().tr(QStringLiteral("stemPreparing")));
    }
}

void StemMixerDialog::startChunkPlayback(int index, const QVariantMap &chunk)
{
    if (!m_stemActive || m_switchPending || !m_engine || m_failedChunks.contains(index))
        return;
    if (!cachedFileReady(chunkVocalsPath(index))
        || !cachedFileReady(chunkAccompanimentPath(index))) {
        return;
    }

    const qint64 position = m_engine->position();
    const qint64 startMs = secondsTextToMs(chunk.value(QStringLiteral("startSec")));
    const qint64 endMs = secondsTextToMs(chunk.value(QStringLiteral("endSec")));
    if (position < startMs || position >= endMs)
        return;

    if (m_stemSwitched || m_source != StemSource::None)
        stopStemPlayback(true);
    if (!m_stemActive)
        return;

    ensurePlayers();
    m_loadingSource = StemSource::Chunk;
    m_loadingChunkIndex = index;
    m_switchPending = true;
    m_vocalsLoaded = false;
    m_accompanimentLoaded = false;
    if (m_engine) {
        m_vocalsEngine->setOutputDevice(m_engine->outputDevice());
        m_accompanimentEngine->setOutputDevice(m_engine->outputDevice());
    }
    m_vocalsEngine->setVolume(0.0f);
    m_accompanimentEngine->setVolume(0.0f);
    const qint64 offset = qMax<qint64>(0, position - startMs);
    m_vocalsEngine->playResuming(QUrl::fromLocalFile(chunkVocalsPath(index)), offset);
    m_accompanimentEngine->playResuming(
        QUrl::fromLocalFile(chunkAccompanimentPath(index)), offset);
    syncTransport();
    m_switchTimeout->start();
    setStatus(I18n::instance().tr(QStringLiteral("stemChunkLoading"))
                  .arg(index + 1)
                  .arg(qMax(m_totalChunks, qMax(index + 1, m_readyChunks.size()))));
}

void StemMixerDialog::startFullPlayback(qint64 positionMs)
{
    if (!m_stemActive || m_switchPending || !m_engine || !m_fullReady || m_fullFailed)
        return;
    if (!cachedFileReady(fullVocalsPath()) || !cachedFileReady(fullAccompanimentPath()))
        return;
    if (m_source == StemSource::Full && m_stemSwitched)
        return;

    if (m_stemSwitched || m_source != StemSource::None)
        stopStemPlayback(true);
    if (!m_stemActive)
        return;

    ensurePlayers();
    m_loadingSource = StemSource::Full;
    m_loadingChunkIndex = -1;
    m_switchPending = true;
    m_vocalsLoaded = false;
    m_accompanimentLoaded = false;
    if (m_engine) {
        m_vocalsEngine->setOutputDevice(m_engine->outputDevice());
        m_accompanimentEngine->setOutputDevice(m_engine->outputDevice());
    }
    m_vocalsEngine->setVolume(0.0f);
    m_accompanimentEngine->setVolume(0.0f);
    m_vocalsEngine->playResuming(QUrl::fromLocalFile(fullVocalsPath()),
                                 qMax<qint64>(0, positionMs));
    m_accompanimentEngine->playResuming(QUrl::fromLocalFile(fullAccompanimentPath()),
                                        qMax<qint64>(0, positionMs));
    syncTransport();
    m_switchTimeout->start();
    setStatus(I18n::instance().tr(QStringLiteral("stemLoadingAudio")));
}

void StemMixerDialog::stopStemPlayback(bool restoreMain)
{
    if (m_switchTimeout)
        m_switchTimeout->stop();
    m_switchPending = false;
    m_stemSwitched = false;
    m_source = StemSource::None;
    m_loadingSource = StemSource::None;
    m_activeChunkIndex = -1;
    m_loadingChunkIndex = -1;
    m_vocalsLoaded = false;
    m_accompanimentLoaded = false;
    if (m_vocalsEngine)
        m_vocalsEngine->stop();
    if (m_accompanimentEngine)
        m_accompanimentEngine->stop();
    if (restoreMain && m_engine && m_masterSlider)
        m_engine->setVolume(m_masterSlider->value() / qreal(kSliderMax));
}

void StemMixerDialog::handlePlaybackFailure(const QString &message)
{
    if (!m_stemActive)
        return;
    const int failedChunk = m_loadingChunkIndex >= 0 ? m_loadingChunkIndex
                                                     : m_activeChunkIndex;
    if (m_loadingSource == StemSource::Chunk && failedChunk >= 0)
        m_failedChunks.insert(failedChunk);
    else if (m_loadingSource == StemSource::Full || m_source == StemSource::Full)
        m_fullFailed = true;

    stopStemPlayback(true);
    const QString detail = message.trimmed();
    setStatus(detail.isEmpty()
                  ? I18n::instance().tr(QStringLiteral("stemPlaybackFallback"))
                  : I18n::instance().tr(QStringLiteral("stemPlaybackFallbackDetail")).arg(detail));
    if (m_stemActive && !m_switchPending)
        syncPositions();
}

void StemMixerDialog::chooseSourceForPosition(qint64 positionMs)
{
    if (!m_stemActive || m_stemSwitched || m_switchPending)
        return;
    if (m_fullReady && !m_fullFailed) {
        startFullPlayback(positionMs);
        return;
    }

    QVariantMap chunk;
    const int index = chunkIndexAt(positionMs, &chunk);
    if (index >= 0 && !m_failedChunks.contains(index))
        startChunkPlayback(index, chunk);
}

void StemMixerDialog::finishStemSwitch()
{
    if (!m_stemActive || !m_switchPending || !m_vocalsLoaded || !m_accompanimentLoaded)
        return;
    m_switchTimeout->stop();
    m_switchPending = false;
    m_stemSwitched = true;
    m_source = m_loadingSource;
    m_activeChunkIndex = m_source == StemSource::Chunk ? m_loadingChunkIndex : -1;
    if (m_engine)
        m_engine->setVolume(0.0f);
    applyStemVolumes();
    syncTransport();
    updateStemControls();
    if (m_source == StemSource::Chunk) {
        setStatus(I18n::instance().tr(QStringLiteral("stemChunkActive"))
                      .arg(m_activeChunkIndex + 1)
                      .arg(qMax(m_totalChunks, m_readyChunks.size())));
    } else {
        setStatus(I18n::instance().tr(QStringLiteral("stemActive")));
    }
}

void StemMixerDialog::applyStemVolumes()
{
    if (!m_stemActive || !m_stemSwitched || !m_vocalsEngine
        || !m_accompanimentEngine) {
        return;
    }
    const qreal master = m_masterSlider->value() / qreal(kSliderMax);
    m_vocalsEngine->setVolume(master * m_vocalsSlider->value() / qreal(kSliderMax));
    m_accompanimentEngine->setVolume(
        master * m_accompanimentSlider->value() / qreal(kSliderMax));
}

void StemMixerDialog::syncTransport()
{
    if (!m_stemActive || !m_engine || !m_vocalsEngine || !m_accompanimentEngine)
        return;
    switch (m_engine->playbackState()) {
    case PlayerEngine::Playing:
        if (m_stemSwitched) {
            m_vocalsEngine->play();
            m_accompanimentEngine->play();
        }
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
    if (!m_stemActive || !m_engine || !m_vocalsEngine || !m_accompanimentEngine
        || m_switchPending) {
        return;
    }
    const qint64 clock = qMax<qint64>(0, m_engine->position());

    if (m_stemSwitched) {
        if (!currentSourceMatchesPosition(clock)) {
            stopStemPlayback(true);
        } else {
            const QVariantMap chunk = m_source == StemSource::Chunk
                    ? m_readyChunks.value(m_activeChunkIndex)
                    : QVariantMap();
            const qint64 offset = m_source == StemSource::Chunk
                    ? chunkOffsetMs(chunk, clock)
                    : clock;
            if (m_engine->playbackState() == PlayerEngine::Playing) {
                if (qAbs(m_vocalsEngine->position() - offset) > kSyncDriftMs)
                    m_vocalsEngine->setPosition(offset);
                if (qAbs(m_accompanimentEngine->position() - offset) > kSyncDriftMs)
                    m_accompanimentEngine->setPosition(offset);
            } else {
                m_vocalsEngine->setPosition(offset);
                m_accompanimentEngine->setPosition(offset);
            }
            syncTransport();
            return;
        }
    }

    chooseSourceForPosition(clock);
}

int StemMixerDialog::chunkIndexAt(qint64 positionMs, QVariantMap *chunk) const
{
    for (auto it = m_readyChunks.cbegin(); it != m_readyChunks.cend(); ++it) {
        const qint64 startMs = secondsTextToMs(it.value().value(QStringLiteral("startSec")));
        const qint64 endMs = secondsTextToMs(it.value().value(QStringLiteral("endSec")));
        if (endMs <= startMs)
            continue;
        if (positionMs >= startMs && positionMs < endMs) {
            if (chunk)
                *chunk = it.value();
            return it.key();
        }
    }
    return -1;
}

qint64 StemMixerDialog::chunkOffsetMs(const QVariantMap &chunk, qint64 positionMs) const
{
    return qMax<qint64>(0, positionMs - secondsTextToMs(chunk.value(QStringLiteral("startSec"))));
}

bool StemMixerDialog::currentSourceMatchesPosition(qint64 positionMs) const
{
    if (m_source == StemSource::Full)
        return m_fullReady && cachedFileReady(fullVocalsPath())
                && cachedFileReady(fullAccompanimentPath());
    if (m_source != StemSource::Chunk || m_activeChunkIndex < 0)
        return false;
    const QVariantMap chunk = m_readyChunks.value(m_activeChunkIndex);
    if (chunk.isEmpty())
        return false;
    const qint64 startMs = secondsTextToMs(chunk.value(QStringLiteral("startSec")));
    const qint64 endMs = secondsTextToMs(chunk.value(QStringLiteral("endSec")));
    return positionMs >= startMs && positionMs < endMs;
}

QString StemMixerDialog::musicCacheDir() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return base + QStringLiteral("/stem-cache/") + QString::number(m_music.id);
}

QString StemMixerDialog::chunkVocalsPath(int index) const
{
    return musicCacheDir()
            + QStringLiteral("/vocals-chunk-%1.mp3").arg(index, 3, 10, QChar('0'));
}

QString StemMixerDialog::chunkAccompanimentPath(int index) const
{
    return musicCacheDir()
            + QStringLiteral("/accompaniment-chunk-%1.mp3").arg(index, 3, 10, QChar('0'));
}

QString StemMixerDialog::chunkManifestPath(int index) const
{
    return musicCacheDir()
            + QStringLiteral("/chunk-%1.json").arg(index, 3, 10, QChar('0'));
}

QString StemMixerDialog::fullVocalsPath() const
{
    return musicCacheDir() + QStringLiteral("/full-vocals.mp3");
}

QString StemMixerDialog::fullAccompanimentPath() const
{
    return musicCacheDir() + QStringLiteral("/full-accompaniment.mp3");
}

void StemMixerDialog::downloadFile(const QString &url, const QString &destination,
                                   DownloadFileCb callback)
{
    if (cachedFileReady(destination)) {
        callback(true, {});
        return;
    }

    QDir().mkpath(QFileInfo(destination).absolutePath());
    const QString part = destination + QStringLiteral(".part");
    QFile::remove(part);
    QNetworkReply *reply = m_cacheNam->get(QNetworkRequest(QUrl(absoluteMediaUrl(url))));
    auto *file = new QFile(part, reply);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString message = file->errorString();
        reply->deleteLater();
        callback(false, message);
        return;
    }

    connect(reply, &QNetworkReply::readyRead, this, [reply, file]() {
        const QByteArray data = reply->readAll();
        if (!data.isEmpty())
            file->write(data);
    });
    connect(reply, &QNetworkReply::finished, this,
            [reply, file, part, destination, callback]() {
                const QByteArray tail = reply->readAll();
                if (!tail.isEmpty())
                    file->write(tail);
                file->flush();
                const qint64 bytes = file->size();
                const bool fileOk = file->error() == QFileDevice::NoError;
                file->close();

                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QString message = reply->errorString();
                const QByteArray body = reply->readAll();
                const QString serverMessage =
                    QJsonDocument::fromJson(body)
                        .object()
                        .value(QStringLiteral("message"))
                        .toString();
                if (!serverMessage.isEmpty())
                    message = serverMessage;
                const bool ok = reply->error() == QNetworkReply::NoError
                        && (status == 0 || status == 200) && bytes > 0 && fileOk;
                reply->deleteLater();
                if (!ok) {
                    QFile::remove(part);
                    callback(false, message);
                    return;
                }
                QFile::remove(destination);
                if (!QFile::rename(part, destination)) {
                    QFile::remove(part);
                    callback(false, QStringLiteral("分离音频缓存保存失败"));
                    return;
                }
                callback(true, {});
            });
}

void StemMixerDialog::downloadPair(const QString &vocalsUrl,
                                   const QString &accompanimentUrl,
                                   const QString &vocalsPath,
                                   const QString &accompanimentPath,
                                   DownloadPairCb callback)
{
    auto completed = std::make_shared<int>(0);
    auto firstError = std::make_shared<QString>();
    auto finish = [completed, firstError, callback](bool ok, const QString &message) {
        if (!ok && firstError->isEmpty())
            *firstError = message;
        ++*completed;
        if (*completed == 2)
            callback(firstError->isEmpty(), *firstError);
    };
    downloadFile(vocalsUrl, vocalsPath, finish);
    downloadFile(accompanimentUrl, accompanimentPath, finish);
}

void StemMixerDialog::stopPlayback()
{
    if (m_syncTimer)
        m_syncTimer->stop();
    if (m_switchTimeout)
        m_switchTimeout->stop();
    if (m_vocalsEngine)
        m_vocalsEngine->stop();
    if (m_accompanimentEngine)
        m_accompanimentEngine->stop();
}
