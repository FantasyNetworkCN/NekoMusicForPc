/**
 * @file musiclistpage.cpp
 * @brief 热门 / 最新音乐列表页实现
 */

#include "musiclistpage.h"
#include "songlistwidget.h"
#include "core/apiclient.h"
#include "core/i18n.h"
#include "core/musicdownloadmanager.h"
#include "ui/songcontextmenu.h"
#include "ui/songmenubuilder.h"
#include "core/playlistmanager.h"
#include "core/usermanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QTimer>
#include <QShowEvent>
#include <QDateTime>
#include <QVariantList>
#include <QRegularExpression>
#include <QSharedPointer>

namespace {

int parseDurationTextToSeconds(const QString &raw);

QVariant firstNonNull(const QVariantMap &a, const QVariantMap &b, const QString &key)
{
    const QVariant va = a.value(key);
    if (va.isValid() && !va.isNull() && !va.toString().isEmpty())
        return va;
    return b.value(key);
}

QVariantMap unwrapMusicNode(const QVariantMap &item)
{
    const QStringList keys = {QStringLiteral("music"), QStringLiteral("song"),
                              QStringLiteral("track"), QStringLiteral("data")};
    for (const QString &k : keys) {
        const QVariant v = item.value(k);
        if (v.canConvert<QVariantMap>()) {
            const QVariantMap m = v.toMap();
            if (!m.isEmpty())
                return m;
        }
    }
    return {};
}

QVariant deepFindByKeys(const QVariant &node, const QStringList &keys, int depth = 0)
{
    if (depth > 6 || !node.isValid() || node.isNull())
        return {};
    if (node.canConvert<QVariantMap>()) {
        const QVariantMap map = node.toMap();
        for (const QString &k : keys) {
            const QVariant v = map.value(k);
            if (v.isValid() && !v.isNull() && !v.toString().isEmpty())
                return v;
        }
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const QVariant hit = deepFindByKeys(it.value(), keys, depth + 1);
            if (hit.isValid() && !hit.isNull() && !hit.toString().isEmpty())
                return hit;
        }
    }
    if (node.canConvert<QVariantList>()) {
        const QVariantList list = node.toList();
        for (const QVariant &v : list) {
            const QVariant hit = deepFindByKeys(v, keys, depth + 1);
            if (hit.isValid() && !hit.isNull() && !hit.toString().isEmpty())
                return hit;
        }
    }
    return {};
}

int deepFindDurationSeconds(const QVariant &node, int depth = 0)
{
    if (depth > 8 || !node.isValid() || node.isNull())
        return 0;

    auto parseCandidate = [](const QVariant &v) -> int {
        if (!v.isValid() || v.isNull())
            return 0;
        bool ok = false;
        qint64 n = v.toLongLong(&ok);
        if (ok && n > 0) {
            if (n > 10000)
                n /= 1000;
            return int(n);
        }
        return 0;
    };

    if (node.canConvert<QVariantMap>()) {
        const QVariantMap map = node.toMap();
        static const QStringList durationKeys = {
            QStringLiteral("duration"), QStringLiteral("durationSec"), QStringLiteral("durationMs"),
            QStringLiteral("dt"), QStringLiteral("length"), QStringLiteral("timeLength"),
            QStringLiteral("songTime"), QStringLiteral("lengthMs"), QStringLiteral("durationText"),
            QStringLiteral("durationStr"), QStringLiteral("dtText"), QStringLiteral("duration_time")};

        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const QString key = it.key().toLower();
            if (durationKeys.contains(it.key()) || key.contains(QStringLiteral("duration"))
                || key == QStringLiteral("dt") || key.contains(QStringLiteral("length"))) {
                int v = parseCandidate(it.value());
                if (v <= 0)
                    v = parseDurationTextToSeconds(it.value().toString());
                if (v > 0 && v >= 10 && v <= 30 * 60)
                    return v;
            }
        }
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            const int sub = deepFindDurationSeconds(it.value(), depth + 1);
            if (sub > 0)
                return sub;
        }
    } else if (node.canConvert<QVariantList>()) {
        const QVariantList list = node.toList();
        for (const QVariant &v : list) {
            const int sub = deepFindDurationSeconds(v, depth + 1);
            if (sub > 0)
                return sub;
        }
    } else {
        const int v = parseDurationTextToSeconds(node.toString());
        if (v > 0)
            return v;
    }
    return 0;
}

