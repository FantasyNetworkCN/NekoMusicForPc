/**
 * @file homepage.cpp
 * @brief 发现页 — 每日推荐、快捷入口与自适应歌单网格
 *
 * 推荐歌单：GET /api/playlists/recommended
 * 热门音乐：GET /api/music/ranking
 * 最新音乐：GET /api/music/latest
 */

#include "homepage.h"
#include "core/apiclient.h"
#include "core/i18n.h"
#include "core/covercache.h"
#include "core/usermanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/playlistcard.h"
#include "ui/flowlayout.h"
#include "ui/glasspaint.h"
#include "ui/scrollareafix.h"
#include "ui/svgicon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTime>
#include <QResizeEvent>
#include <QPainterPath>

namespace {

// 原生按钮保留键盘与无障碍语义，装饰与子标签不截获点击。
class DailyMixButton final : public QPushButton
{
public:
    explicit DailyMixButton(QWidget *parent) : QPushButton(parent) {}
protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool dark = Theme::ThemeManager::instance().isDarkMode();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 24, 24);
        p.setClipPath(shape);
        GlassPaint::paintCachedBackdrop(p, this, rect());
        QLinearGradient wash(rect().topLeft(), rect().bottomRight());
        wash.setColorAt(0, dark ? QColor(33, 76, 99, 220) : QColor(210, 241, 249, 230));
        wash.setColorAt(1, dark ? QColor(37, 43, 76, 220) : QColor(226, 230, 251, 210));
        p.fillPath(shape, wash);
        if (underMouse() || isDown())
            p.fillPath(shape, QColor(255, 255, 255, isDown() ? 5 : 14));
        p.setPen(QPen(dark ? QColor(160, 220, 249, 20) : QColor(255, 255, 255, 100), 1));
        p.setBrush(Qt::NoBrush);
        const QPointF orbit(width() - 100, height() / 2.0);
        p.drawEllipse(orbit, 155, 155);
        p.drawEllipse(orbit, 185, 185);
        p.setClipping(false);
        p.setPen(QPen(hasFocus() ? QColor(Theme::kLavender)
                                : (dark ? QColor(182, 221, 250, 45) : QColor(255, 255, 255, 210)),
                      hasFocus() ? 2 : 1));
        p.drawPath(shape);
    }
    void resizeEvent(QResizeEvent *event) override
    {
        QPushButton::resizeEvent(event);
        if (auto *art = findChild<QLabel *>(QStringLiteral("hpDailyCover")))
            art->setVisible(width() >= 650);
    }
};

class DailyArtwork final : public QLabel
{
public:
    explicit DailyArtwork(QWidget *parent) : QLabel(parent) {}
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(width() / 176.0, height() / 160.0);
        const QPointF c(88, 80);
        QRadialGradient vinyl(c, 78);
        vinyl.setColorAt(0, QColor(37, 53, 70));
        vinyl.setColorAt(.5, QColor(17, 27, 43));
        vinyl.setColorAt(1, QColor(30, 46, 65));
        p.setPen(QPen(QColor(197, 230, 255, 65), 1));
        p.setBrush(vinyl);
        p.drawEllipse(c, 78, 78);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(200, 230, 255, 18), 1));
        for (int radius = 44; radius < 76; radius += 6)
            p.drawEllipse(c, radius, radius);
        QPainterPath clip;
        clip.addEllipse(c, 34, 34);
        p.setClipPath(clip);
        p.drawPixmap(QRectF(c.x() - 34, c.y() - 34, 68, 68).toRect(), pixmap());
        p.setClipping(false);
        p.setBrush(QColor(213, 243, 252));
        p.setPen(Qt::NoPen);
        p.drawEllipse(c, 4, 4);
    }
};

} // namespace

// ─── HomePage ────────────────────────────────────────

