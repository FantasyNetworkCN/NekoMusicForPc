/**
 * @file recentpage.cpp
 * @brief 最近播放 — 1:1 SPlayer History.vue + SongList + SongCard
 */

#include "recentpage.h"
#include "songlistwidget.h"
#include "batchactionbar.h"
#include "core/i18n.h"
#include "core/playlistdb.h"
#include "core/playlistmanager.h"
#include "core/musicdownloadmanager.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"
#include "ui/toast.h"
#include "ui/headeractionmenu.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QShowEvent>

RecentPage::RecentPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("recentPage"));
    setupUi();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) { applyPageStyle(); });

    connect(&PlaylistManager::instance(), &PlaylistManager::playlistChanged, this,
            &RecentPage::updatePlayingHighlight);
    connect(&PlaylistManager::instance(), &PlaylistManager::currentIndexChanged, this,
            &RecentPage::updatePlayingHighlight);
}

void RecentPage::setupUi()
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
    titleLay->setSpacing(8);
    titleLay->setAlignment(Qt::AlignBottom);

    m_titleLbl = new QLabel(I18n::instance().tr(QStringLiteral("recentPlay")), titleRow);
    titleLay->addWidget(m_titleLbl);

    m_countLbl = new QLabel(titleRow);
    titleLay->addWidget(m_countLbl);
    titleLay->addStretch();
    headerLay->addWidget(titleRow);

    auto *menuRow = new QWidget(m_header);
    menuRow->setFixedHeight(40);
    m_menuRow = menuRow;
    auto *menuLay = new QHBoxLayout(menuRow);
    menuLay->setContentsMargins(0, 0, 0, 0);
    menuLay->setSpacing(12);

    m_playBtn = new QPushButton(I18n::instance().tr(QStringLiteral("play")), menuRow);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setFixedHeight(40);
    connect(m_playBtn, &QPushButton::clicked, this, [this]() {
        if (!m_allRecent.isEmpty())
            emit playAllRequested(m_allRecent);
    });
    menuLay->addWidget(m_playBtn);

    m_moreBtn = new QPushButton(menuRow);
    m_moreBtn->setFixedSize(40, 40);
    m_moreBtn->setCursor(Qt::PointingHandCursor);
    m_moreBtn->setFlat(true);
    m_moreBtn->setToolTip(I18n::instance().tr(QStringLiteral("moreActions")));
    connect(m_moreBtn, &QPushButton::clicked, this, &RecentPage::showMoreMenu);
    menuLay->addWidget(m_moreBtn);

    menuLay->addStretch();
    headerLay->addWidget(menuRow);
    root->addWidget(m_header);

    m_songList = new SongListWidget(this);
    m_songList->onSongActivate = [this](const MusicInfo &info) { emit playRequested(info); };
    m_songList->onSongPlayNext = [this](const MusicInfo &info) { emit playRequested(info); };
    m_songList->onUnfavorite = [this](int id) { emit favoriteRequested(id); };
    // 该页的“收藏”是切换语义：统一菜单里作为「添加/取消收藏」双向入口复用
    m_songList->onToggleFavorite = [this](const MusicInfo &info) { emit favoriteRequested(info.id); };
    m_songList->isFavorited = [this](int id) { return m_favoritedIds.contains(id); };
    m_songList->onTogglePlayPause = [this]() { emit playPauseRequested(); };
    m_songList->onDownload = [this](const MusicInfo &info) { emit downloadRequested(info); };
    m_songList->isDownloaded = [](int id) { return MusicDownloadManager::instance().isDownloaded(id); };
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
    m_statusLabel->hide();
    emptyLay->addWidget(m_statusLabel);
    m_emptyWrap->hide();
    root->addWidget(m_emptyWrap);

    setupBatchBar();

    applyPageStyle();
    loadRecentPlays();
}

void RecentPage::showMoreMenu()
{
    const bool hasSongs = !m_allRecent.isEmpty();
    HeaderActionMenu menu(this);
    QAction *downloadAll = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("downloadAll")), "Download", hasSongs);
    QAction *addAll = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("addAllToQueue")), "MusicList", hasSongs);
    menu.addSeparator();
    QAction *batchManage = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("batchManage")), "Batch", hasSongs);
    QAction *clearRecent = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("clearRecentList")), "Delete", hasSongs);
    menu.addSeparator();
    QAction *refreshAction = menu.addHeaderAction(
        I18n::instance().tr(QStringLiteral("refresh")), "Refresh");

    QAction *picked = menu.exec(m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
    if (picked == downloadAll)
        emit downloadAllRequested(m_allRecent);
    else if (picked == addAll)
        addAllToQueue();
    else if (picked == batchManage)
        enterBatchMode();
    else if (picked == clearRecent)
        confirmClearRecent();
    else if (picked == refreshAction)
        refresh();
}