int parseDurationTextToSeconds(const QString &raw)
{
    const QString s = raw.trimmed();
    if (s.isEmpty())
        return 0;
    if (s.contains(QLatin1Char(':'))) {
        const QStringList parts = s.split(QLatin1Char(':'), Qt::SkipEmptyParts);
        if (parts.size() == 2) {
            return parts[0].toInt() * 60 + parts[1].toInt();
        }
        if (parts.size() == 3) {
            return parts[0].toInt() * 3600 + parts[1].toInt() * 60 + parts[2].toInt();
        }
    }
    bool ok = false;
    const int v = s.toInt(&ok);
    if (ok && v > 0)
        return v;
    static const QRegularExpression msRe(QStringLiteral("^(\\d+)\\s*ms$"),
                                         QRegularExpression::CaseInsensitiveOption);
    const auto m = msRe.match(s);
    if (m.hasMatch())
        return m.captured(1).toInt() / 1000;
    return 0;
}

int parseDurationSeconds(const QVariantMap &item)
{
    int v = item.value(QStringLiteral("duration")).toInt();
    if (v <= 0)
        v = item.value(QStringLiteral("durationSec")).toInt();
    if (v <= 0)
        v = item.value(QStringLiteral("dt")).toInt();
    if (v <= 0)
        v = item.value(QStringLiteral("length")).toInt();
    if (v <= 0)
        v = item.value(QStringLiteral("durationMs")).toInt();
    if (v <= 0)
        v = parseDurationTextToSeconds(item.value(QStringLiteral("duration")).toString());
    if (v <= 0)
        v = parseDurationTextToSeconds(item.value(QStringLiteral("durationText")).toString());
    if (v <= 0)
        v = parseDurationTextToSeconds(item.value(QStringLiteral("durationStr")).toString());
    if (v <= 0)
        v = parseDurationTextToSeconds(item.value(QStringLiteral("dtText")).toString());
    if (v <= 0)
        return 0;
    // 兼容毫秒字段（如 dt/durationMs）并统一为秒。
    if (v > 10000)
        v /= 1000;
    if (v < 10 || v > 30 * 60)
        return 0;
    return v;
}

int parseDurationSeconds(const QVariantMap &primary, const QVariantMap &fallback)
{
    const int a = parseDurationSeconds(primary);
    if (a > 0)
        return a;
    return parseDurationSeconds(fallback);
}

int parseDurationSecondsFromInfoMap(const QVariantMap &info)
{
    QVariantMap wrapped;
    wrapped.insert(QStringLiteral("duration"), info.value(QStringLiteral("duration")));
    wrapped.insert(QStringLiteral("durationSec"), info.value(QStringLiteral("durationSec")));
    wrapped.insert(QStringLiteral("durationMs"), info.value(QStringLiteral("durationMs")));
    wrapped.insert(QStringLiteral("dt"), info.value(QStringLiteral("dt")));
    wrapped.insert(QStringLiteral("length"), info.value(QStringLiteral("length")));
    wrapped.insert(QStringLiteral("durationText"), info.value(QStringLiteral("durationText")));
    wrapped.insert(QStringLiteral("durationStr"), info.value(QStringLiteral("durationStr")));
    return parseDurationSeconds(wrapped);
}

qint64 parseTimestampMs(const QVariantMap &item)
{
    qint64 ts = item.value(QStringLiteral("createdAt")).toLongLong();
    if (ts <= 0)
        ts = item.value(QStringLiteral("uploadedAt")).toLongLong();
    if (ts <= 0)
        ts = item.value(QStringLiteral("uploadTime")).toLongLong();
    if (ts <= 0)
        ts = item.value(QStringLiteral("publishTime")).toLongLong();
    if (ts <= 0)
        ts = item.value(QStringLiteral("created_at")).toLongLong();
    if (ts <= 0)
    {
        const QString s = item.value(QStringLiteral("createdAt")).toString().trimmed();
        if (!s.isEmpty()) {
            QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
            if (!dt.isValid())
                dt = QDateTime::fromString(s, Qt::ISODate);
            if (dt.isValid())
                ts = dt.toMSecsSinceEpoch();
        }
    }
    if (ts <= 0)
        return 0;
    // 兼容秒级时间戳
    if (ts > 0 && ts < 1000000000000LL)
        ts *= 1000;
    return ts;
}