HomePage::HomePage(ApiClient *apiClient, QWidget *parent) : QWidget(parent), m_apiClient(apiClient)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setupUi();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) {
                applyDailyEntryStyle();
                update();
            });

    connect(&UserManager::instance(), &UserManager::loginStateChanged, this, [this]() {
        refreshDailyEntry();
        fetchDailyEntryCover();
        m_playlistRefreshRequested = true;
        refreshData();
    });

    // 延迟加载数据，先显示UI
    QTimer::singleShot(100, this, &HomePage::refreshData);

    // 入场淡入
    auto *eff = new QGraphicsOpacityEffect(this);
    eff->setOpacity(0.0);
    setGraphicsEffect(eff);
    auto *anim = new QPropertyAnimation(eff, "opacity");
    anim->setDuration(600);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::finished, this, [this]() {
        setGraphicsEffect(nullptr);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void HomePage::setupUi()
{
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setObjectName("hpScroll");
    auto *container = new QWidget(m_scroll);
    container->setObjectName("hpContainer");
    auto *lay = new QVBoxLayout(container);
    lay->setContentsMargins(24, 12, 24, 28);
    lay->setSpacing(14);

    auto *headingRow = new QHBoxLayout;
    auto *headings = new QVBoxLayout;
    headings->setSpacing(6);
    m_heading = new QLabel(container);
    m_heading->setObjectName("hpHeading");
    m_greeting = new QLabel(container);
    m_greeting->setObjectName("hpSubtitle");
    headings->addWidget(m_heading);
    headings->addWidget(m_greeting);
    headingRow->addLayout(headings, 1);
    m_refreshBtn = new QPushButton(container);
    m_refreshBtn->setObjectName("hpRefreshBtn");
    m_refreshBtn->setFixedSize(36, 36);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() {
        m_playlistRefreshRequested = true;
        refreshData();
    });
    headingRow->addWidget(m_refreshBtn, 0, Qt::AlignVCenter);
    lay->addLayout(headingRow);

    m_dailyEntry = new DailyMixButton(container);
    m_dailyEntry->setObjectName("hpDailyRecommendEntry");
    m_dailyEntry->setCursor(Qt::PointingHandCursor);
    m_dailyEntry->setMinimumHeight(164);
    m_dailyEntry->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *dailyLay = new QHBoxLayout(m_dailyEntry);
    dailyLay->setContentsMargins(24, 16, 20, 16);
    dailyLay->setSpacing(12);
    auto *dailyTextWrap = new QWidget(m_dailyEntry);
    dailyTextWrap->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *text = new QVBoxLayout(dailyTextWrap);
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(5);
    auto *eyebrow = new QLabel(QStringLiteral("MADE FOR YOU / DAILY MIX"), dailyTextWrap);
    eyebrow->setObjectName("hpEyebrow");
    m_dailyTitle = new QLabel(dailyTextWrap);
    m_dailyTitle->setObjectName("hpDailyTitle");
    m_dailyDesc = new QLabel(dailyTextWrap);
    m_dailyDesc->setObjectName("hpSubtitle");
    m_dailyDesc->setWordWrap(true);
    m_dailyAction = new QLabel(dailyTextWrap);
    m_dailyAction->setObjectName("hpDailyAction");
    m_dailyAction->setAlignment(Qt::AlignCenter);
    m_dailyAction->setMinimumHeight(34);
    text->addWidget(eyebrow);
    text->addWidget(m_dailyTitle);
    text->addWidget(m_dailyDesc);
    text->addSpacing(4);
    text->addWidget(m_dailyAction, 0, Qt::AlignLeft);
    dailyLay->addWidget(dailyTextWrap, 1);
    m_dailyCover = new DailyArtwork(m_dailyEntry);
    m_dailyCover->setObjectName("hpDailyCover");
    m_dailyCover->setFixedSize(145, 132);
    m_dailyCover->setAttribute(Qt::WA_TransparentForMouseEvents);
    dailyLay->addWidget(m_dailyCover, 0, Qt::AlignVCenter);
    connect(m_dailyEntry, &QPushButton::clicked, this, &HomePage::navigateToDailyRecommendations);
    lay->addWidget(m_dailyEntry);

    auto *shortcuts = new QHBoxLayout;
    shortcuts->setSpacing(14);
    auto makeEntry = [this, container](bool hot, QLabel **caption) {
        auto *button = new QPushButton(container);
        button->setObjectName(hot ? "hpHotEntry" : "hpLatestEntry");
        button->setProperty("discoveryTile", true);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumHeight(72);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *row = new QHBoxLayout(button);
        row->setContentsMargins(18, 12, 18, 12);
        row->setSpacing(14);
        auto *icon = new QLabel(button);
        icon->setObjectName(hot ? "hpHotGlyph" : "hpLatestGlyph");
        icon->setFixedSize(38, 38);
        icon->setAlignment(Qt::AlignCenter);
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(icon);
        *caption = new QLabel(button);
        (*caption)->setObjectName("hpShortcutCaption");
        (*caption)->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(*caption, 1);
        auto *arrow = new QLabel(QStringLiteral("›"), button);
        arrow->setObjectName("hpTileArrow");
        arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(arrow);
        connect(button, &QPushButton::clicked, this, [this, hot]() { emit navigateToMusicList(hot); });
        return button;
    };
    m_hotEntry = makeEntry(true, &m_hotCaption);
    m_latestEntry = makeEntry(false, &m_latestCaption);
    shortcuts->addWidget(m_hotEntry, 1);
    shortcuts->addWidget(m_latestEntry, 1);
    lay->addLayout(shortcuts);

    auto *playlistHead = new QHBoxLayout;
    auto *playlistHeadings = new QVBoxLayout;
    playlistHeadings->setSpacing(2);
    m_playlistTitle = new QLabel(I18n::instance().tr("recommend_playlists"), container);
    m_playlistTitle->setObjectName("hpSectionTitle");
    m_playlistStatus = new QLabel(container);
    m_playlistStatus->setObjectName("hpSectionStatus");
    playlistHeadings->addWidget(m_playlistTitle);
    playlistHeadings->addWidget(m_playlistStatus);
    playlistHead->addLayout(playlistHeadings, 1);
    lay->addLayout(playlistHead);
    m_cardContainer = new QWidget(container);
    m_cardContainer->setObjectName("hpCardContainer");
    m_cardLayout = new FlowLayout(m_cardContainer, 16);
    auto *loading = new QLabel(I18n::instance().tr("loading"), m_cardContainer);
    loading->setObjectName("hpLoading");
    loading->setMinimumHeight(80);
    m_cardLayout->addWidget(loading);
    lay->addWidget(m_cardContainer);
    lay->addStretch();
    m_scroll->setWidget(container);
    nekoPolishScrollAreaViewport(m_scroll);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_scroll);
    retranslate();
    applyDailyEntryStyle();
    setDailyCoverPlaceholder();
}

