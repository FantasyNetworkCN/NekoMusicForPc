/**
 * @file addtoplaylistdialog.cpp
 * @brief 添加到歌单 — 1:1 SPlayer PlaylistAdd.vue
 */

#include "addtoplaylistdialog.h"
#include "lineinputdialog.h"
#include "core/apiclient.h"
#include "core/playlistdb.h"
#include "core/usermanager.h"
#include "core/covercache.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"
#include "ui/toast.h"
#include "ui/scrollareafix.h"
#include "ui/glasspaint.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QPushButton>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVariantAnimation>
#include <QParallelAnimationGroup>
#include <QEasingCurve>
#include <QTimer>
#include <functional>
#include <memory>

namespace {

constexpr int kCardMinW = 680;
constexpr int kCardMaxW = 860;
constexpr int kCardWidthRatioPercent = 78; // 相对主窗口宽度
constexpr int kCardHeightRatioPercent = 68; // 约 SPlayer 70vh，略收一点
constexpr int kCardMinH = 400;
constexpr int kCardChromeH = 76; // 标题栏 + 内边距（列表区之外）
constexpr qreal kBlurRadius = 48.0;
constexpr int kCoverSize = 50;
constexpr int kRowH = 58;
constexpr int kRowRadius = 8;

class PlaylistAddRowWidget : public QWidget
{
public:
    explicit PlaylistAddRowWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(kRowH);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);
    }

    int playlistId = 0;
    QLabel *coverLabel = nullptr;
    QLabel *titleLabel = nullptr;
    QLabel *metaLabel = nullptr;
    std::function<void()> onActivated;
    QMetaObject::Connection coverConn;

    void setPlaceholderCover()
    {
        if (!coverLabel)
            return;
        QPixmap pix(kCoverSize, kCoverSize);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addRoundedRect(0, 0, kCoverSize, kCoverSize, kRowRadius, kRowRadius);
        p.fillPath(path, QColor(128, 128, 128, 55));
        p.setClipPath(path);
        p.drawPixmap(13, 13, Icons::renderNamed("Music", 24, QColor(255, 255, 255, 140)));
        coverLabel->setPixmap(pix);
    }

    void applyCoverPixmap(const QPixmap &src)
    {
        if (!coverLabel)
            return;
        if (src.isNull()) {
            setPlaceholderCover();
            return;
        }
        QPixmap scaled = src.scaled(kCoverSize, kCoverSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        QPixmap out(kCoverSize, kCoverSize);
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath clip;
        clip.addRoundedRect(0, 0, kCoverSize, kCoverSize, kRowRadius, kRowRadius);
        p.setClipPath(clip);
        const int x = (kCoverSize - scaled.width()) / 2;
        const int y = (kCoverSize - scaled.height()) / 2;
        p.drawPixmap(x, y, scaled);
        coverLabel->setPixmap(out);
    }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onActivated)
            onActivated();
        QWidget::mousePressEvent(e);
    }

    void enterEvent(QEnterEvent *) override
    {
        m_hovered = true;
        update();
    }

    void leaveEvent(QEvent *) override
    {
        m_hovered = false;
        update();
    }

    void paintEvent(QPaintEvent *) override
    {
        if (!m_hovered)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QColor bg(22, 119, 184, 31);
        if (!Theme::ThemeManager::instance().isDarkMode())
            bg = QColor(22, 119, 184, 22);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(rect().adjusted(2, 1, -2, -1), kRowRadius, kRowRadius);
    }

    bool m_hovered = false;
};

QString coverUrlForMusicId(int musicId)
{
    if (musicId <= 0)
        return QString();
    return QString::fromUtf8("%1/api/music/cover/%2").arg(Theme::kApiBase).arg(musicId);
}