qint64 parseTimestampMs(const QVariantMap &primary, const QVariantMap &fallback)
{
    const qint64 a = parseTimestampMs(primary);
    if (a > 0)
        return a;
    return parseTimestampMs(fallback);
}

} // namespace

MusicListPage::MusicListPage(Type type, QWidget *parent)
    : QWidget(parent)
    , m_type(type)
    , m_api(new ApiClient(this))
{
    setObjectName(
        m_type == Hot ? QStringLiteral("hotMusicPage")
                      : (m_type == Latest ? QStringLiteral("latestMusicPage")
                                          : QStringLiteral("dailyMusicPage")));
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setupUi();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) { applyPageStyle(); });

    connect(&PlaylistManager::instance(), &PlaylistManager::playlistChanged, this,
            &MusicListPage::updatePlayingHighlight);
    connect(&PlaylistManager::instance(), &PlaylistManager::currentIndexChanged, this,
            &MusicListPage::updatePlayingHighlight);
}

void MusicListPage::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 12, 24, 24);
    root->setSpacing(0);

    m_header = new QWidget(this);
    auto *headerLay = new QVBoxLayout(m_header);
    headerLay->setContentsMargins(0, 0, 0, 0);
    headerLay->setSpacing(12);

    auto *titleRow = new QWidget(m_header);
    auto *titleLay = new QHBoxLayout(titleRow);
    titleLay->setContentsMargins(0, 0, 0, 0);
    titleLay->setSpacing(12);
    titleLay->setAlignment(Qt::AlignVCenter);

    m_backBtn = new QPushButton(titleRow);
    m_backBtn->setFixedSize(40, 40);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->setFlat(true);
    connect(m_backBtn, &QPushButton::clicked, this, &MusicListPage::backRequested);
    titleLay->addWidget(m_backBtn, 0, Qt::AlignVCenter);

    auto *textCol = new QWidget(titleRow);
    auto *textLay = new QVBoxLayout(textCol);
    textLay->setContentsMargins(0, 0, 0, 0);
    textLay->setSpacing(4);

    auto *titleLine = new QWidget(textCol);
    auto *titleLineLay = new QHBoxLayout(titleLine);
    titleLineLay->setContentsMargins(0, 0, 0, 0);
    titleLineLay->setSpacing(8);
    titleLineLay->setAlignment(Qt::AlignBottom);

    m_titleLbl = new QLabel(pageTitle(), titleLine);
    titleLineLay->addWidget(m_titleLbl);

    m_countLbl = new QLabel(titleLine);
    titleLineLay->addWidget(m_countLbl);
    titleLineLay->addStretch();

    textLay->addWidget(titleLine);

    m_descLbl = new QLabel(pageDesc(), textCol);
    m_descLbl->setWordWrap(true);
    textLay->addWidget(m_descLbl);

    titleLay->addWidget(textCol, 1);

    headerLay->addWidget(titleRow);

    auto *menuRow = new QWidget(m_header);
    menuRow->setFixedHeight(40);
    auto *menuLay = new QHBoxLayout(menuRow);
    menuLay->setContentsMargins(0, 0, 0, 0);
    menuLay->setSpacing(12);

    m_playAllBtn = new QPushButton(I18n::instance().tr("playAll"), menuRow);
    m_playAllBtn->setCursor(Qt::PointingHandCursor);
    m_playAllBtn->setFixedHeight(40);
    connect(m_playAllBtn, &QPushButton::clicked, this, [this]() {
        if (!m_musicList.isEmpty())
            emit playAllRequested(m_musicList);
    });
    menuLay->addWidget(m_playAllBtn);

    m_downloadAllBtn = new QPushButton(I18n::instance().tr(QStringLiteral("downloadAll")), menuRow);
    m_downloadAllBtn->setCursor(Qt::PointingHandCursor);
    m_downloadAllBtn->setFixedHeight(40);
    connect(m_downloadAllBtn, &QPushButton::clicked, this, [this]() {
        if (!m_musicList.isEmpty())
            emit downloadAllRequested(m_musicList);
    });
    menuLay->addWidget(m_downloadAllBtn);
    menuLay->addStretch();

    headerLay->addWidget(menuRow);
    root->addWidget(m_header);

    m_songList = new SongListWidget(this);
    m_songList->setListDisplayMode(
        m_type == Hot ? SongListWidget::ListDisplayMode::HotRanking
                      : (m_type == Latest ? SongListWidget::ListDisplayMode::LatestUpload
                                          : SongListWidget::ListDisplayMode::Default));
    m_songList->onSongActivate = [this](const MusicInfo &info) { emit playMusic(info); };
    m_songList->onSongPlayNext = [this](const MusicInfo &info) { emit playMusic(info); };
    m_songList->onSongContextMenu = [this](const MusicInfo &info, const QPoint &pos) {
        showSongContextMenu(info, pos);
    };
    m_songList->onUnfavorite = [this](int id) { emit favoriteRequested(id); };
    m_songList->onDownload = [this](const MusicInfo &info) { emit downloadRequested(info); };
    m_songList->isFavorited = [this](int id) { return m_favoritedIds.contains(id); };
    m_songList->isDownloaded = [](int id) { return MusicDownloadManager::instance().isDownloaded(id); };
    m_songList->onTogglePlayPause = [this]() { emit playPauseRequested(); };
    root->addWidget(m_songList, 1);

    m_emptyWrap = new QWidget(this);
    auto *emptyLay = new QVBoxLayout(m_emptyWrap);
    emptyLay->setAlignment(Qt::AlignCenter);
    emptyLay->setSpacing(12);
    m_emptyIcon = new QLabel(m_emptyWrap);
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    m_emptyIcon->hide();
    emptyLay->addWidget(m_emptyIcon);
    m_statusLabel = new QLabel(m_emptyWrap);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    emptyLay->addWidget(m_statusLabel);
    m_emptyWrap->hide();
    root->addWidget(m_emptyWrap);

    applyPageStyle();
    showLoadingState();
}