void HomePage::refreshData()
{
    m_refreshBtn->setEnabled(false);
    m_playlistReady = false;
    m_playlists.clear();

    const bool refresh = m_playlistRefreshRequested;
    m_playlistRefreshRequested = false;
    fetchPlaylists(refresh);
    fetchDailyEntryCover();
}

void HomePage::retranslate()
{
    auto &i18n = I18n::instance();
    refreshDailyEntry();
    m_heading->setText(i18n.tr("discoverMusic"));
    const int hour = QTime::currentTime().hour();
    m_greeting->setText(i18n.tr(hour < 12 ? "discoverMorning" : hour < 18 ? "discoverAfternoon" : "discoverEvening"));
    m_hotCaption->setText(i18n.tr("hot_music"));
    m_latestCaption->setText(i18n.tr("latest_music"));
    m_hotEntry->setAccessibleName(m_hotCaption->text());
    m_latestEntry->setAccessibleName(m_latestCaption->text());
    m_refreshBtn->setToolTip(i18n.tr("refresh"));
    m_refreshBtn->setAccessibleName(i18n.tr("refresh"));
    if (m_playlistTitle)
        m_playlistTitle->setText(i18n.tr("recommend_playlists"));
    if (m_playlistStatus) {
        m_playlistStatus->setText(i18n.tr(m_playlistPersonalized
                                              ? "recommendPlaylistsPersonalized"
                                              : "recommendPlaylistsPopular"));
    }
    if (auto *label = findChild<QLabel *>("hpLoading"))
        label->setText(i18n.tr("loading"));
    if (auto *label = findChild<QLabel *>("hpEmpty"))
        label->setText(i18n.tr("discoverEmpty"));
}