QString playlistCoverUrlFromMap(const QVariantMap &pl)
{
    static const QStringList kCoverKeys = {
        QStringLiteral("coverUrl"),
        QStringLiteral("cover_url"),
        QStringLiteral("cover"),
        QStringLiteral("imageUrl"),
        QStringLiteral("image"),
        QStringLiteral("picUrl"),
        QStringLiteral("avatarUrl"),
    };
    for (const QString &key : kCoverKeys) {
        const QString raw = pl.value(key).toString().trimmed();
        if (!raw.isEmpty())
            return CoverCache::resolveCoverUrl(raw);
    }

    static const QStringList kFirstIdKeys = {
        QStringLiteral("firstMusicId"),
        QStringLiteral("first_music_id"),
        QStringLiteral("coverMusicId"),
        QStringLiteral("cover_music_id"),
        QStringLiteral("musicId"),
    };
    for (const QString &key : kFirstIdKeys) {
        const int id = pl.value(key).toInt();
        if (id > 0)
            return coverUrlForMusicId(id);
    }
    return {};
}

QString cacheKeyFromCoverUrl(const QString &coverUrl)
{
    QString key = CoverCache::musicIdFromCoverUrl(coverUrl);
    if (!key.isEmpty())
        return key;
    return coverUrl;
}

/** 全屏模糊遮罩（独立层，避免与卡片 opacity 冲突） */
class PlaylistAddBackdrop final : public QWidget
{
public:
    explicit PlaylistAddBackdrop(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_StyledBackground, true);
        setCursor(Qt::ArrowCursor);
    }

    QPixmap blurPixmap;
    qreal scrimOpacity = 0.0;
    std::function<void()> onClicked;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setOpacity(scrimOpacity);
        if (!blurPixmap.isNull())
            p.drawPixmap(rect(), blurPixmap);
        else
            p.fillRect(rect(), QColor(36, 36, 36, 220));
        p.fillRect(rect(), QColor(0, 0, 0, 72));
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (onClicked)
            onClicked();
        event->accept();
    }
};

} // namespace

AddToPlaylistDialog::AddToPlaylistDialog(const MusicInfo &music, ApiClient *apiClient, QWidget *parent)
    : QWidget(parent), m_music(music), m_musics({music}), m_apiClient(apiClient)
{
    m_isLocal = music.id < 0;
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    setupUi();
    applyTheme();
    loadPlaylists();
    updateCardHeight();

    m_backdropRefreshTimer = new QTimer(this);
    m_backdropRefreshTimer->setSingleShot(true);
    connect(m_backdropRefreshTimer, &QTimer::timeout, this, &AddToPlaylistDialog::refreshBackdrop);

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this]() { applyTheme(); });
}

AddToPlaylistDialog::AddToPlaylistDialog(const QList<MusicInfo> &musics, ApiClient *apiClient,
                                         QWidget *parent)
    : QWidget(parent), m_apiClient(apiClient)
{
    m_musics = musics;
    if (!m_musics.isEmpty())
        m_music = m_musics.first();
    // 批量加入只面向在线歌单（本地歌单走 PlaylistDatabase 的批量插入）
    m_isLocal = m_musics.size() == 1 && m_music.id < 0;
    m_batchTotal = m_musics.size();

    setAttribute(Qt::WA_DeleteOnClose, true);
    setAttribute(Qt::WA_StyledBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    setupUi();
    applyTheme();
    loadPlaylists();
    updateCardHeight();

    m_backdropRefreshTimer = new QTimer(this);
    m_backdropRefreshTimer->setSingleShot(true);
    connect(m_backdropRefreshTimer, &QTimer::timeout, this, &AddToPlaylistDialog::refreshBackdrop);

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this]() { applyTheme(); });
}

void AddToPlaylistDialog::openOn(QWidget *host)
{
    if (!host)
        return;
    m_host = host;
    setParent(host);
    setGeometry(host->rect());
    host->installEventFilter(this);

    if (m_card)
        m_card->setFixedSize(cardWidthForHost(), cardHeightForHost());
    updateCardHeight();
    refreshBackdrop();

    layoutOverlay();
    if (auto *bd = static_cast<PlaylistAddBackdrop *>(m_backdrop))
        bd->scrimOpacity = 0.0;

    show();
    if (m_card)
        m_card->show();
    if (m_backdrop)
        m_backdrop->lower();
    if (m_card)
        m_card->raise();
    setFocus();
    animateOpen();
}

int AddToPlaylistDialog::cardWidthForHost() const
{
    if (!m_host)
        return kCardMinW;
    const int fromHost = m_host->width() * kCardWidthRatioPercent / 100;
    return qBound(kCardMinW, fromHost, kCardMaxW);
}

