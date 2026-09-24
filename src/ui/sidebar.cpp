/**
 * @file sidebar.cpp
 * @brief 侧边栏实现
 *
 * 240px 侧栏，SPlayer 式扁平 surface + 顶栏 Logo。
 * 选中态：主色半透明圆角底。
 */

#include "sidebar.h"
#include "core/shellbackdropsettings.h"
#include "svgicon.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/glasspaint.h"
#include "ui/scrollareafix.h"
#include "ui/playlistlistitem.h"
#include "core/covercache.h"
#include "core/i18n.h"
#include "core/usermanager.h"
#include "core/apiclient.h"
#include "ui/lineinputdialog.h"

#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QTimer>

namespace {

constexpr int kPlaylistCoverRequestConcurrency = 2;

QColor navIconNormalColor()
{
    return Theme::ThemeManager::instance().isDarkMode() ? QColor(245, 240, 255, 180)
                                                        : QColor(33, 37, 41, 190);
}

QColor navIconActiveColor()
{
    return Theme::ThemeManager::instance().isDarkMode() ? QColor(255, 143, 163)
                                                        : QColor(230, 57, 80);
}

const char *navSvgName(const QString &key)
{
    if (key == QStringLiteral("home"))
        return "Home";
    if (key == QStringLiteral("favorites"))
        return "Favorite";
    if (key == QStringLiteral("recent"))
        return "History";
    if (key == QStringLiteral("downloads"))
        return "Download";
    if (key == QStringLiteral("settings"))
        return "Settings";
    if (key == QStringLiteral("music"))
        return "Music";
    return "Home";
}

QIcon navIcon(const QString &key, bool active)
{
    const char *name = navSvgName(key);
    return Icons::iconNamed(name, 20, active ? navIconActiveColor() : navIconNormalColor(),
                            navIconActiveColor());
}

QString coverUrlForMusicId(int musicId)
{
    if (musicId <= 0)
        return {};
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

QString coverUrlFromMusicList(const QList<QVariantMap> &musicList)
{
    if (musicList.isEmpty())
        return {};
    const QVariantMap first = musicList.first();
    const QString rawCover = first.value(QStringLiteral("coverUrl")).toString().trimmed();
    if (!rawCover.isEmpty())
        return CoverCache::resolveCoverUrl(rawCover);
    return coverUrlForMusicId(first.value(QStringLiteral("id")).toInt());
}

} // namespace

Sidebar::Sidebar(ApiClient *apiClient, QWidget *parent) : QWidget(parent), m_apiClient(apiClient)
{
    setFixedWidth(Theme::kSidebarW);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setupUi();
    setActiveNav("home");
    loadPlaylists();
}

void Sidebar::setupUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *logoRow = new QWidget(this);
    logoRow->setObjectName("sbLogoRow");
    logoRow->setFixedHeight(64);
    auto *logoLay = new QHBoxLayout(logoRow);
    logoLay->setContentsMargins(18, 0, 14, 0);
    logoLay->setSpacing(12);