/** 批量编辑模式：顶部按钮排 ↔ 批量操作条 的公共接线 */
void RecentPage::setupBatchBar()
{
    if (!m_menuRow || !m_songList)
        return;

    m_batchBar = new BatchActionBar(m_menuRow->parentWidget());
    m_batchBar->setCapabilities(true, true, true, true, false);
    m_batchBar->hide();
    if (auto *lay = qobject_cast<QBoxLayout *>(m_menuRow->parentWidget()->layout()))
        lay->insertWidget(lay->indexOf(m_menuRow), m_batchBar);

    connect(m_batchBar, &BatchActionBar::exitRequested, this, &RecentPage::exitBatchMode);
    connect(m_batchBar, &BatchActionBar::selectAllToggled, this, [this]() {
        if (m_songList)
            m_songList->setBatchSelectAll(!m_songList->batchAllSelected());
    });
    connect(m_batchBar, &BatchActionBar::addToPlaylistRequested, this, [this]() {
        const QList<MusicInfo> picked = m_songList ? m_songList->batchSelectedSongs() : QList<MusicInfo>();
        if (picked.isEmpty())
            return;
        emit batchAddToPlaylistRequested(picked);
        exitBatchMode(); // 动作已派发：立刻退出并清空选择，避免重复点重复请求
    });
    connect(m_batchBar, &BatchActionBar::downloadRequested, this, [this]() {
        const QList<MusicInfo> picked = m_songList ? m_songList->batchSelectedSongs() : QList<MusicInfo>();
        if (picked.isEmpty())
            return;
        emit batchDownloadRequested(picked);
        exitBatchMode();
    });
    // 最近播放页的「收藏」是切换语义，批量动作名义上是取消收藏，实际逐首走主窗口的开关
    connect(m_batchBar, &BatchActionBar::unfavoriteRequested, this, [this]() {
        QList<int> ids;
        if (m_songList) {
            const QList<MusicInfo> picked = m_songList->batchSelectedSongs();
            for (const MusicInfo &info : picked)
                ids.append(info.id);
        }
        if (ids.isEmpty())
            return;
        emit batchFavoriteToggleRequested(ids);
        exitBatchMode();
    });
    connect(m_batchBar, &BatchActionBar::addToQueueRequested, this, [this]() {
        if (!m_songList)
            return;
        const QList<MusicInfo> picked = m_songList->batchSelectedSongs();
        if (picked.isEmpty())
            return;
        PlaylistManager::instance().addAllToPlaylist(picked);
        Toast::show(this, I18n::instance().tr(QStringLiteral("batchQueueAdded")).arg(picked.size()),
                    Toast::Success);
        exitBatchMode();
    });
    connect(m_songList, &SongListWidget::batchSelectionChanged, this,
            &RecentPage::onBatchSelectionChanged);
}

void RecentPage::enterBatchMode()
{
    if (!m_songList || !m_batchBar || m_allRecent.isEmpty())
        return;

    m_songList->clearBatchSelection();
    m_songList->setBatchMode(true);
    m_batchBar->reset();
    m_menuRow->hide();
    m_batchBar->show();
}

void RecentPage::exitBatchMode()
{
    if (!m_songList || !m_batchBar)
        return;

    m_songList->setBatchMode(false);
    m_batchBar->reset();
    m_batchBar->hide();
    if (m_menuRow)
        m_menuRow->show();
}

void RecentPage::onBatchSelectionChanged(int count)
{
    if (!m_batchBar || !m_songList)
        return;
    m_batchBar->setSelectedCount(count);
    m_batchBar->setAllSelected(m_songList->batchAllSelected());
}

/** 一键把当前列表的全部曲目加入本地播放队列 */
void RecentPage::addAllToQueue()
{
    if (m_allRecent.isEmpty()) {
        Toast::show(this, I18n::instance().tr(QStringLiteral("queueAddAllEmpty")), Toast::Info);
        return;
    }

    auto &mgr = PlaylistManager::instance();
    const int before = mgr.count();
    mgr.addAllToPlaylist(m_allRecent);
    const int added = qMax(0, mgr.count() - before);

    if (added <= 0)
        Toast::show(this, I18n::instance().tr(QStringLiteral("queueAddAllNone")), Toast::Info);
    else
        Toast::show(this, I18n::instance().tr(QStringLiteral("addAllToQueueDone")).arg(added),
                    Toast::Success);
}