int AddToPlaylistDialog::cardHeightForHost() const
{
    const int hostH = m_host ? m_host->height() : height();
    if (hostH <= 0)
        return kCardMinH;
    const int fromHost = hostH * kCardHeightRatioPercent / 100;
    const int maxH = qMax(kCardMinH, hostH - 40);
    return qBound(kCardMinH, fromHost, maxH);
}

void AddToPlaylistDialog::refreshBackdrop()
{
    if (!m_host || !m_backdrop)
        return;
    QList<QWidget *> exclude;
    exclude.append(this);
    if (m_card)
        exclude.append(m_card);
    const QPixmap blur = GlassPaint::grabBlurredBackdrop(m_host, exclude, kBlurRadius);
    if (auto *bd = static_cast<PlaylistAddBackdrop *>(m_backdrop)) {
        bd->blurPixmap = blur;
        bd->update();
    }
}

void AddToPlaylistDialog::scheduleBackdropRefresh(int delayMs)
{
    if (!m_backdropRefreshTimer) {
        refreshBackdrop();
        return;
    }
    m_backdropRefreshTimer->start(qMax(0, delayMs));
}

void AddToPlaylistDialog::layoutOverlay()
{
    if (m_backdrop)
        m_backdrop->setGeometry(rect());

    if (!m_card)
        return;

    const int targetW = cardWidthForHost();
    const int targetH = cardHeightForHost();
    if (!m_dismissing) {
        m_card->setFixedSize(targetW, targetH);
    }
    updateCardHeight();

    const int x = qMax(0, (width() - m_card->width()) / 2);
    const int y = qMax(0, (height() - m_card->height()) / 2);
    m_card->move(x, y);
}

void AddToPlaylistDialog::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutOverlay();
}

void AddToPlaylistDialog::finishOpenAnimation()
{
    if (auto *bd = static_cast<PlaylistAddBackdrop *>(m_backdrop))
        bd->scrimOpacity = 1.0;
    if (m_card)
        m_card->setFixedSize(cardWidthForHost(), cardHeightForHost());
    layoutOverlay();
}

void AddToPlaylistDialog::stopAnimations()
{
    if (!m_animGroup)
        return;
    m_animGroup->stop();
    m_animGroup->deleteLater();
    m_animGroup = nullptr;
}