    auto *logoImg = new QLabel(logoRow);
    logoImg->setFixedSize(32, 32);
    logoImg->setPixmap(QPixmap(QStringLiteral(":/icons/app.png"))
                           .scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLay->addWidget(logoImg);

    auto *logoText = new QLabel(QStringLiteral("NekoMusic"), logoRow);
    logoText->setObjectName("sbLogoText");
    logoLay->addWidget(logoText);
    logoLay->addStretch();
    outer->addWidget(logoRow);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName("sbScroll");

    auto *container = new QWidget(scroll);
    container->setObjectName("sbContainer");
    auto *lay = new QVBoxLayout(container);
    lay->setContentsMargins(8, 10, 8, 14);
    lay->setSpacing(4);

    // 主导航（带 PNG 图标）
    lay->addWidget(createNavItem("home", I18n::instance().tr("home"), navIcon("home", true)));

    // 我喜欢的（可点击导航）
    m_favBtn = createNavItem("favorites", I18n::instance().tr("favorites"), navIcon("favorites", false));
    lay->addWidget(m_favBtn);

    // 最近播放（可点击导航）
    m_recBtn = createNavItem("recent", I18n::instance().tr("recentPlay"), navIcon("recent", false));
    lay->addWidget(m_recBtn);

    // 下载管理（可点击导航）
    m_downloadBtn = createNavItem("downloads", I18n::instance().tr("downloadManage"), navIcon("downloads", false));
    lay->addWidget(m_downloadBtn);

    // 分隔线
    auto *div = new QWidget(container);
    div->setObjectName("sbDivider");
    div->setFixedHeight(1);
    lay->addWidget(div);

    // 歌单区域标题
    auto *plHeader = new QLabel(I18n::instance().tr("myPlaylistsTitle"), container);
    plHeader->setObjectName("sbPlaylistTitle");
    lay->addWidget(plHeader);

    // 播放列表容器
    m_playlistContainer = new QWidget(container);
    m_playlistContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_playlistContainer->setAutoFillBackground(false);
    m_playlistLayout = new QVBoxLayout(m_playlistContainer);
    m_playlistLayout->setContentsMargins(2, 0, 2, 0);
    m_playlistLayout->setSpacing(3);
    m_playlistLayout->setAlignment(Qt::AlignTop);
    lay->addWidget(m_playlistContainer);

    // 创建歌单按钮
    m_createPlaylistBtn = new QPushButton(I18n::instance().tr("createPlaylist"), container);
    m_createPlaylistBtn->setObjectName("sbCreatePlaylist");
    m_createPlaylistBtn->setFixedHeight(36);
    m_createPlaylistBtn->setCursor(Qt::PointingHandCursor);
    connect(m_createPlaylistBtn, &QPushButton::clicked, this, [this]() {
        emit playlistCreateRequested();
    });
    lay->addWidget(m_createPlaylistBtn);
    
    // 导入网易云歌单按钮
    m_importNeteaseBtn = new QPushButton(I18n::instance().tr("importNeteasePlaylist"), container);
    m_importNeteaseBtn->setObjectName("sbCreatePlaylist");
    m_importNeteaseBtn->setFixedHeight(36);
    m_importNeteaseBtn->setCursor(Qt::PointingHandCursor);
    m_importNeteaseBtn->setToolTip(I18n::instance().tr("importNeteaseDesc"));
    connect(m_importNeteaseBtn, &QPushButton::clicked, this, [this]() {
        emit neteaseImportRequested();
    });
    lay->addWidget(m_importNeteaseBtn);

    m_importQqBtn = new QPushButton(I18n::instance().tr("importQqPlaylist"), container);
    m_importQqBtn->setObjectName("sbCreatePlaylist");
    m_importQqBtn->setFixedHeight(36);
    m_importQqBtn->setCursor(Qt::PointingHandCursor);
    m_importQqBtn->setToolTip(I18n::instance().tr("importQqDesc"));
    connect(m_importQqBtn, &QPushButton::clicked, this, [this]() {
        emit qqImportRequested();
    });
    lay->addWidget(m_importQqBtn);

    m_importKugouBtn = new QPushButton(I18n::instance().tr("importKugouPlaylist"), container);
    m_importKugouBtn->setObjectName("sbCreatePlaylist");
    m_importKugouBtn->setFixedHeight(36);
    m_importKugouBtn->setCursor(Qt::PointingHandCursor);
    m_importKugouBtn->setToolTip(I18n::instance().tr("importKugouDesc"));
    connect(m_importKugouBtn, &QPushButton::clicked, this, [this]() {
        emit kugouImportRequested();
    });
    lay->addWidget(m_importKugouBtn);

    m_importQishuiBtn = new QPushButton(I18n::instance().tr("importQishuiPlaylist"), container);
    m_importQishuiBtn->setObjectName("sbCreatePlaylist");
    m_importQishuiBtn->setFixedHeight(36);
    m_importQishuiBtn->setCursor(Qt::PointingHandCursor);
    m_importQishuiBtn->setToolTip(I18n::instance().tr("importQishuiDesc"));
    connect(m_importQishuiBtn, &QPushButton::clicked, this, [this]() {
        emit qishuiImportRequested();
    });
    lay->addWidget(m_importQishuiBtn);

    // 收藏歌单分隔线
    auto *favDiv = new QWidget(container);
    favDiv->setObjectName("sbDivider");
    favDiv->setFixedHeight(1);
    lay->addWidget(favDiv);

    // 收藏歌单标题
    auto *favHeader = new QLabel(I18n::instance().tr("favoritePlaylistsTitle"), container);
    favHeader->setObjectName("sbPlaylistTitle");
    lay->addWidget(favHeader);

    // 收藏歌单容器
    m_favPlaylistContainer = new QWidget(container);
    m_favPlaylistContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_favPlaylistContainer->setAutoFillBackground(false);
    m_favPlaylistLayout = new QVBoxLayout(m_favPlaylistContainer);
    m_favPlaylistLayout->setContentsMargins(2, 0, 2, 0);
    m_favPlaylistLayout->setSpacing(3);
    m_favPlaylistLayout->setAlignment(Qt::AlignTop);
    lay->addWidget(m_favPlaylistContainer);

    lay->addStretch();

    scroll->setWidget(container);
    nekoPolishScrollAreaViewport(scroll);

    outer->addWidget(scroll, 1);

    m_playlistRefreshTimer = new QTimer(this);
    m_playlistRefreshTimer->setSingleShot(true);
    m_playlistRefreshTimer->setInterval(90);
    connect(m_playlistRefreshTimer, &QTimer::timeout, this, &Sidebar::refreshPlaylistList);

    m_favPlaylistRefreshTimer = new QTimer(this);
    m_favPlaylistRefreshTimer->setSingleShot(true);
    m_favPlaylistRefreshTimer->setInterval(90);
    connect(m_favPlaylistRefreshTimer, &QTimer::timeout, this, &Sidebar::refreshFavPlaylistList);
}

void Sidebar::refreshPlaylists()
{
    loadPlaylists();
}

void Sidebar::loadPlaylists()
{
    if (!m_apiClient || !UserManager::instance().isLoggedIn()) {
        // Not logged in or no API client, clear the list
        ++m_playlistCoverGeneration;
        ++m_favPlaylistCoverGeneration;
        m_pendingPlaylistCoverIds.clear();
        m_pendingFavPlaylistCoverIds.clear();
        m_activePlaylistCoverRequests = 0;
        m_activeFavPlaylistCoverRequests = 0;
        m_apiPlaylists.clear();
        m_favPlaylists.clear();
        refreshPlaylistList();
        refreshFavPlaylistList();
        return;
    }

    m_apiClient->fetchUserPlaylists([this](bool success, const QList<QVariantMap> &playlists) {
        if (success) {
            m_apiPlaylists.clear();
            for (const auto &pl : playlists) {
                ApiPlaylistInfo info;
                info.id = pl.value("id").toInt();
                info.name = pl.value("name").toString();
                info.description = pl.value("description").toString();
                info.musicCount = pl.value("musicCount").toInt();
                info.coverUrl = playlistCoverUrlFromMap(pl);
                m_apiPlaylists.append(info);
            }
            qDebug() << "[歌单] 共加载" << m_apiPlaylists.size() << "个歌单";
        } else {
            m_apiPlaylists.clear();
            qDebug() << "[歌单] 加载失败";
        }
        refreshPlaylistList();
        enqueueMissingPlaylistCovers();
        loadFavPlaylists();
    });
}

void Sidebar::schedulePlaylistListRefresh()
{
    if (!m_playlistRefreshTimer) {
        refreshPlaylistList();
        return;
    }
    m_playlistRefreshTimer->start();
}

void Sidebar::enqueueMissingPlaylistCovers()
{
    ++m_playlistCoverGeneration;
    m_pendingPlaylistCoverIds.clear();
    m_activePlaylistCoverRequests = 0;
    for (const auto &pl : m_apiPlaylists) {
        if (pl.coverUrl.isEmpty() && pl.musicCount > 0)
            m_pendingPlaylistCoverIds.append(pl.id);
    }
    pumpPlaylistCoverRequests();
}

void Sidebar::pumpPlaylistCoverRequests()
{
    if (!m_apiClient)
        return;
    const int gen = m_playlistCoverGeneration;
    while (m_activePlaylistCoverRequests < kPlaylistCoverRequestConcurrency
           && !m_pendingPlaylistCoverIds.isEmpty()) {
        const int playlistId = m_pendingPlaylistCoverIds.takeFirst();
        ++m_activePlaylistCoverRequests;
        m_apiClient->fetchPlaylistMusic(
            playlistId,
            [this, gen, playlistId](bool ok, int, const QList<QVariantMap> &musicList) {
                if (gen != m_playlistCoverGeneration)
                    return;
                m_activePlaylistCoverRequests = qMax(0, m_activePlaylistCoverRequests - 1);
                const QString coverUrl = ok ? coverUrlFromMusicList(musicList) : QString();
                if (!coverUrl.isEmpty()) {
                    for (auto &info : m_apiPlaylists) {
                        if (info.id == playlistId) {
                            info.coverUrl = coverUrl;
                            break;
                        }
                    }
                    schedulePlaylistListRefresh();
                }
                pumpPlaylistCoverRequests();
            });
    }
}

void Sidebar::refreshPlaylistList()
{
    // 清空布局内所有控件（含「暂无歌单」占位 QLabel，旧逻辑未加入 m_playlistItems 会残留叠层）
    while (QLayoutItem *it = m_playlistLayout->takeAt(0)) {
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }
    m_playlistItems.clear();

    if (m_apiPlaylists.isEmpty()) {
        auto *empty = new QLabel(I18n::instance().tr("noPlaylists"), m_playlistContainer);
        empty->setObjectName("sbEmptyPlaylist");
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        m_playlistLayout->addWidget(empty);
    } else {
        for (const auto &pl : m_apiPlaylists) {
            auto *item = new PlaylistListItem(pl.id, pl.name, pl.musicCount, pl.coverUrl, PlaylistListItem::UserPlaylist, m_playlistContainer);
            connect(item, &PlaylistListItem::clicked, this, [this, playlistId = pl.id]() {
                emit playlistClicked(playlistId);
            });
            connect(item, &PlaylistListItem::renameRequested, this, [this, playlistId = pl.id]() {
                if (!m_apiClient) return;
                // 找到当前歌单信息
                for (const auto &pl : m_apiPlaylists) {
                    if (pl.id == playlistId) {
                        LineInputDialog dlg(this,
                                            I18n::instance().tr(QStringLiteral("renamePlaylist")),
                                            I18n::instance().tr(QStringLiteral("inputNewPlaylistName")),
                                            QString(),
                                            pl.name,
                                            I18n::instance().tr(QStringLiteral("save")),
                                            false);
                        if (dlg.exec() != QDialog::Accepted)
                            break;
                        const QString newName = dlg.value();
                        if (!newName.isEmpty() && newName != pl.name) {
                            // 调用API更新歌单名称
                            m_apiClient->updatePlaylist(playlistId, newName, pl.description, [this, playlistId, newName](bool success, const QString &, const QVariantMap &) {
                                if (success) {
                                    // 更新缓存
                                    for (auto &p : m_apiPlaylists) {
                                        if (p.id == playlistId) {
                                            p.name = newName;
                                            break;
                                        }
                                    }
                                    refreshPlaylistList();
                                }
                            });
                        }
                        break;
                    }
                }
            });
            connect(item, &PlaylistListItem::editDescriptionRequested, this, [this, playlistId = pl.id]() {
                if (!m_apiClient) return;
                // 找到当前歌单信息
                for (const auto &pl : m_apiPlaylists) {
                    if (pl.id == playlistId) {
                        LineInputDialog dlg(this,
                                            I18n::instance().tr(QStringLiteral("modifyPlaylistDesc")),
                                            I18n::instance().tr(QStringLiteral("inputPlaylistDesc")),
                                            QString(),
                                            pl.description,
                                            I18n::instance().tr(QStringLiteral("save")),
                                            true);
                        if (dlg.exec() != QDialog::Accepted)
                            break;
                        const QString newDesc = dlg.value();
                        if (newDesc != pl.description) {
                            // 调用API更新歌单描述
                            m_apiClient->updatePlaylist(playlistId, pl.name, newDesc, [this, playlistId, newDesc](bool success, const QString &, const QVariantMap &) {
                                if (success) {
                                    // 更新缓存
                                    for (auto &p : m_apiPlaylists) {
                                        if (p.id == playlistId) {
                                            p.description = newDesc;
                                            break;
                                        }
                                    }
                                    refreshPlaylistList();
                                }
                            });
                        }
                        break;
                    }
                }
            });
            connect(item, &PlaylistListItem::deleteRequested, this, [this, playlistId = pl.id]() {
                if (!m_apiClient) return;
                m_apiClient->deletePlaylist(playlistId, [this, playlistId](bool success, const QString &) {
                    if (success) {
                        // 从缓存中移除
                        for (int i = 0; i < m_apiPlaylists.size(); ++i) {
                            if (m_apiPlaylists[i].id == playlistId) {
                                m_apiPlaylists.removeAt(i);
                                break;
                            }
                        }
                        refreshPlaylistList();
                    }
                });
            });
            m_playlistLayout->addWidget(item);
            m_playlistItems.append(item);
        }
    }
    m_playlistLayout->addStretch(1);
}

void Sidebar::loadFavPlaylists()
{
    if (!m_apiClient || !UserManager::instance().isLoggedIn()) {
        ++m_favPlaylistCoverGeneration;
        m_pendingFavPlaylistCoverIds.clear();
        m_activeFavPlaylistCoverRequests = 0;
        m_favPlaylists.clear();
        refreshFavPlaylistList();
        return;
    }

    m_apiClient->fetchFavoritePlaylists([this](bool success, const QList<QVariantMap> &playlists) {
        if (success) {
            m_favPlaylists.clear();
            for (const auto &pl : playlists) {
                ApiPlaylistInfo info;
                info.id = pl.value("id").toInt();
                info.name = pl.value("name").toString();
                info.description = pl.value("description").toString();
                info.musicCount = pl.value("musicCount").toInt();
                info.coverUrl = playlistCoverUrlFromMap(pl);
                m_favPlaylists.append(info);
            }
        } else {
            m_favPlaylists.clear();
        }
        refreshFavPlaylistList();
        enqueueMissingFavPlaylistCovers();
    });
}

void Sidebar::scheduleFavPlaylistListRefresh()
{
    if (!m_favPlaylistRefreshTimer) {
        refreshFavPlaylistList();
        return;
    }
    m_favPlaylistRefreshTimer->start();
}

void Sidebar::enqueueMissingFavPlaylistCovers()
{
    ++m_favPlaylistCoverGeneration;
    m_pendingFavPlaylistCoverIds.clear();
    m_activeFavPlaylistCoverRequests = 0;
    for (const auto &pl : m_favPlaylists) {
        if (pl.coverUrl.isEmpty() && pl.musicCount > 0)
            m_pendingFavPlaylistCoverIds.append(pl.id);
    }
    pumpFavPlaylistCoverRequests();
}

void Sidebar::pumpFavPlaylistCoverRequests()
{
    if (!m_apiClient)
        return;
    const int gen = m_favPlaylistCoverGeneration;
    while (m_activeFavPlaylistCoverRequests < kPlaylistCoverRequestConcurrency
           && !m_pendingFavPlaylistCoverIds.isEmpty()) {
        const int playlistId = m_pendingFavPlaylistCoverIds.takeFirst();
        ++m_activeFavPlaylistCoverRequests;
        m_apiClient->fetchPlaylistMusic(
            playlistId,
            [this, gen, playlistId](bool ok, int, const QList<QVariantMap> &musicList) {
                if (gen != m_favPlaylistCoverGeneration)
                    return;
                m_activeFavPlaylistCoverRequests = qMax(0, m_activeFavPlaylistCoverRequests - 1);
                const QString coverUrl = ok ? coverUrlFromMusicList(musicList) : QString();
                if (!coverUrl.isEmpty()) {
                    for (auto &info : m_favPlaylists) {
                        if (info.id == playlistId) {
                            info.coverUrl = coverUrl;
                            break;
                        }
                    }
                    scheduleFavPlaylistListRefresh();
                }
                pumpFavPlaylistCoverRequests();
            });
    }
}

void Sidebar::refreshFavPlaylistList()
{
    while (QLayoutItem *it = m_favPlaylistLayout->takeAt(0)) {
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }
    m_favPlaylistItems.clear();

    if (m_favPlaylists.isEmpty()) {
        auto *empty = new QLabel(I18n::instance().tr("noPlaylists"), m_favPlaylistContainer);
        empty->setObjectName("sbEmptyPlaylist");
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        m_favPlaylistLayout->addWidget(empty);
    } else {
        for (const auto &pl : m_favPlaylists) {
            auto *item = new PlaylistListItem(pl.id, pl.name, pl.musicCount, pl.coverUrl, PlaylistListItem::FavoritePlaylist, m_favPlaylistContainer);
            connect(item, &PlaylistListItem::clicked, this, [this, playlistId = pl.id]() { emit playlistClicked(playlistId); });
            connect(item, &PlaylistListItem::unfavoriteRequested, this, [this, playlistId = pl.id]() {
                if (!m_apiClient) return;
                m_apiClient->unfavoritePlaylist(playlistId, [this, playlistId](bool success, const QString &) {
                    if (success) {
                        // 从缓存中移除
                        for (int i = 0; i < m_favPlaylists.size(); ++i) {
                            if (m_favPlaylists[i].id == playlistId) {
                                m_favPlaylists.removeAt(i);
                                break;
                            }
                        }
                        refreshFavPlaylistList();
                    }
                });
            });
            m_favPlaylistLayout->addWidget(item);
            m_favPlaylistItems.append(item);
        }
    }
    m_favPlaylistLayout->addStretch(1);
}

QPushButton *Sidebar::createNavItem(const QString &key, const QString &label, const QIcon &icon)
{
    auto *btn = new QPushButton(label, this);
    btn->setObjectName("sbNavItem");
    btn->setFixedHeight(38);
    btn->setIcon(icon);
    btn->setIconSize(QSize(18, 18));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("navKey", key);
    connect(btn, &QPushButton::clicked, this, [this, key]() {
        setActiveNav(key);
        emit navigationRequested(key);
    });
    m_navBtns[key] = btn;
    return btn;
}

void Sidebar::setActiveNav(const QString &key)
{
    m_activeKey = key;
    for (auto it = m_navBtns.constBegin(); it != m_navBtns.constEnd(); ++it) {
        bool active = (it.key() == key);
        it.value()->setProperty("active", active);
        it.value()->style()->unpolish(it.value());
        it.value()->style()->polish(it.value());
        // 更新图标
        it.value()->setIcon(navIcon(it.key(), active));
    }
}

void Sidebar::retranslate()
{
    auto *homeBtn = m_navBtns.value("home");
    if (homeBtn) homeBtn->setText(I18n::instance().tr("home"));

    if (m_favBtn) m_favBtn->setText(I18n::instance().tr("favorites"));
    if (m_recBtn) m_recBtn->setText(I18n::instance().tr("recentPlay"));
    if (m_downloadBtn) m_downloadBtn->setText(I18n::instance().tr("downloadManage"));

    auto headers = findChildren<QLabel *>("sbPlaylistTitle");
    if (headers.size() >= 1) headers[0]->setText(I18n::instance().tr("myPlaylistsTitle"));
    if (headers.size() >= 2) headers[1]->setText(I18n::instance().tr("favoritePlaylistsTitle"));

    if (m_createPlaylistBtn) m_createPlaylistBtn->setText(I18n::instance().tr("createPlaylist"));
    if (m_importNeteaseBtn) {
        m_importNeteaseBtn->setText(I18n::instance().tr("importNeteasePlaylist"));
        m_importNeteaseBtn->setToolTip(I18n::instance().tr("importNeteaseDesc"));
    }
    if (m_importQqBtn) {
        m_importQqBtn->setText(I18n::instance().tr("importQqPlaylist"));
        m_importQqBtn->setToolTip(I18n::instance().tr("importQqDesc"));
    }
    if (m_importKugouBtn) {
        m_importKugouBtn->setText(I18n::instance().tr("importKugouPlaylist"));
        m_importKugouBtn->setToolTip(I18n::instance().tr("importKugouDesc"));
    }
    if (m_importQishuiBtn) {
        m_importQishuiBtn->setText(I18n::instance().tr("importQishuiPlaylist"));
        m_importQishuiBtn->setToolTip(I18n::instance().tr("importQishuiDesc"));
    }
}

void Sidebar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    GlassPaint::paintBarGlass(p, rect(), GlassPaint::BarKind::Sidebar,
                              Theme::ThemeManager::instance().isDarkMode(),
                              ShellBackdropSettings::instance().usesImageBackdrop());
}