void HomePage::refreshDailyEntry()
{
    auto &i18n = I18n::instance();
    const bool loggedIn = UserManager::instance().isLoggedIn();
    m_dailyTitle->setText(i18n.tr("dailyRecommend"));
    m_dailyDesc->setText(i18n.tr(loggedIn ? "dailyMixDescription" : "dailyMixGuest"));
    m_dailyAction->setText(i18n.tr(loggedIn ? "dailyMixOpen" : "dailyMixGuestAction") + QStringLiteral("  →"));
    m_dailyEntry->setAccessibleName(m_dailyTitle->text() + QStringLiteral(" · ") + m_dailyAction->text());
}

void HomePage::setDailyCoverPlaceholder()
{
    if (!m_dailyCover)
        return;
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    QPixmap pm(70, 70);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(0, 0, 70, 70, 10, 10);
    QLinearGradient g(0, 0, 70, 70);
    g.setColorAt(0.0, dark ? QColor(33, 91, 117) : QColor(159, 225, 241));
    g.setColorAt(1.0, dark ? QColor(46, 62, 107) : QColor(155, 173, 236));
    p.fillPath(path, g);
    p.setClipPath(path);
    const QPixmap musicIc = Icons::renderNamed("Music", 26,
                                               dark ? QColor(255, 255, 255, 220)
                                                    : QColor(84, 62, 112, 220));
    p.drawPixmap((70 - 26) / 2, (70 - 26) / 2, musicIc);
    p.end();
    m_dailyCover->setPixmap(pm);
}