void AddToPlaylistDialog::animateOpen()
{
    stopAnimations();

    const int targetW = cardWidthForHost();
    const int startW = int(targetW * 0.92);
    if (m_card) {
        m_card->setFixedSize(startW, cardHeightForHost());
        m_card->show();
        updateCardHeight();
        layoutOverlay();
    }
    if (auto *bd = static_cast<PlaylistAddBackdrop *>(m_backdrop)) {
        bd->scrimOpacity = 0.0;
        bd->update();
    }

    auto *group = new QParallelAnimationGroup(this);
    m_animGroup = group;

    if (auto *bd = static_cast<PlaylistAddBackdrop *>(m_backdrop)) {
        auto *scrim = new QVariantAnimation(group);
        scrim->setDuration(Theme::kAnimNormal);
        scrim->setStartValue(0.0);
        scrim->setEndValue(1.0);
        scrim->setEasingCurve(QEasingCurve::OutCubic);
        connect(scrim, &QVariantAnimation::valueChanged, bd, [bd](const QVariant &v) {
            bd->scrimOpacity = v.toDouble();
            bd->update();
        });
    }

    if (m_card) {
        auto *cardW = new QVariantAnimation(group);
        cardW->setDuration(Theme::kAnimNormal);
        cardW->setStartValue(startW);
        cardW->setEndValue(targetW);
        cardW->setEasingCurve(QEasingCurve::OutCubic);
        connect(cardW, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            if (!m_card)
                return;
            m_card->setFixedSize(v.toInt(), cardHeightForHost());
            updateCardHeight();
            layoutOverlay();
        });
    }

    connect(group, &QParallelAnimationGroup::finished, this, [this]() {
        m_animGroup = nullptr;
        finishOpenAnimation();
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void AddToPlaylistDialog::animateClose(const std::function<void()> &onFinished)
{
    stopAnimations();

    if (!m_card) {
        if (onFinished)
            onFinished();
        return;
    }

    const int startW = m_card->width();
    const int endW = qMax(kCardMinW - 40, int(startW * 0.94));

    auto *group = new QParallelAnimationGroup(this);
    m_animGroup = group;

    if (auto *bd = static_cast<PlaylistAddBackdrop *>(m_backdrop)) {
        auto *scrim = new QVariantAnimation(group);
        scrim->setDuration(Theme::kAnimNormal);
        scrim->setStartValue(bd->scrimOpacity);
        scrim->setEndValue(0.0);
        scrim->setEasingCurve(QEasingCurve::InCubic);
        connect(scrim, &QVariantAnimation::valueChanged, bd, [bd](const QVariant &v) {
            bd->scrimOpacity = v.toDouble();
            bd->update();
        });
    }

    auto *cardW = new QVariantAnimation(group);
    cardW->setDuration(Theme::kAnimNormal);
    cardW->setStartValue(startW);
    cardW->setEndValue(endW);
    cardW->setEasingCurve(QEasingCurve::InCubic);
    connect(cardW, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        if (!m_card)
            return;
        m_card->setFixedSize(v.toInt(), cardHeightForHost());
        updateCardHeight();
        layoutOverlay();
    });

    connect(group, &QParallelAnimationGroup::finished, this, [this, onFinished]() {
        m_animGroup = nullptr;
        if (onFinished)
            onFinished();
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void AddToPlaylistDialog::dismissAnimated()
{
    if (m_dismissing)
        return;
    m_dismissing = true;
    animateClose([this]() { dismiss(); });
}

void AddToPlaylistDialog::dismiss()
{
    stopAnimations();
    if (m_backdropRefreshTimer)
        m_backdropRefreshTimer->stop();
    if (m_host)
        m_host->removeEventFilter(this);
    emit closed();
    hide();
    deleteLater();
}

bool AddToPlaylistDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host && event->type() == QEvent::Resize && m_host) {
        setGeometry(m_host->rect());
        if (m_card && !m_dismissing)
            m_card->setFixedSize(cardWidthForHost(), cardHeightForHost());
        layoutOverlay();
        scheduleBackdropRefresh();
    }
    return QWidget::eventFilter(watched, event);
}

void AddToPlaylistDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        dismissAnimated();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AddToPlaylistDialog::updateCardHeight()
{
    if (!m_scroll)
        return;

    const int cardH = cardHeightForHost();
    const int scrollH = qMax(240, cardH - kCardChromeH);
    m_scroll->setMinimumHeight(scrollH);
    m_scroll->setMaximumHeight(scrollH);
    m_scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void AddToPlaylistDialog::setupUi()
{
    m_backdrop = new PlaylistAddBackdrop(this);
    static_cast<PlaylistAddBackdrop *>(m_backdrop)->onClicked = [this]() { dismissAnimated(); };

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("playlistAddCard"));
    m_card->setFixedSize(kCardMinW, kCardMinH);
    m_card->setAttribute(Qt::WA_StyledBackground, false);

    m_cardBody = new QWidget(m_card);
    m_cardBody->setObjectName(QStringLiteral("playlistAddCardBody"));
    auto *shadow = new QGraphicsDropShadowEffect(m_cardBody);
    shadow->setBlurRadius(40);
    shadow->setOffset(0, 10);
    shadow->setColor(QColor(0, 0, 0, 90));
    m_cardBody->setGraphicsEffect(shadow);

    auto *cardOuterLay = new QVBoxLayout(m_card);
    cardOuterLay->setContentsMargins(0, 0, 0, 0);
    cardOuterLay->addWidget(m_cardBody);

    auto *cardLay = new QVBoxLayout(m_cardBody);
    cardLay->setContentsMargins(24, 20, 24, 24);
    cardLay->setSpacing(14);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    m_titleLbl = new QLabel(m_cardBody);
    m_titleLbl->setObjectName(QStringLiteral("playlistAddTitle"));
    if (m_batchTotal > 1) {
        m_titleLbl->setText(I18n::instance().tr(QStringLiteral("batchAddToPlaylistTitle"))
                                .arg(m_batchTotal));
    } else {
        m_titleLbl->setText(I18n::instance().tr(m_isLocal ? QStringLiteral("addToLocalPlaylist")
                                                          : QStringLiteral("addToPlaylist")));
    }
    header->addWidget(m_titleLbl, 1);

    m_closeBtn = new QPushButton(QStringLiteral("×"), m_cardBody);
    m_closeBtn->setObjectName(QStringLiteral("playlistAddClose"));
    m_closeBtn->setFixedSize(32, 32);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setFlat(true);
    connect(m_closeBtn, &QPushButton::clicked, this, &AddToPlaylistDialog::dismissAnimated);
    header->addWidget(m_closeBtn);
    cardLay->addLayout(header);

    m_scroll = new QScrollArea(m_cardBody);
    m_scroll->setObjectName(QStringLiteral("playlistAddScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    m_listHost = new QWidget(m_scroll);
    m_listHost->setObjectName(QStringLiteral("playlistAddList"));
    m_listLay = new QVBoxLayout(m_listHost);
    m_listLay->setContentsMargins(0, 0, 2, 0);
    m_listLay->setSpacing(2);
    m_listLay->setAlignment(Qt::AlignTop);

    m_emptyLbl = new QLabel(m_listHost);
    m_emptyLbl->setAlignment(Qt::AlignCenter);
    m_emptyLbl->setWordWrap(true);
    m_emptyLbl->hide();

    m_scroll->setWidget(m_listHost);
    nekoPolishScrollAreaViewport(m_scroll);
    cardLay->addWidget(m_scroll, 1);

    layoutOverlay();
}

void AddToPlaylistDialog::applyTheme()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString titleFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#182B45");
    const QString metaFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.55)");
    const QString emptyFg = metaFg;

    if (m_cardBody) {
        if (dark) {
            m_cardBody->setStyleSheet(QStringLiteral(
                "QWidget#playlistAddCardBody {"
                "  background: rgb(36, 36, 36);"
                "  border: 1px solid rgba(255, 255, 255, 0.08);"
                "  border-radius: 14px;"
                "}"
                "QPushButton#playlistAddClose {"
                "  color: #c8c8c8; font-size: 22px; border: none; border-radius: 8px;"
                "  background: transparent;"
                "}"
                "QPushButton#playlistAddClose:hover { background: rgba(255,255,255,0.1); }"));
        } else {
            m_cardBody->setStyleSheet(QStringLiteral(
                "QWidget#playlistAddCardBody {"
                "  background: #ffffff;"
                "  border: 1px solid rgba(0, 0, 0, 0.08);"
                "  border-radius: 14px;"
                "}"
                "QPushButton#playlistAddClose {"
                "  color: #666; font-size: 22px; border: none; border-radius: 8px;"
                "  background: transparent;"
                "}"
                "QPushButton#playlistAddClose:hover { background: rgba(0,0,0,0.06); }"));
        }
    }
    if (m_titleLbl) {
        m_titleLbl->setStyleSheet(
            QStringLiteral("QLabel#playlistAddTitle { font-size: 20px; font-weight: 700; color: %1; }")
                .arg(titleFg));
    }
    if (m_emptyLbl) {
        m_emptyLbl->setStyleSheet(
            QStringLiteral("QLabel { font-size: 14px; color: %1; padding: 40px 12px; }").arg(emptyFg));
    }

    for (auto it = m_rowsByPlaylistId.constBegin(); it != m_rowsByPlaylistId.constEnd(); ++it) {
        auto *row = static_cast<PlaylistAddRowWidget *>(it.value());
        if (!row)
            continue;
        if (row->titleLabel) {
            row->titleLabel->setStyleSheet(
                QStringLiteral("QLabel { font-size: 15px; font-weight: 600; color: %1; }").arg(titleFg));
        }
        if (row->metaLabel) {
            row->metaLabel->setStyleSheet(
                QStringLiteral("QLabel { font-size: 13px; color: %1; }").arg(metaFg));
        }
    }
}

void AddToPlaylistDialog::clearList()
{
    m_rowsByPlaylistId.clear();
    while (QLayoutItem *it = m_listLay->takeAt(0)) {
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }
    if (m_emptyLbl)
        m_emptyLbl->hide();
}

QWidget *AddToPlaylistDialog::appendCreateRow()
{
    auto *row = new PlaylistAddRowWidget(m_listHost);
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(10, 4, 12, 4);
    lay->setSpacing(12);

    auto *iconBox = new QLabel(row);
    iconBox->setFixedSize(kCoverSize, kCoverSize);
    iconBox->setAlignment(Qt::AlignCenter);
    QPixmap pix(kCoverSize, kCoverSize);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    p.setBrush(dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 12));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(0, 0, kCoverSize, kCoverSize, kRowRadius, kRowRadius);
    p.drawPixmap(15, 15, Icons::renderNamed("Add", 20, QColor(255, 255, 255, 200)));
    iconBox->setPixmap(pix);
    lay->addWidget(iconBox);

    auto *textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(0);
    row->titleLabel = new QLabel(I18n::instance().tr(QStringLiteral("createNewPlaylistRow")), row);
    textCol->addWidget(row->titleLabel);
    textCol->addStretch(1);
    lay->addLayout(textCol, 1);

    row->onActivated = [this]() { openCreatePlaylist(); };
    m_listLay->addWidget(row);
    return row;
}

QWidget *AddToPlaylistDialog::appendPlaylistRow(const PlaylistRow &pl)
{
    auto *row = new PlaylistAddRowWidget(m_listHost);
    row->playlistId = pl.id;

    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(10, 4, 12, 4);
    lay->setSpacing(12);

    row->coverLabel = new QLabel(row);
    row->coverLabel->setFixedSize(kCoverSize, kCoverSize);
    row->setPlaceholderCover();
    lay->addWidget(row->coverLabel);

    auto *textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 6, 0, 6);
    textCol->setSpacing(2);

    row->titleLabel = new QLabel(pl.name, row);
    textCol->addWidget(row->titleLabel);

    row->metaLabel = new QLabel(I18n::instance().tr(QStringLiteral("playlistSongCount")).arg(pl.musicCount), row);
    textCol->addWidget(row->metaLabel);
    textCol->addStretch(1);
    lay->addLayout(textCol, 1);

    const int playlistId = pl.id;
    row->onActivated = [this, playlistId]() { addToPlaylist(playlistId); };

    if (!pl.coverUrl.isEmpty())
        bindCover(row, pl.coverUrl);

    m_listLay->addWidget(row);
    m_rowsByPlaylistId.insert(pl.id, row);
    return row;
}