QString MusicListPage::pageTitle() const
{
    if (m_type == Hot)
        return I18n::instance().tr("hotMusic");
    if (m_type == Latest)
        return I18n::instance().tr("latestMusic");
    return I18n::instance().tr("dailyRecommend");
}

QString MusicListPage::pageDesc() const
{
    if (m_type == Hot)
        return I18n::instance().tr("hotMusicDesc");
    if (m_type == Latest)
        return I18n::instance().tr("latestMusicDesc");
    return I18n::instance().tr("dailyRecommendDesc");
}

void MusicListPage::applyPageStyle()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString titleFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QString metaFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.72)");
    const QString statusFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.55)");

    if (m_titleLbl) {
        m_titleLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 30px; font-weight: 700; color: %1; }").arg(titleFg));
    }
    if (m_descLbl) {
        m_descLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 14px; color: %1; }").arg(metaFg));
    }
    if (m_countLbl) {
        m_countLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 15px; font-weight: 400; color: %1; padding-bottom: 2px; }")
                                      .arg(metaFg));
    }

    if (m_backBtn) {
        const QString backBg = dark ? QStringLiteral("#2a2a2a") : QStringLiteral("#f0f0f0");
        const QColor backIc = dark ? QColor(244, 246, 255, 210) : QColor(33, 37, 41, 210);
        m_backBtn->setIcon(Icons::renderNamed("SkipPrev", 20, backIc));
        m_backBtn->setIconSize(QSize(20, 20));
        m_backBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: none; border-radius: 20px; }"
            "QPushButton:hover { background: rgba(230,57,80,0.15); }")
                                     .arg(backBg));
    }

    if (m_playAllBtn) {
        m_playAllBtn->setIcon(Icons::renderNamed("Play", 18, QColor(255, 255, 255)));
        m_playAllBtn->setIconSize(QSize(18, 18));
        m_playAllBtn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: #D84B63;"
            "  color: #ffffff;"
            "  border: none;"
            "  border-radius: 20px;"
            "  font-size: 14px;"
            "  font-weight: 500;"
            "  padding: 0 20px;"
            "}"
            "QPushButton:hover { background: #ff5070; }"
            "QPushButton:disabled { background: rgba(230,57,80,0.35); color: rgba(255,255,255,0.6); }"));
    }

    const QString secondaryBg = dark ? QStringLiteral("#2a2a2a") : QStringLiteral("#f0f0f0");
    const QString secondaryFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QColor secondaryIc = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
    if (m_downloadAllBtn) {
        m_downloadAllBtn->setIcon(Icons::renderNamed("Download", 18, secondaryIc));
        m_downloadAllBtn->setIconSize(QSize(18, 18));
        m_downloadAllBtn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: %1;"
            "  color: %2;"
            "  border: none;"
            "  border-radius: 20px;"
            "  font-size: 14px;"
            "  font-weight: 500;"
            "  padding: 0 20px;"
            "}"
            "QPushButton:hover { background: rgba(230,57,80,0.15); }"
            "QPushButton:disabled { color: rgba(128,128,128,0.8); }")
                                         .arg(secondaryBg, secondaryFg));
    }

    if (m_statusLabel) {
        m_statusLabel->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 14px; color: %1; padding: 60px 24px; }").arg(statusFg));
    }

    if (m_songList)
        m_songList->applyTheme();
}