void HomePage::setDailyCoverByMusicId(int musicId)
{
    if (!m_dailyCover || musicId <= 0) {
        setDailyCoverPlaceholder();
        return;
    }
    disconnect(m_dailyCoverConn);
    m_dailyCoverConn = {};
    const QString key = QString::number(musicId);
    if (QPixmap cached = CoverCache::instance()->get(key); !cached.isNull()) {
        m_dailyCover->setPixmap(cached.scaled(70, 70, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
        return;
    }
    setDailyCoverPlaceholder();
    m_dailyCoverConn = connect(CoverCache::instance(), &CoverCache::coverLoaded, this,
                               [this, key](const QString &id, const QPixmap &pix) {
                                   if (id != key || pix.isNull() || !m_dailyCover)
                                       return;
                                   m_dailyCover->setPixmap(
                                       pix.scaled(70, 70, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                               });
    const QString coverUrl = QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(musicId);
    CoverCache::instance()->fetchCover(key, coverUrl);
}

void HomePage::fetchDailyEntryCover()
{
    if (!UserManager::instance().isLoggedIn()) {
        setDailyCoverPlaceholder();
        return;
    }
    QUrl url(QString::fromUtf8("%1/api/user/recommendations/daily").arg(Theme::kApiBase));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", UserManager::instance().token().toUtf8());
    req.setTransferTimeout(5000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setDailyCoverPlaceholder();
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        if (!root.value("success").toBool()) {
            setDailyCoverPlaceholder();
            return;
        }
        QJsonArray arr = root.value("data").toArray();
        if (arr.isEmpty())
            arr = root.value("results").toArray();
        if (arr.isEmpty())
            arr = root.value("recommendations").toArray();
        if (arr.isEmpty()) {
            setDailyCoverPlaceholder();
            return;
        }
        QJsonObject first = arr.first().toObject();
        if (first.contains("music") && first.value("music").isObject())
            first = first.value("music").toObject();
        else if (first.contains("song") && first.value("song").isObject())
            first = first.value("song").toObject();
        else if (first.contains("track") && first.value("track").isObject())
            first = first.value("track").toObject();
        int musicId = first.value("id").toInt();
        if (musicId <= 0)
            musicId = first.value("musicId").toInt();
        setDailyCoverByMusicId(musicId);
    });
}

void HomePage::applyDailyEntryStyle()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QColor accent = dark ? QColor(Theme::kLavender) : QColor(Theme::kLavenderDk);
    m_refreshBtn->setIcon(Icons::iconNamed("Refresh", 18, accent, accent));
    if (auto *icon = findChild<QLabel *>("hpHotGlyph"))
        icon->setPixmap(Icons::renderNamed("Eq", 24, accent));
    if (auto *icon = findChild<QLabel *>("hpLatestGlyph"))
        icon->setPixmap(Icons::renderNamed("Music", 24, accent));
    m_dailyEntry->update();
}

void HomePage::fetchPlaylists(bool refresh)
{
    if (!m_apiClient) {
        m_playlistReady = true;
        rebuildRecommendSection();
        return;
    }
    const QPointer<HomePage> guard(this);
    m_apiClient->fetchRecommendedPlaylists(
        refresh,
        [guard](bool ok, bool personalized, const QList<QVariantMap> &items) {
            if (!guard)
                return;
            HomePage *self = guard.data();
            QList<PlaylistInfo> result;
            if (ok) {
                result.reserve(qMin(5, items.size()));
                for (const QVariantMap &obj : items) {
                    PlaylistInfo info;
                    info.id = obj.value(QStringLiteral("id")).toInt();
                    info.name = obj.value(QStringLiteral("name")).toString();
                    info.description = obj.value(QStringLiteral("description")).toString();
                    info.musicCount = obj.value(QStringLiteral("musicCount")).toInt();
                    info.reason = obj.value(QStringLiteral("reason")).toString();
                    info.source = obj.value(QStringLiteral("source")).toString();
                    info.exploration = obj.value(QStringLiteral("exploration")).toBool();
                    info.creator = obj.value(QStringLiteral("creator")).toMap()
                                       .value(QStringLiteral("nickname")).toString();

                    QString cover = obj.value(QStringLiteral("firstMusicCover")).toString();
                    if (cover.isEmpty()) {
                        const QVariantList covers =
                            obj.value(QStringLiteral("coverUrls")).toList();
                        if (!covers.isEmpty())
                            cover = covers.first().toString();
                    }
                    info.coverUrl = CoverCache::resolveCoverUrl(cover);
                    if (info.id > 0)
                        result.append(info);
                    if (result.size() >= 5)
                        break;
                }
            }
            self->m_playlistPersonalized = personalized;
            self->m_playlists = std::move(result);
            self->m_playlistReady = true;
            self->rebuildRecommendSection();
        });
}

void HomePage::rebuildRecommendSection()
{
    if (!m_playlistReady)
        return;
    m_refreshBtn->setEnabled(true);
    if (m_playlistStatus) {
        m_playlistStatus->setText(I18n::instance().tr(m_playlistPersonalized
                                                          ? "recommendPlaylistsPersonalized"
                                                          : "recommendPlaylistsPopular"));
    }
    while (auto *item = m_cardLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const auto &info : m_playlists) {
        auto *card = new PlaylistCard(info, m_cardContainer);
        connect(card, &PlaylistCard::clicked, this, &HomePage::navigateToPlaylist);
        m_cardLayout->addWidget(card);
    }
    if (m_playlists.isEmpty()) {
        auto *empty = new QLabel(I18n::instance().tr("discoverEmpty"), m_cardContainer);
        empty->setObjectName("hpEmpty");
        empty->setWordWrap(true);
        empty->setFixedSize(340, 90);
        m_cardLayout->addWidget(empty);
    }
    m_cardLayout->invalidate();
    m_cardContainer->updateGeometry();
}

void HomePage::paintEvent(QPaintEvent *) {}