void AddToPlaylistDialog::bindCover(QWidget *rowWidget, const QString &coverUrl)
{
    auto *row = static_cast<PlaylistAddRowWidget *>(rowWidget);
    if (!row || !row->coverLabel || coverUrl.isEmpty())
        return;

    const QString resolved = CoverCache::resolveCoverUrl(coverUrl);
    const QString cacheKey = cacheKeyFromCoverUrl(resolved);
    if (cacheKey.isEmpty())
        return;

    QPixmap cached = CoverCache::instance()->get(cacheKey);
    if (!cached.isNull()) {
        row->applyCoverPixmap(cached);
        return;
    }

    QObject::disconnect(row->coverConn);
    QPointer<PlaylistAddRowWidget> guard(row);
    row->coverConn = QObject::connect(
        CoverCache::instance(), &CoverCache::coverLoaded, row,
        [guard, cacheKey](const QString &id, const QPixmap &pix) {
            if (guard && id == cacheKey)
                guard->applyCoverPixmap(pix);
        });
    CoverCache::instance()->fetchCover(cacheKey, resolved);
}

void AddToPlaylistDialog::loadPlaylists()
{
    clearList();
    appendCreateRow();

    if (m_isLocal)
        loadLocalPlaylists();
    else
        loadOnlinePlaylists();
}