void MusicListPage::retranslate()
{
    if (m_titleLbl)
        m_titleLbl->setText(pageTitle());
    if (m_descLbl)
        m_descLbl->setText(pageDesc());
    if (m_playAllBtn)
        m_playAllBtn->setText(I18n::instance().tr("playAll"));
    if (m_downloadAllBtn)
        m_downloadAllBtn->setText(I18n::instance().tr(QStringLiteral("downloadAll")));
    if (m_songList)
        m_songList->retranslate();
    updateHeaderMeta();
    applyPageStyle();
}

void MusicListPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updatePlayingHighlight();
}

void MusicListPage::releaseCachedData()
{
    ++m_fetchGeneration;
    m_musicList.clear();
    m_musicList.squeeze();
    if (m_songList)
        m_songList->setSongs({});
    updateHeaderMeta();
}

void MusicListPage::refresh()
{
    ++m_fetchGeneration;
    m_musicList.clear();
    showLoadingState();
    fetchData();
}

void MusicListPage::refreshKeepingContent()
{
    ++m_fetchGeneration;
    if (m_musicList.isEmpty())
        showLoadingState();
    else
        updateHeaderMeta();
    fetchData();
}

void MusicListPage::showLoadingState()
{
    if (m_songList)
        m_songList->setSongs({});
    showPageStatus(I18n::instance().tr("loading"));
    updateHeaderMeta();
}

void MusicListPage::showPageStatus(const QString &text, const char *iconName)
{
    if (m_emptyIcon) {
        if (iconName) {
            const bool dark = Theme::ThemeManager::instance().isDarkMode();
            const QColor ic = dark ? QColor(244, 246, 255, 90) : QColor(33, 37, 41, 90);
            m_emptyIcon->setPixmap(Icons::renderNamed(iconName, 64, ic));
            m_emptyIcon->show();
        } else {
            m_emptyIcon->hide();
        }
    }
    if (m_statusLabel) {
        m_statusLabel->setText(text);
        m_statusLabel->show();
    }
    if (m_emptyWrap)
        m_emptyWrap->show();
    if (m_songList)
        m_songList->hide();
}

void MusicListPage::hidePageStatus()
{
    if (m_emptyIcon)
        m_emptyIcon->hide();
    if (m_statusLabel)
        m_statusLabel->hide();
    if (m_emptyWrap)
        m_emptyWrap->hide();
}

