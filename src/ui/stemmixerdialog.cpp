#include "stemmixerdialog.h"

#include "core/apiclient.h"
#include "core/i18n.h"
#include "core/nekonetworkaccessmanager.h"
#include "core/playerengine.h"
#include "core/stemaudiomixer.h"
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
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>
#include <utility>

namespace {

constexpr int kSliderMax = 100;
constexpr int kDefaultGain = 80;
constexpr qint64 kCacheTtlMs = 180LL * 24 * 60 * 60 * 1000;
const auto kEnabledSetting = QStringLiteral("stem/enabled");

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

QString chunkPathsKey(int index)
{
    return QString::number(index);
}

} // namespace

bool StemMixerDialog::enabledPreference()
{
    return QSettings().value(kEnabledSetting, false).toBool();
}

StemMixerDialog::StemMixerDialog(ApiClient *apiClient, PlayerEngine *engine, QWidget *parent)
    : QDialog(parent)
    , m_apiClient(apiClient)
    , m_engine(engine)
    , m_stemRequested(enabledPreference())
{
    setWindowTitle(I18n::instance().tr(QStringLiteral("volumeAndStem")));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setModal(false);
    setWindowModality(Qt::NonModal);
    setFixedSize(520, 452);
    m_cacheNam = new NekoNetworkAccessManager(this);
    m_gainSaveTimer = new QTimer(this);
    m_gainSaveTimer->setSingleShot(true);
    m_gainSaveTimer->setInterval(250);
    connect(m_gainSaveTimer, &QTimer::timeout, this, &StemMixerDialog::persistGains);
    connect(this, &QDialog::finished, this, [this]() { persistGains(); });
    setupUi();
    auto &user = UserManager::instance();
    connect(&user, &UserManager::loginStateChanged, this, &StemMixerDialog::refreshStemAccess);
    connect(&user, &UserManager::vipStatusChanged, this, &StemMixerDialog::refreshStemAccess);
    if (m_engine)
        connect(m_engine, &PlayerEngine::currentMusicChanged, this, &StemMixerDialog::setTrack);
    setTrack(m_engine ? m_engine->currentMusic() : MusicInfo{});
}

StemMixerDialog::~StemMixerDialog()
{
    persistGains();
    m_stemActive = false;
    cancelStemRequests();
    stopPlayback();
    if (m_audioMixer)
        m_audioMixer->clearChunks();
}

void StemMixerDialog::setTrack(const MusicInfo &music)
{
    const bool changed = m_music.id != music.id || m_music.localPath != music.localPath
        || m_music.playable != music.playable;
    if (changed) {
        // 取消的是旧曲目的会话，不改变用户开启分离的偏好。
        setStemActive(false);
        if (m_audioMixer)
            m_audioMixer->clearChunks();
        m_readyChunks.clear();
        m_seenChunkEvents.clear();
        m_fullStemData.clear();
        m_jobId.clear();
        m_totalChunks = 0;
        m_streamDone = false;
        m_fullDecodePending = false;
        m_fullReady = false;
        m_accessFailure = 0;
    }

    m_music = music;
    const auto title = I18n::instance().tr(QStringLiteral("volumeAndStem"));
    m_titleLabel->setText(m_music.title.isEmpty() ? title
                          : QStringLiteral("%1 · %2").arg(title, m_music.title));
    if (changed && trackSupportsStems())
        loadCachedStems();
    syncStemState();
}