void AddToPlaylistDialog::loadLocalPlaylists()
{
    const auto playlists = PlaylistDatabase::instance().getAllPlaylists();
    if (playlists.isEmpty()) {
        m_emptyLbl->setText(I18n::instance().tr(QStringLiteral("noPlaylists")));
        m_listLay->addWidget(m_emptyLbl);
        m_emptyLbl->show();
        return;
    }

    QList<PlaylistRow> rows;
    for (const auto &pl : playlists) {
        PlaylistRow row;
        row.id = pl.localId;
        row.name = pl.name;
        row.musicCount = PlaylistDatabase::instance().getPlaylistMusicCount(pl.localId);
        const MusicInfo first = PlaylistDatabase::instance().getPlaylistFirstMusic(pl.localId);
        if (first.id != 0) {
            if (!first.coverUrl.isEmpty())
                row.coverUrl = CoverCache::resolveCoverUrl(first.coverUrl);
            else if (first.id > 0)
                row.coverUrl = coverUrlForMusicId(first.id);
            else if (pl.coverMusicId > 0)
                row.coverUrl = coverUrlForMusicId(pl.coverMusicId);
        } else if (pl.coverMusicId > 0) {
            row.coverUrl = coverUrlForMusicId(pl.coverMusicId);
        }
        rows.append(row);
        appendPlaylistRow(row);
    }
    applyTheme();
    updateCardHeight();
    layoutOverlay();
    Q_UNUSED(rows);
}