void MusicListPage::fetchData()
{
    const int gen = m_fetchGeneration;
    if (m_type == Daily && !UserManager::instance().isLoggedIn()) {
        m_musicList.clear();
        updateHeaderMeta();
        showPageStatus(I18n::instance().tr("pleaseLoginFirst"), "Person");
        return;
    }

    auto finish = [this, gen](bool success, const QList<QVariantMap> &results) {
        QTimer::singleShot(0, this, [this, success, results, gen]() {
            if (gen != m_fetchGeneration)
                return;
            m_musicList.clear();
            if (success) {
                m_musicList.reserve(results.size());
                for (const auto &item : results) {
                    const QVariantMap nested = unwrapMusicNode(item);
                    const QVariantMap &primary = nested.isEmpty() ? item : nested;
                    const QVariantMap &fallback = nested.isEmpty() ? QVariantMap{} : item;
                    const QVariant root = QVariant(item);
                    MusicInfo info;
                    info.id = firstNonNull(primary, fallback, QStringLiteral("id")).toInt();
                    if (info.id <= 0)
                        info.id = firstNonNull(primary, fallback, QStringLiteral("musicId")).toInt();
                    if (info.id <= 0)
                        info.id = deepFindByKeys(root, {QStringLiteral("id"), QStringLiteral("musicId")}).toInt();
                    info.title = firstNonNull(primary, fallback, QStringLiteral("title")).toString();
                    info.artist = firstNonNull(primary, fallback, QStringLiteral("artist")).toString();
                    info.album = firstNonNull(primary, fallback, QStringLiteral("album")).toString();
                    info.duration = (m_type == Daily) ? 0 : parseDurationSeconds(primary, fallback);
                    if (info.title.isEmpty())
                        info.title = firstNonNull(primary, fallback, QStringLiteral("name")).toString();
                    if (info.artist.isEmpty())
                        info.artist = firstNonNull(primary, fallback, QStringLiteral("author")).toString();
                    if (info.album.isEmpty())
                        info.album = firstNonNull(primary, fallback, QStringLiteral("albumName")).toString();
                    if (m_type != Daily && info.duration <= 0) {
                        const QVariant dv = deepFindByKeys(
                            root, {QStringLiteral("duration"), QStringLiteral("durationSec"),
                                   QStringLiteral("durationMs"), QStringLiteral("dt"),
                                   QStringLiteral("length"), QStringLiteral("durationText"),
                                   QStringLiteral("durationStr"), QStringLiteral("dtText")});
                        QVariantMap dm;
                        dm.insert(QStringLiteral("duration"), dv);
                        info.duration = parseDurationSeconds(dm);
                    }
                    if (m_type != Daily && info.duration <= 0)
                        info.duration = deepFindDurationSeconds(root);
                    if (info.title.isEmpty())
                        info.title = deepFindByKeys(root, {QStringLiteral("title"), QStringLiteral("name")}).toString();
                    if (info.artist.isEmpty())
                        info.artist = deepFindByKeys(root, {QStringLiteral("artist"), QStringLiteral("author")}).toString();
                    if (info.album.isEmpty())
                        info.album = deepFindByKeys(root, {QStringLiteral("album"), QStringLiteral("albumName")}).toString();
                    info.coverUrl = QString::fromUtf8("%1/api/music/cover/%2")
                                        .arg(Theme::kApiBase)
                                        .arg(info.id);
                    if (info.id <= 0)
                        continue;
                    info.maxQuality = firstNonNull(primary, fallback, QStringLiteral("maxQuality")).toString();
                    if (info.maxQuality.isEmpty())
                        info.maxQuality = deepFindByKeys(root, {QStringLiteral("maxQuality")}).toString();
                    if (m_type == Hot)
                        info.playCount =
                            firstNonNull(primary, fallback, QStringLiteral("playCount")).toInt();
                    else
                        info.uploadedAtMs = parseTimestampMs(primary, fallback);
                    m_musicList.append(info);
                }
            }
            presentSongs();
            backfillDurationsFromMusicInfo(gen);
        });
    };

    if (m_type == Hot) {
        m_api->fetchRanking([finish](bool success, const QList<QVariantMap> &results) {
            finish(success, results);
        });
    } else if (m_type == Latest) {
        m_api->fetchLatest(300, [finish](bool success, const QList<QVariantMap> &results) {
            finish(success, results);
        });
    } else {
        m_api->fetchDailyRecommendations([finish](bool success, const QList<QVariantMap> &results) {
            finish(success, results);
        });
    }
}

void MusicListPage::presentSongs()
{
    updateHeaderMeta();

    if (m_musicList.isEmpty()) {
        if (m_songList)
            m_songList->setSongs({});
        showPageStatus(I18n::instance().tr("noData"), "SearchOff");
        return;
    }

    hidePageStatus();
    if (m_songList) {
        m_songList->setSongs(m_musicList);
        m_songList->refreshFavoriteDisplay();
        m_songList->refreshDownloadDisplay();
        m_songList->show();
    }
    updatePlayingHighlight();
}