bool StemMixerDialog::trackSupportsStems() const
{
    return m_music.id > 0 && !m_music.isLocalFile() && m_music.playable;
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
    m_masterSlider->setObjectName(QStringLiteral("stemMasterSlider"));
    m_masterSlider->setRange(0, kSliderMax);
    QSettings savedGains;
    const int initialMaster = qRound((m_engine ? m_engine->volume() : 0.8f) * kSliderMax);
    m_masterSlider->setValue(initialMaster);
    m_masterValue = new QLabel(percentText(initialMaster), volumeRow);
    m_masterValue->setMinimumWidth(42);
    m_masterValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    volumeLay->addWidget(volumeName);
    volumeLay->addWidget(m_masterSlider, 1);
    volumeLay->addWidget(m_masterValue);
    root->addWidget(volumeRow);
    connect(m_masterSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_engine)
            m_engine->setVolume(value / float(kSliderMax));
        else
            m_masterValue->setText(percentText(value));
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
    m_vocalsSlider->setObjectName(QStringLiteral("stemVocalsSlider"));
    m_accompanimentSlider->setObjectName(QStringLiteral("stemAccompanimentSlider"));
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

    const auto bindStemGain = [this](QSlider *slider, QLabel *label) {
        connect(slider, &QSlider::sliderPressed, m_gainSaveTimer, &QTimer::stop);
        connect(slider, &QSlider::sliderReleased, this, [this]() {
            if (m_gainsDirty)
                m_gainSaveTimer->start();
        });
        connect(slider, &QSlider::valueChanged, this, [this, slider, label](int value) {
            label->setText(percentText(value));
            applyStemVolumes();
            m_gainsDirty = true;
            // 拖动时只改混音目标；键盘、滚轮和重置操作也合并保存。
            if (!slider->isSliderDown())
                m_gainSaveTimer->start();
        });
    };
    bindStemGain(m_vocalsSlider, m_vocalsValue);
    bindStemGain(m_accompanimentSlider, m_accompanimentValue);
    connect(m_stemToggle, &QCheckBox::toggled, this, [this](bool checked) {
        m_stemRequested = checked;
        QSettings().setValue(kEnabledSetting, checked);
        m_accessFailure = 0;
        syncStemState();
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

    if (m_engine) {
        connect(m_engine, &PlayerEngine::volumeChanged, this, [this](float volume) {
            const int value = qRound(volume * kSliderMax);
            const QSignalBlocker blocker(m_masterSlider);
            m_masterSlider->setValue(value);
            m_masterValue->setText(percentText(value));
            applyStemVolumes();
        });
        connect(m_engine, &PlayerEngine::playbackFinished, this, [this]() {
            if (m_stemActive)
                setStatus(I18n::instance().tr(QStringLiteral("stemTrackFinished")));
        });
    }
}

void StemMixerDialog::ensureMixer()
{
    if (m_audioMixer)
        return;

    m_audioMixer = new StemAudioMixer(this);
    if (m_engine) {
        m_audioMixer->setOutputDevice(m_engine->outputDevice());
        connect(m_engine, &PlayerEngine::decodedAudioBuffer, m_audioMixer,
                [this](const QAudioBuffer &buffer) {
                    if (m_engine->currentMusic().id == m_music.id
                        && m_engine->isActuallyPlaying())
                        m_audioMixer->processOriginalBuffer(buffer, m_engine->position());
                });
        connect(m_audioMixer, &StemAudioMixer::mixOutputChanged,
                m_engine, &PlayerEngine::setOriginalAudioMuted);
        connect(m_engine, &PlayerEngine::outputDeviceChanged,
                m_audioMixer, &StemAudioMixer::setOutputDevice);
        connect(m_engine, &PlayerEngine::seekStarted, m_audioMixer, &StemAudioMixer::resetOutput);
        connect(m_engine, &PlayerEngine::mediaPlaybackStateChanged, m_audioMixer, [this]() {
            if (!m_engine->isActuallyPlaying())
                m_audioMixer->resetOutput();
        });
    }
    connect(m_audioMixer, &StemAudioMixer::playbackActive, this, [this](int index) {
        if (!m_stemActive)
            return;
        if (index < 0) {
            setStatus(I18n::instance().tr(QStringLiteral("stemActive")));
            return;
        }
        setStatus(I18n::instance().tr(QStringLiteral("stemChunkActive"))
                      .arg(index + 1)
                      .arg(qMax(m_totalChunks, m_readyChunks.size())));
    });
    connect(m_audioMixer, &StemAudioMixer::chunkDecoding, this, [this](int index) {
        if (!m_stemActive)
            return;
        setStatus(I18n::instance().tr(QStringLiteral("stemChunkLoading"))
                      .arg(index + 1)
                      .arg(qMax(m_totalChunks, m_readyChunks.size())));
    });
    connect(m_audioMixer, &StemAudioMixer::playbackFallback, this, [this]() {
        if (m_stemActive && !m_mixerOutputFailed)
            setStatus(I18n::instance().tr(QStringLiteral("stemWaitingChunk")));
    });
    connect(m_audioMixer, &StemAudioMixer::outputUnavailable, this,
            &StemMixerDialog::onMixerOutputUnavailable);
    connect(m_audioMixer, &StemAudioMixer::chunkDecodeFailed, this,
            &StemMixerDialog::onMixerChunkFailure);
    connect(m_audioMixer, &StemAudioMixer::fullTracksReady, this, [this]() {
        m_fullDecodePending = false;
        m_fullReady = true;
        updateProgress();
        if (m_stemActive && !m_mixerOutputFailed)
            setStatus(I18n::instance().tr(QStringLiteral("stemReady")));
    });
    connect(m_audioMixer, &StemAudioMixer::fullTracksFailed, this, [this](const QString &message) {
        m_fullDecodePending = false;
        QFile::remove(fullTrackPath(true));
        QFile::remove(fullTrackPath(false));
        onMixerChunkFailure(-1, message);
    });
    for (auto it = m_readyChunks.cbegin(); it != m_readyChunks.cend(); ++it)
        registerReadyChunk(it.key(), it.value());
}

void StemMixerDialog::registerReadyChunk(int index, const QVariantMap &chunk)
{
    if (m_audioMixer) {
        m_audioMixer->addChunk(
            index,
            chunk.value(QStringLiteral("startSec")).toDouble(),
            chunk.value(QStringLiteral("endSec")).toDouble(),
            chunkVocalsPath(index),
            chunkAccompanimentPath(index));
    }
}

void StemMixerDialog::loadCachedStems()
{
    const QDir dir(musicCacheDir());
    if (!dir.exists())
        return;

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
        if (object.value(QStringLiteral("endSec")).toDouble() <= object.value(QStringLiteral("startSec")).toDouble()
            || !cachedFileReady(chunkVocalsPath(index))
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
    if (!m_stemActive || !m_apiClient || m_streamReply || m_streamDone) {
        return;
    }
    setStatus(I18n::instance().tr(QStringLiteral("stemPreparing")));
    QPointer<StemMixerDialog> self(this);
    const quint64 generation = m_requestGeneration;
    m_streamReply = m_apiClient->streamStemSeparation(
        m_music.id,
        [self, generation](const QString &event, const QVariantMap &data) {
            if (self && self->m_requestGeneration == generation)
                self->onStemStreamEvent(event, data);
        },
        [self, generation](int statusCode, const QString &message) {
            if (self && self->m_requestGeneration == generation)
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

void StemMixerDialog::cancelStemRequests()
{
    ++m_requestGeneration;
    stopStemStream();
    const auto replies = std::exchange(m_downloadReplies, {});
    m_chunkDownloads.clear();
    m_fullDownloadPending = false;
    for (auto *reply : replies) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

void StemMixerDialog::onStemStreamError(int statusCode, const QString &message)
{
    if (!m_stemActive)
        return;
    if (statusCode == 401 || statusCode == 403) {
        m_accessFailure = statusCode;
        syncStemState();
        return;
    }

    const QString detail = message.trimmed();
    setStatus(detail.isEmpty()
                  ? I18n::instance().tr(QStringLiteral("stemServiceUnavailable"))
                  : I18n::instance().tr(QStringLiteral("stemServiceUnavailableDetail"))
                        .arg(detail));
}

void StemMixerDialog::onMixerOutputUnavailable(const QString &message)
{
    if (!m_stemActive)
        return;
    m_mixerOutputFailed = true;
    if (m_audioMixer)
        m_audioMixer->setEnabled(false);
    const QString detail = message.trimmed();
    setStatus(detail.isEmpty()
                  ? I18n::instance().tr(QStringLiteral("stemPlaybackFallback"))
                  : I18n::instance().tr(QStringLiteral("stemPlaybackFallbackDetail"))
                        .arg(detail));
}

void StemMixerDialog::onMixerChunkFailure(int index, const QString &message)
{
    if (!m_stemActive)
        return;
    const QString detail = message.trimmed();
    setStatus(detail.isEmpty()
                  ? I18n::instance().tr(QStringLiteral("stemPlaybackFallback"))
                  : I18n::instance().tr(QStringLiteral("stemPlaybackFallbackDetail"))
                        .arg(detail));
    if (index >= 0) {
        m_readyChunks.remove(index);
        QFile::remove(chunkManifestPath(index));
        QFile::remove(chunkVocalsPath(index));
        QFile::remove(chunkAccompanimentPath(index));
        if (m_streamDone) {
            finishStemPreparation();
        } else if (cachedFileReady(fullTrackPath(true)) && cachedFileReady(fullTrackPath(false))) {
            prepareFullTracks();
        } else if (!m_streamReply) {
            startStemStream();
        }
    }
}

void StemMixerDialog::onStemStreamEvent(const QString &event, const QVariantMap &data)
{
    if (!m_stemActive)
        return;

    if (event == QLatin1String("queued")) {
        const QString jobId = data.value(QStringLiteral("jobId")).toString();
        if (!jobId.isEmpty())
            m_jobId = jobId;
        if (m_readyChunks.isEmpty()) {
            const int queuePosition = qMax(0, data.value(QStringLiteral("queuePosition")).toInt());
            setStatus(I18n::instance().tr(QStringLiteral("stemQueued")).arg(queuePosition));
        }
        return;
    }

    if (event == QLatin1String("start") || event == QLatin1String("meta")) {
        m_totalChunks = qMax(m_totalChunks,
                             data.value(QStringLiteral("totalChunks")).toInt());
        updateProgress();
        if (m_readyChunks.isEmpty()) {
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
        if (m_stemActive && !m_mixerOutputFailed) {
            setStatus(I18n::instance().tr(QStringLiteral("stemChunkDownloading"))
                          .arg(index + 1)
                          .arg(qMax(m_totalChunks, index + 1)));
        }
        cacheChunk(index, data);
        return;
    }

    if (event == QLatin1String("done")) {
        m_streamDone = true;
        m_fullStemData = data;
        finishStemPreparation();
        return;
    }

    if (event == QLatin1String("error")) {
        const QString detail = data.value(QStringLiteral("message")).toString().trimmed();
        setStatus(detail.isEmpty()
                      ? I18n::instance().tr(QStringLiteral("stemServiceUnavailable"))
                      : I18n::instance().tr(QStringLiteral("stemServiceUnavailableDetail"))
                            .arg(detail));
    }
}

bool StemMixerDialog::allChunksReady() const
{
    if (m_totalChunks <= 0 || m_readyChunks.size() < m_totalChunks)
        return false;
    for (int index = 0; index < m_totalChunks; ++index) {
        if (!m_readyChunks.contains(index))
            return false;
    }
    return true;
}

void StemMixerDialog::finishStemPreparation()
{
    if (!m_stemActive || !m_streamDone || !m_chunkDownloads.isEmpty())
        return;
    updateProgress();
    if (allChunksReady()) {
        if (!m_mixerOutputFailed)
            setStatus(I18n::instance().tr(QStringLiteral("stemReady")));
    } else {
        prepareFullTracks();
    }
}

void StemMixerDialog::prepareFullTracks()
{
    if (m_fullReady || m_fullDownloadPending || m_fullDecodePending)
        return;
    const QString vocalsPath = fullTrackPath(true);
    const QString accompanimentPath = fullTrackPath(false);
    const auto decode = [this, vocalsPath, accompanimentPath]() {
        m_fullDecodePending = true;
        m_audioMixer->loadFullTracks(vocalsPath, accompanimentPath);
    };
    if (cachedFileReady(vocalsPath) && cachedFileReady(accompanimentPath)) {
        decode();
        return;
    }

    const QString vocalsUrl = m_fullStemData.value(QStringLiteral("vocalsUrl")).toString();
    const QString accompanimentUrl = m_fullStemData.value(QStringLiteral("accompanimentUrl")).toString();
    if (vocalsUrl.isEmpty() || accompanimentUrl.isEmpty()) {
        onMixerChunkFailure(-1, QStringLiteral("服务端未返回完整的分离音轨"));
        return;
    }
    m_fullDownloadPending = true;
    if (!m_mixerOutputFailed)
        setStatus(I18n::instance().tr(QStringLiteral("stemPreparing")));
    downloadPair(vocalsUrl, accompanimentUrl, vocalsPath, accompanimentPath,
                 [this, decode](bool ok, const QString &message) {
                     m_fullDownloadPending = false;
                     if (ok && m_stemActive)
                         decode();
                     else if (!ok)
                         onMixerChunkFailure(-1, message);
                 });
}

void StemMixerDialog::cacheChunk(int index, const QVariantMap &data)
{
    if (index < 0 || m_chunkDownloads.contains(index))
        return;
    const double start = data.value(QStringLiteral("startSec")).toDouble();
    const double end = data.value(QStringLiteral("endSec")).toDouble();
    if (start < 0 || end <= start)
        return;

    const QString vocalsPath = chunkVocalsPath(index);
    const QString accompanimentPath = chunkAccompanimentPath(index);
    if (cachedFileReady(vocalsPath) && cachedFileReady(accompanimentPath)) {
        QVariantMap chunk = data;
        chunk.insert(QStringLiteral("index"), index);
        chunk.insert(QStringLiteral("cached"), true);
        m_readyChunks.insert(index, chunk);
        saveChunkManifest(index, chunk);
        registerReadyChunk(index, chunk);
        updateProgress();
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
                if (m_stemActive) {
                    const QString detail = message.trimmed();
                    setStatus(detail.isEmpty()
                                  ? I18n::instance().tr(QStringLiteral("stemPlaybackFallback"))
                                  : I18n::instance()
                                        .tr(QStringLiteral("stemPlaybackFallbackDetail"))
                                        .arg(detail));
                }
                finishStemPreparation();
                return;
            }
            QVariantMap chunk = data;
            chunk.insert(QStringLiteral("index"), index);
            chunk.insert(QStringLiteral("cached"), true);
            m_readyChunks.insert(index, chunk);
            saveChunkManifest(index, chunk);
            registerReadyChunk(index, chunk);
            updateProgress();
            finishStemPreparation();
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
    if (m_fullReady) {
        m_progressBar->setRange(0, 1);
        m_progressBar->setValue(1);
        return;
    }
    const int total = qMax(m_totalChunks, m_readyChunks.size());
    if (total > 0) {
        m_progressBar->setRange(0, total);
        m_progressBar->setValue(qMin(total, int(m_readyChunks.size())));
    } else {
        m_progressBar->setRange(0, 0);
    }
}

void StemMixerDialog::updateStemControls()
{
    m_vocalsSlider->setEnabled(m_stemActive);
    m_accompanimentSlider->setEnabled(m_stemActive);
    m_resetMixButton->setEnabled(m_stemActive);
    const QSignalBlocker blocker(m_stemToggle);
    m_stemToggle->setChecked(m_stemRequested);
    m_stemToggle->setEnabled(trackSupportsStems() && UserManager::instance().isLoggedIn()
                             && UserManager::instance().isVip());
}

void StemMixerDialog::refreshStemAccess()
{
    m_accessFailure = 0;
    syncStemState();
}

void StemMixerDialog::syncStemState()
{
    const auto &user = UserManager::instance();
    const bool loggedIn = user.isLoggedIn() && m_accessFailure != 401;
    const bool vip = user.isVip() && m_accessFailure != 403;
    const bool supported = trackSupportsStems();
    setStemActive(m_stemRequested && supported && loggedIn && vip);
    updateStemControls();
    updateProgress();
    if (!supported)
        setStatus(I18n::instance().tr(QStringLiteral("stemOnlineTrackRequired")));
    else if (!loggedIn)
        setStatus(I18n::instance().tr(QStringLiteral("stemLoginRequired")));
    else if (!vip)
        setStatus(I18n::instance().tr(QStringLiteral("stemVipRequired")));
    else if (!m_stemRequested)
        setStatus(I18n::instance().tr(QStringLiteral("stemDisabled")));
}

void StemMixerDialog::persistGains()
{
    if (!m_gainsDirty)
        return;
    m_gainSaveTimer->stop();
    m_gainsDirty = false;
    QSettings settings;
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
        return;
    }

    if (!active) {
        m_stemActive = false;
        m_mixerOutputFailed = false;
        cancelStemRequests();
        if (m_audioMixer)
            m_audioMixer->setEnabled(false);
        return;
    }

    m_stemActive = true;
    m_mixerOutputFailed = false;
    m_seenChunkEvents.clear();
    ensureMixer();
    if (m_audioMixer && m_engine)
        m_audioMixer->setOutputDevice(m_engine->outputDevice());
    applyStemVolumes();
    if (m_audioMixer)
        m_audioMixer->setEnabled(true);
    if (m_fullReady || allChunksReady()) {
        setStatus(I18n::instance().tr(QStringLiteral("stemCacheRestored")));
    } else if (m_fullDownloadPending || m_fullDecodePending
               || (cachedFileReady(fullTrackPath(true)) && cachedFileReady(fullTrackPath(false)))) {
        setStatus(I18n::instance().tr(QStringLiteral("stemPreparing")));
        prepareFullTracks();
    } else {
        m_streamDone = false;
        m_fullStemData.clear();
        startStemStream();
    }
}

void StemMixerDialog::applyStemVolumes()
{
    if (!m_stemActive || !m_audioMixer || m_mixerOutputFailed)
        return;
    const qreal master = m_engine ? m_engine->volume()
                                 : m_masterSlider->value() / qreal(kSliderMax);
    m_audioMixer->setGains(master,
                           m_vocalsSlider->value() / qreal(kSliderMax),
                           m_accompanimentSlider->value() / qreal(kSliderMax));
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

QString StemMixerDialog::fullTrackPath(bool vocals) const
{
    return musicCacheDir() + (vocals ? QStringLiteral("/full-vocals.mp3")
                                   : QStringLiteral("/full-accompaniment.mp3"));
}

void StemMixerDialog::downloadFile(const QString &url, const QString &destination,
                                   DownloadFileCb callback)
{
    if (cachedFileReady(destination)) {
        callback(true, {});
        return;
    }

    QDir().mkpath(QFileInfo(destination).absolutePath());
    auto file = std::make_shared<QSaveFile>(destination);
    if (!file->open(QIODevice::WriteOnly)) {
        callback(false, file->errorString());
        return;
    }
    QNetworkRequest request{QUrl(absoluteMediaUrl(url))};
    request.setTransferTimeout(120000);
    QNetworkReply *reply = m_cacheNam->get(request);
    m_downloadReplies.insert(reply);

    auto bytes = std::make_shared<qint64>(0);
    const auto writeData = [reply, file, bytes]() {
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return;
        const auto data = reply->readAll();
        if (!data.isEmpty() && file->write(data) == data.size())
            *bytes += data.size();
    };
    connect(reply, &QNetworkReply::readyRead, this, writeData);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, bytes, writeData, callback]() {
                m_downloadReplies.remove(reply);
                writeData();
                const int status =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const QVariant expected = reply->header(QNetworkRequest::ContentLengthHeader);
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError || status != 200) {
                    const auto message = QJsonDocument::fromJson(reply->readAll()).object()
                                             .value(QStringLiteral("message")).toString();
                    callback(false, message.isEmpty() ? reply->errorString() : message);
                    return;
                }
                if (*bytes <= 0 || (expected.isValid() && *bytes != expected.toLongLong())) {
                    callback(false, QStringLiteral("分离音轨下载为空或不完整"));
                    return;
                }
                if (!file->commit()) {
                    callback(false, file->errorString());
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
    auto success = std::make_shared<bool>(true);
    auto firstError = std::make_shared<QString>();
    auto finish = [completed, success, firstError, callback](bool ok, const QString &message) {
        if (!ok && *success)
            *firstError = message;
        *success &= ok;
        ++*completed;
        if (*completed == 2)
            callback(*success, *firstError);
    };
    downloadFile(vocalsUrl, vocalsPath, finish);
    downloadFile(accompanimentUrl, accompanimentPath, finish);
}

void StemMixerDialog::stopPlayback()
{
    if (m_audioMixer)
        m_audioMixer->setEnabled(false);
}