void AddToPlaylistDialog::loadOnlinePlaylists()
{
    if (!UserManager::instance().isLoggedIn()) {
        m_emptyLbl->setText(I18n::instance().tr(QStringLiteral("goToLogin")));
        m_listLay->addWidget(m_emptyLbl);
        m_emptyLbl->show();
        return;
    }
    if (!m_apiClient) {
        m_emptyLbl->setText(I18n::instance().tr(QStringLiteral("noPlaylists")));
        m_listLay->addWidget(m_emptyLbl);
        m_emptyLbl->show();
        return;
    }

    m_apiClient->fetchUserPlaylists([this](bool success, const QList<QVariantMap> &playlists) {
        if (!success || playlists.isEmpty()) {
            m_emptyLbl->setText(I18n::instance().tr(QStringLiteral("noPlaylists")));
            m_listLay->addWidget(m_emptyLbl);
            m_emptyLbl->show();
            return;
        }

        for (const auto &pl : playlists) {
            PlaylistRow row;
            row.id = pl.value(QStringLiteral("id")).toInt();
            row.name = pl.value(QStringLiteral("name")).toString();
            row.musicCount = pl.value(QStringLiteral("musicCount")).toInt();
            row.coverUrl = playlistCoverUrlFromMap(pl);
            if (row.id <= 0 || row.name.isEmpty())
                continue;
            appendPlaylistRow(row);
        }
        applyTheme();
        updateCardHeight();
        layoutOverlay();
    });
}

void AddToPlaylistDialog::openCreatePlaylist()
{
    QWidget *dlgParent = m_host ? m_host : window();
    LineInputDialog dlg(dlgParent,
                        I18n::instance().tr(QStringLiteral("createPlaylist")),
                        I18n::instance().tr(QStringLiteral("playlistName")),
                        I18n::instance().tr(QStringLiteral("playlistNamePlaceholder")),
                        QString(),
                        I18n::instance().tr(QStringLiteral("create")),
                        false);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString name = dlg.value();
    if (name.isEmpty())
        return;

    if (m_isLocal) {
        const int localId = PlaylistDatabase::instance().createPlaylist(name);
        if (localId > 0)
            addToPlaylist(localId);
        return;
    }

    if (!m_apiClient || !UserManager::instance().isLoggedIn()) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("goToLogin")), Toast::Error);
        return;
    }

    m_adding = true;
    m_apiClient->createPlaylist(name, QString(), [this](bool success, const QString &message, const QVariantMap &playlist) {
        if (!success) {
            m_adding = false;
            Toast::show(this,
                        message.isEmpty() ? I18n::instance().tr(QStringLiteral("createPlaylistFailed"))
                                          : message,
                        Toast::Error);
            return;
        }
        const int newId = playlist.value(QStringLiteral("id")).toInt();
        if (newId <= 0) {
            m_adding = false;
            Toast::show(this, I18n::instance().tr(QStringLiteral("createPlaylistFailed")), Toast::Error);
            return;
        }
        emit playlistsChanged();
        addToPlaylist(newId);
    });
}