void RecentPage::applyPageStyle()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString titleFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QString metaFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.72)");
    const QString statusFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.55)");

    if (m_titleLbl) {
        m_titleLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 30px; font-weight: 700; color: %1; }").arg(titleFg));
    }
    if (m_countLbl) {
        m_countLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 15px; font-weight: 400; color: %1; padding-bottom: 2px; }")
                                      .arg(metaFg));
    }
    if (m_playBtn) {
        m_playBtn->setIcon(Icons::renderNamed("Play", 18, QColor(255, 255, 255)));
        m_playBtn->setIconSize(QSize(18, 18));
        m_playBtn->setStyleSheet(QStringLiteral(
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
    if (m_moreBtn) {
        const QColor moreIc = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
        m_moreBtn->setIcon(Icons::renderNamed("More", 20, moreIc));
        m_moreBtn->setIconSize(QSize(20, 20));
        m_moreBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: none; border-radius: 20px; }"
            "QPushButton:hover { background: rgba(230,57,80,0.15); }").arg(secondaryBg));
    }
    if (m_batchBar)
        m_batchBar->applyTheme();

    if (m_statusLabel) {
        m_statusLabel->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 14px; color: %1; padding: 60px 24px; }").arg(statusFg));
    }

    if (m_songList)
        m_songList->applyTheme();
}

void RecentPage::retranslate()
{
    if (m_titleLbl)
        m_titleLbl->setText(I18n::instance().tr(QStringLiteral("recentPlay")));
    if (m_playBtn)
        m_playBtn->setText(I18n::instance().tr(QStringLiteral("play")));
    if (m_moreBtn)
        m_moreBtn->setToolTip(I18n::instance().tr(QStringLiteral("moreActions")));
    if (m_batchBar)
        m_batchBar->retranslate();
    if (m_songList)
        m_songList->retranslate();
    updateHeaderMeta();
    applyPageStyle();
}

void RecentPage::refresh()
{
    loadRecentPlays();
}

void RecentPage::setPlaybackPaused(bool paused)
{
    if (m_songList)
        m_songList->setPlaybackPaused(paused);
}

void RecentPage::setFavoritedMusicIds(const QSet<int> &ids)
{
    m_favoritedIds = ids;
    if (m_songList)
        m_songList->refreshFavoriteDisplay();
}

void RecentPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updatePlayingHighlight();
}

void RecentPage::updateHeaderMeta()
{
    if (m_countLbl) {
        m_countLbl->setText(I18n::instance().tr(QStringLiteral("favoritePageSongCount"))
                                .arg(m_allRecent.size()));
    }
    const bool hasSongs = !m_allRecent.isEmpty();
    if (m_playBtn)
        m_playBtn->setEnabled(hasSongs);

    // 列表空了就没有可批量的对象：入口置灰，已经进去了就退出来
    const bool batchActive = m_songList && m_songList->batchMode();
    if (!hasSongs && batchActive)
        exitBatchMode();
    // 批量模式下按钮排已让位给批量操作条
    else if (m_menuRow && batchActive)
        m_menuRow->hide();
}

void RecentPage::refreshDownloadDisplay()
{
    if (m_songList)
        m_songList->refreshDownloadDisplay();
}

int RecentPage::currentPlayingMusicId() const
{
    const auto &mgr = PlaylistManager::instance();
    const int idx = mgr.currentIndex();
    if (idx < 0 || idx >= mgr.playlist().size())
        return -1;
    return mgr.playlist()[idx].id;
}

void RecentPage::updatePlayingHighlight()
{
    if (m_songList)
        m_songList->setCurrentPlayingId(currentPlayingMusicId());
}

void RecentPage::showPageStatus(const QString &text, const char *iconName)
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

void RecentPage::hidePageStatus()
{
    if (m_emptyIcon)
        m_emptyIcon->hide();
    if (m_statusLabel)
        m_statusLabel->hide();
    if (m_emptyWrap)
        m_emptyWrap->hide();
}

void RecentPage::confirmClearRecent()
{
    if (m_allRecent.isEmpty())
        return;

    const auto ret = QMessageBox::question(
        this, I18n::instance().tr(QStringLiteral("clearRecentList")),
        I18n::instance().tr(QStringLiteral("clearRecentConfirm")),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    PlaylistDatabase::instance().clearRecentPlays();
    Toast::show(this, I18n::instance().tr(QStringLiteral("clearRecentSuccess")), Toast::Success);
    loadRecentPlays();
}

void RecentPage::loadRecentPlays()
{
    m_allRecent = PlaylistDatabase::instance().getRecentPlays();
    updateHeaderMeta();

    if (m_allRecent.isEmpty()) {
        if (m_songList)
            m_songList->setSongs({});
        showPageStatus(I18n::instance().tr(QStringLiteral("emptyRecent")), "SearchOff");
        return;
    }

    hidePageStatus();
    if (m_songList) {
        m_songList->setSongs(m_allRecent);
        m_songList->refreshFavoriteDisplay();
        m_songList->refreshDownloadDisplay();
        m_songList->show();
    }
    updatePlayingHighlight();
}

void RecentPage::paintEvent(QPaintEvent *)
{
}