void MusicListPage::backfillDurationsFromMusicInfo(int gen)
{
    if (m_type != Daily)
        return;
    int pending = 0;
    for (int i = 0; i < m_musicList.size(); ++i) {
        if (m_musicList[i].id <= 0 || m_musicList[i].duration > 0)
            continue;
        ++pending;
    }
    if (pending <= 0)
        return;

    auto remaining = QSharedPointer<int>::create(pending);
    auto changed = QSharedPointer<bool>::create(false);
    auto flush = [this, gen, remaining, changed]() {
        --(*remaining);
        if (*remaining > 0 || !*changed || gen != m_fetchGeneration)
            return;
        if (m_songList) {
            m_songList->setSongs(m_musicList);
            m_songList->refreshFavoriteDisplay();
            m_songList->refreshDownloadDisplay();
            m_songList->show();
        }
        updateHeaderMeta();
        updatePlayingHighlight();
    };

    for (int i = 0; i < m_musicList.size(); ++i) {
        if (m_musicList[i].id <= 0 || m_musicList[i].duration > 0)
            continue;
        const int idx = i;
        const int musicId = m_musicList[i].id;
        m_api->fetchMusicInfo(musicId, [this, gen, idx, changed, flush](bool ok, const QVariantMap &infoMap) {
            if (!ok || gen != m_fetchGeneration) {
                flush();
                return;
            }
            if (idx < 0 || idx >= m_musicList.size()) {
                flush();
                return;
            }
            if (m_musicList[idx].duration > 0) {
                flush();
                return;
            }
            const int sec = parseDurationSecondsFromInfoMap(infoMap);
            if (sec <= 0) {
                flush();
                return;
            }
            m_musicList[idx].duration = sec;
            *changed = true;
            flush();
        });
    }
}

void MusicListPage::updateHeaderMeta()
{
    if (m_countLbl) {
        m_countLbl->setText(
            I18n::instance().tr(QStringLiteral("favoritePageSongCount")).arg(m_musicList.size()));
    }
    if (m_playAllBtn)
        m_playAllBtn->setEnabled(!m_musicList.isEmpty());
    if (m_downloadAllBtn)
        m_downloadAllBtn->setEnabled(!m_musicList.isEmpty());
}

void MusicListPage::refreshDownloadDisplay()
{
    if (m_songList)
        m_songList->refreshDownloadDisplay();
}

int MusicListPage::currentPlayingMusicId() const
{
    const auto &mgr = PlaylistManager::instance();
    const int idx = mgr.currentIndex();
    if (idx < 0 || idx >= mgr.playlist().size())
        return -1;
    return mgr.playlist()[idx].id;
}

void MusicListPage::updatePlayingHighlight()
{
    if (m_songList)
        m_songList->setCurrentPlayingId(currentPlayingMusicId());
}

void MusicListPage::setPlaybackPaused(bool paused)
{
    if (m_songList)
        m_songList->setPlaybackPaused(paused);
}

void MusicListPage::setFavoritedMusicIds(const QSet<int> &ids)
{
    m_favoritedIds = ids;
    if (m_songList)
        m_songList->refreshFavoriteDisplay();
}

void MusicListPage::showSongContextMenu(const MusicInfo &info, const QPoint &globalPos)
{
    if (info.id <= 0 && info.localPath.isEmpty())
        return;

    // 统一菜单（设计稿）：收藏 / 播放队列 / 下载，全站同一套
    SongMenuBuilder::State state;
    state.favorited = m_favoritedIds.contains(info.id);
    state.downloaded = MusicDownloadManager::instance().isDownloaded(info.id);
    state.inPlayQueue = SongMenuBuilder::queueContains(info.id);

    SongMenuBuilder::Handlers handlers;
    handlers.toggleFavorite = [this, info]() { emit favoriteRequested(info.id); };
    handlers.toggleQueue = [info]() { SongMenuBuilder::toggleQueue(info); };
    handlers.download = [this, info]() { emit downloadRequested(info); };

    QList<SongContextMenuPopup::Entry> entries =
        SongMenuBuilder::buildStandard(info, state, handlers);

    // 本页特有项追加在统一菜单之后
    SongContextMenuPopup::Entry playlistEntry;
    playlistEntry.iconName = "AddList";
    playlistEntry.label = I18n::instance().tr(QStringLiteral("addToPlaylist"));
    playlistEntry.action = [this, info]() { emit addToPlaylist(info); };
    entries.append(playlistEntry);

    SongContextMenuPopup::showAt(window() ? window() : this, globalPos, entries);
}

void MusicListPage::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
}