void AddToPlaylistDialog::addToPlaylist(int playlistId)
{
    if (playlistId <= 0 || m_adding)
        return;

    if (m_isLocal) {
        if (PlaylistDatabase::instance().addMusic(playlistId, m_music)) {
            Toast::show(this, I18n::instance().tr(QStringLiteral("addToPlaylistSuccess")), Toast::Success);
            emit playlistsChanged();
            dismissAnimated();
        } else {
            Toast::show(this, I18n::instance().tr(QStringLiteral("musicAddFailed")), Toast::Error);
        }
        return;
    }

    if (!m_apiClient || !UserManager::instance().isLoggedIn()) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("goToLogin")), Toast::Error);
        return;
    }

    // 批量加入：接口一次只吃一首，逐首串行调用，全部结束后统一报结果
    if (m_batchTotal > 1) {
        addBatchToOnlinePlaylist(playlistId);
        return;
    }

    if (m_music.id <= 0) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("addToPlaylistFailed")), Toast::Error);
        return;
    }

    m_adding = true;
    Toast::show(this, I18n::instance().tr(QStringLiteral("addingToPlaylist")), Toast::Info);

    m_apiClient->addMusicToPlaylist(playlistId, m_music.id, [this](bool success, const QString &message) {
        m_adding = false;
        if (success) {
            Toast::show(this, I18n::instance().tr(QStringLiteral("addToPlaylistSuccess")), Toast::Success);
            emit playlistsChanged();
            dismissAnimated();
            return;
        }
        const QString msg =
            message.isEmpty() ? I18n::instance().tr(QStringLiteral("addToPlaylistFailed")) : message;
        Toast::show(this, msg, Toast::Error);
    });
}

void AddToPlaylistDialog::addBatchToOnlinePlaylist(int playlistId)
{
    QList<MusicInfo> pending;
    pending.reserve(m_musics.size());
    for (const MusicInfo &info : m_musics) {
        if (info.id > 0)
            pending.append(info);
    }

    const int total = pending.size();
    if (total <= 0) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("addToPlaylistFailed")), Toast::Error);
        return;
    }

    m_adding = true;
    m_batchTotal = total;
    m_batchDone = 0;
    m_batchSuccess = 0;
    m_batchFailed = 0;
    showBatchProgress(0, total);

    // 逐首串行：一次只发一个请求，回调里再发下一首。
    // QPointer 保住浮层在请求返回前被关掉（ESC / 点遮罩）的场合。
    auto step = std::make_shared<std::function<void(int)>>();
    QPointer<AddToPlaylistDialog> guard(this);
    *step = [this, guard, playlistId, pending, total, step](int index) {
        if (!guard)
            return;
        if (index >= total) {
            m_adding = false;
            emit playlistsChanged();
            reportBatchResult(m_batchSuccess, m_batchFailed);
            return;
        }

        const int musicId = pending.at(index).id;
        m_apiClient->addMusicToPlaylist(
            playlistId, musicId, [this, guard, index, total, step](bool success, const QString &) {
                if (!guard)
                    return;
                if (success)
                    ++m_batchSuccess;
                else
                    ++m_batchFailed;
                m_batchDone = index + 1;
                showBatchProgress(m_batchDone, total);
                (*step)(index + 1);
            });
    };
    (*step)(0);
}

void AddToPlaylistDialog::showBatchProgress(int done, int total)
{
    if (done <= 0) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("batchAddingToPlaylist")).arg(total),
                    Toast::Info);
        return;
    }
    Toast::show(this,
                I18n::instance().tr(QStringLiteral("batchAddProgress")).arg(done).arg(total),
                Toast::Info, 1500);
}

void AddToPlaylistDialog::reportBatchResult(int success, int failed)
{
    if (failed <= 0) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("batchDone")).arg(success), Toast::Success);
    } else if (success <= 0) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("batchFailed")).arg(failed), Toast::Error);
    } else {
        Toast::show(this,
                    I18n::instance().tr(QStringLiteral("batchPartialDone")).arg(success).arg(failed),
                    Toast::Info);
    }
    dismissAnimated();
}
