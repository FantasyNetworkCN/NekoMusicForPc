#include "ui/playlistpanel.h"
#include "ui/batchactionbar.h"
#include "ui/songcontextmenu.h"
#include "ui/songmenubuilder.h"
#include "ui/toast.h"
#include "core/playlistmanager.h"
#include "core/landevicemanager.h"
#include "core/musicdownloadmanager.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/scrollareafix.h"
#include "ui/svgicon.h"
#include "ui/localmusicbadgelabel.h"

#include <QSizePolicy>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsDropShadowEffect>
#include <QScrollBar>
#include <QFrame>
#include <QFontMetrics>
#include <QResizeEvent>
#include <QComboBox>
#include <QSignalBlocker>
#include <functional>

namespace {

constexpr int kPlaylistRowHeight = 64;
constexpr int kPlaylistRowSpacing = 16;
constexpr int kPlaylistRowStride = kPlaylistRowHeight + kPlaylistRowSpacing;
constexpr int kListPad = 16;

// 列表区右侧留白：抽屉贴住主窗口右缘，滚动条若不左移会整个落在无边框窗口
// 的缩放/拖动手感区（8px）内，拖滚动条会被系统当成拖窗口边缘。
constexpr int kScrollbarEdgeInset = 10;

constexpr QColor kPrimary(22, 119, 184);

/** 复选框圆角方块里的对勾（自己画，避免依赖字体里是否有 ✓） */
QPixmap checkMarkPixmap(int size, const QColor &color)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, qMax(2.0, size / 7.0));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    QPainterPath path;
    path.moveTo(size * 0.22, size * 0.52);
    path.lineTo(size * 0.42, size * 0.72);
    path.lineTo(size * 0.78, size * 0.30);
    p.drawPath(path);
    p.end();
    return pm;
}

} // namespace

// ─── 播放队列项（对齐 SPlayer SongPlayList .song-node）────────────────
class PlaylistItemCard : public QWidget {
public:
    explicit PlaylistItemCard(const MusicInfo &info, int index, bool isCurrent, QWidget *parent = nullptr)
        : QWidget(parent), m_info(info), m_musicId(info.id), m_index(index), m_isCurrent(isCurrent)
    {
        setObjectName(QStringLiteral("PlaylistItemCard"));
        setFixedHeight(kPlaylistRowHeight);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, false);

        const bool dark = Theme::ThemeManager::instance().isDarkMode();

        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(8, 8, 8, 8);
        lay->setSpacing(8);

        // 序号列：编号标签与批量复选框同位置互斥（与 SongCardWidget 的约定一致）
        auto *indexCol = new QWidget(this);
        indexCol->setFixedWidth(36);
        m_indexCol = indexCol;

        m_indexLbl = new QLabel(indexCol);
        m_indexLbl->setGeometry(0, 0, 36, 48);
        m_indexLbl->setAlignment(Qt::AlignCenter);
        m_indexLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        applyIndexLabelStyle(dark);
        updateIndexDisplay();

        m_batchBox = new QPushButton(indexCol);
        m_batchBox->setGeometry(0, 0, 36, 48);
        m_batchBox->setFlat(true);
        m_batchBox->setCursor(Qt::PointingHandCursor);
        m_batchBox->hide();
        connect(m_batchBox, &QPushButton::clicked, this, [this](bool) {
            if (batchToggleRequested)
                batchToggleRequested(m_info);
        });

        lay->addWidget(indexCol);

        auto *infoV = new QWidget(this);
        infoV->setAttribute(Qt::WA_TranslucentBackground);
        auto *infoLay = new QVBoxLayout(infoV);
        infoLay->setContentsMargins(0, 0, 0, 0);
        infoLay->setSpacing(2);

        auto *titleRow = new QWidget(infoV);
        auto *titleRowLay = new QHBoxLayout(titleRow);
        titleRowLay->setContentsMargins(0, 0, 0, 0);
        titleRowLay->setSpacing(6);

        m_localBadge = new QLabel(titleRow);
        m_localBadge->setVisible(false);
        titleRowLay->addWidget(m_localBadge, 0, Qt::AlignVCenter);

        m_titleLbl = new QLabel(info.title, titleRow);
        m_titleLbl->setMinimumWidth(0);
        m_titleLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        applyTitleStyle(dark);
        titleRowLay->addWidget(m_titleLbl, 1, Qt::AlignVCenter);

        infoLay->addWidget(titleRow);

        m_artistLbl = new QLabel(info.artist, infoV);
        applyArtistStyle(dark);
        infoLay->addWidget(m_artistLbl);

        lay->addWidget(infoV, 1);

        // 行右侧三点菜单：与右键共用统一菜单（SongMenuBuilder）
        m_moreBtn = new QPushButton(this);
        m_moreBtn->setFixedSize(32, 32);
        m_moreBtn->setCursor(Qt::PointingHandCursor);
        m_moreBtn->setFlat(true);
        applyMoreBtnStyle(dark);
        connect(m_moreBtn, &QPushButton::clicked, this, [this](bool) {
            if (m_batchMode)
                return;
            if (menuRequested)
                menuRequested(m_info, m_moreBtn->mapToGlobal(QPoint(0, m_moreBtn->height())));
        });
        lay->addWidget(m_moreBtn);

        m_removeBtn = new QPushButton(this);
        m_removeBtn->setFixedSize(32, 32);
        m_removeBtn->setCursor(Qt::PointingHandCursor);
        m_removeBtn->setFlat(true);
        applyRemoveBtnStyle(dark);
        connect(m_removeBtn, &QPushButton::clicked, this, [this](bool) {
            if (m_batchMode)
                return;
            if (!m_entryId.isEmpty() && entryRemoveRequested)
                entryRemoveRequested(m_entryId);
            else if (removeRequested)
                removeRequested(m_musicId);
        });
        lay->addWidget(m_removeBtn);

        updateLocalBadge(dark);
        elideTexts();
        // 构造末尾统一按「非批量、非只读」定一次行内按钮状态，保证初始态明确
        applyRowButtonState();
        setReadOnly(false);
    }

    std::function<void(int)> onClicked;
    std::function<void(int)> removeRequested;
    std::function<void(const QString &)> entryRemoveRequested;
    std::function<void(const MusicInfo &, const QPoint &)> menuRequested;
    std::function<void(const MusicInfo &)> batchToggleRequested;

    const MusicInfo &info() const { return m_info; }

    void bind(const MusicInfo &info, int index, bool isCurrent, const QString &entryId = QString())
    {
        m_info = info;
        m_musicId = info.id;
        m_entryId = entryId;
        m_index = index;
        m_titleLbl->setText(info.title);
        m_artistLbl->setText(info.artist);
        updateCurrentState(isCurrent);
        updateLocalBadge(Theme::ThemeManager::instance().isDarkMode());
        elideTexts();
    }

    void updateCurrentState(bool isCurrent)
    {
        m_isCurrent = isCurrent;
        const bool dark = Theme::ThemeManager::instance().isDarkMode();
        applyIndexLabelStyle(dark);
        updateIndexDisplay();
        applyTitleStyle(dark);
        applyArtistStyle(dark);
        // 走 applyRowButtonState() 而不是单独调样式函数，
        // 这样当前行切换时 ⋮ / 🗑 的 enabled 也会跟着批量模式同步
        applyRowButtonState();
        updateBatchBox();
        updateLocalBadge(dark);
        update();
    }

    void setReadOnly(bool readOnly)
    {
        m_readOnly = readOnly;
        m_removeBtn->setVisible(!readOnly);
        m_moreBtn->setVisible(!readOnly);
        setCursor(readOnly ? Qt::ArrowCursor : Qt::PointingHandCursor);
    }

    void setMenuVisible(bool visible)
    {
        if (m_moreBtn)
            m_moreBtn->setVisible(visible);
    }

    /** 批量编辑模式：序号列换复选框，整行点击 = 切换选中，不触发播放/移除/菜单 */
    void setBatchMode(bool enabled)
    {
        if (m_batchMode == enabled)
            return;
        m_batchMode = enabled;
        m_batchPressRow = false;
        m_batchBox->setVisible(enabled);
        if (m_indexLbl)
            m_indexLbl->setVisible(!enabled);
        // 行内 ⋮ / 🗑 在批量模式下点击会被吞掉，所以置灰而不是装成能点：
        // 与 SongCardWidget 里三点按钮的 setEnabled(!enabled) 行为对齐
        applyRowButtonState();
        setCursor(enabled ? Qt::ArrowCursor : Qt::PointingHandCursor);
        updateBatchBox();
        update();
    }

    void setBatchChecked(bool checked)
    {
        if (m_batchChecked == checked)
            return;
        m_batchChecked = checked;
        updateBatchBox();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QPainterPath path;
        path.addRoundedRect(rect(), 8, 8);

        if (m_batchMode && m_batchChecked) {
            p.fillPath(path, QColor(kPrimary.red(), kPrimary.green(), kPrimary.blue(), 74));
            p.strokePath(path, QPen(kPrimary, 1.0));
        } else if (m_isCurrent) {
            p.fillPath(path, QColor(kPrimary.red(), kPrimary.green(), kPrimary.blue(), 74));
            p.strokePath(path, QPen(kPrimary, 1.0));
        } else if (m_hover && !m_batchMode) {
            p.fillPath(path, QColor(kPrimary.red(), kPrimary.green(), kPrimary.blue(), 20));
            p.strokePath(path, QPen(kPrimary, 1.0));
        } else {
            p.fillPath(path, QColor(kPrimary.red(), kPrimary.green(), kPrimary.blue(), 20));
        }
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hover = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hover = false;
        update();
        QWidget::leaveEvent(event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        elideTexts();
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            if (m_batchMode) {
                // 批量模式：整行点击只切换选中，不播放
                m_batchPressRow = true;
                e->accept();
                return;
            }
            if (onClicked)
                onClicked(m_musicId);
        }
        QWidget::mousePressEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (m_batchMode && e->button() == Qt::LeftButton && m_batchPressRow) {
            m_batchPressRow = false;
            if (batchToggleRequested)
                batchToggleRequested(m_info);
            e->accept();
            return;
        }
        QWidget::mouseReleaseEvent(e);
    }

private:
    int textColumnWidth() const
    {
        // 右侧两个 32px 按钮 + 序号列 36px + 内外间距
        int w = qMax(40, width() - 36 - 32 - 32 - 8 * 5);
        if (m_localBadge && m_localBadge->isVisible())
            w = qMax(40, w - m_localBadge->sizeHint().width() - 6);
        return w;
    }

    void updateLocalBadge(bool dark)
    {
        updateLocalMusicBadge(m_localBadge, m_info.isLocalFile(), dark);
    }

    void elideTexts()
    {
        const QFontMetrics titleFm(m_titleLbl->font());
        const QFontMetrics artistFm(m_artistLbl->font());
        const int w = textColumnWidth();
        m_titleLbl->setText(titleFm.elidedText(m_info.title, Qt::ElideRight, w));
        m_artistLbl->setText(artistFm.elidedText(m_info.artist, Qt::ElideRight, w));
    }

    void applyIndexLabelStyle(bool dark)
    {
        const QString c = dark ? QString::fromUtf8(Theme::kTextMuted) : QStringLiteral("rgba(33,37,41,0.45)");
        m_indexLbl->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 12px; }").arg(c));
    }

    void applyTitleStyle(bool dark)
    {
        const QString col = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#182B45");
        m_titleLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 14px; font-weight: 500; color: %1; }").arg(col));
    }

    void applyArtistStyle(bool dark)
    {
        const QString col = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.62)");
        m_artistLbl->setStyleSheet(QStringLiteral("QLabel { font-size: 12px; color: %1; }").arg(col));
    }

    void applyRemoveBtnStyle(bool dark)
    {
        const bool on = !m_batchMode;
        const QColor ic = dark ? QColor(244, 246, 255, on ? 140 : 70)
                               : QColor(33, 37, 41, on ? 140 : 70);
        m_removeBtn->setIcon(Icons::renderNamed("Delete", 20, ic));
        m_removeBtn->setIconSize(QSize(20, 20));
        m_removeBtn->setStyleSheet(rowButtonStyle());
    }

    void applyMoreBtnStyle(bool dark)
    {
        const bool on = !m_batchMode;
        const QColor ic = dark ? QColor(244, 246, 255, on ? 165 : 80)
                               : QColor(33, 37, 41, on ? 140 : 80);
        m_moreBtn->setIcon(Icons::renderNamed("More", 20, ic));
        m_moreBtn->setIconSize(QSize(20, 20));
        m_moreBtn->setStyleSheet(rowButtonStyle());
    }

    /** 批量模式下把 ⋮ / 🗑 一起置灰（图标调淡 + 禁用 + 箭头光标） */
    void applyRowButtonState()
    {
        const bool enabled = !m_batchMode;
        const bool dark = Theme::ThemeManager::instance().isDarkMode();
        for (QPushButton *btn : {m_moreBtn, m_removeBtn}) {
            if (!btn)
                continue;
            btn->setEnabled(enabled);
            btn->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
        }
        applyMoreBtnStyle(dark);
        applyRemoveBtnStyle(dark);
    }

    static QString rowButtonStyle()
    {
        return QStringLiteral(
            "QPushButton { background: transparent; border: none; border-radius: 8px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.08); }");
    }

    void updateBatchBox()
    {
        if (!m_batchBox)
            return;

        const bool dark = Theme::ThemeManager::instance().isDarkMode();
        constexpr int kBoxSize = 20;
        constexpr int kIconSize = 16;
        QPixmap pm(kBoxSize, kBoxSize);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing, true);
            if (m_batchChecked) {
                p.setPen(Qt::NoPen);
                p.setBrush(kPrimary);
                p.drawRoundedRect(QRectF(0.5, 0.5, kBoxSize - 1.0, kBoxSize - 1.0), 5, 5);
            } else {
                p.setPen(QPen(dark ? QColor(244, 246, 255, 150) : QColor(33, 37, 41, 130), 1.6));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(QRectF(0.8, 0.8, kBoxSize - 1.6, kBoxSize - 1.6), 5, 5);
            }
        }
        if (m_batchChecked) {
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.drawPixmap((kBoxSize - kIconSize) / 2, (kBoxSize - kIconSize) / 2,
                         checkMarkPixmap(kIconSize, Qt::white));
        }

        m_batchBox->setIcon(QIcon(pm));
        m_batchBox->setIconSize(QSize(kBoxSize, kBoxSize));
        m_batchBox->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: none; }"));
        m_batchBox->setToolTip(I18n::instance().tr(QStringLiteral("batchRowHint")));
    }

    void updateIndexDisplay()
    {
        if (m_isCurrent) {
            const QColor ac = kPrimary;
            m_indexLbl->setPixmap(Icons::renderNamed("Music", 20, ac));
            m_indexLbl->setText(QString());
        } else {
            m_indexLbl->setPixmap(QPixmap());
            m_indexLbl->setText(QString::number(m_index + 1));
        }
    }

    MusicInfo m_info;
    int m_musicId;
    QString m_entryId;
    int m_index;
    bool m_isCurrent = false;
    bool m_hover = false;
    bool m_readOnly = false;
    bool m_batchMode = false;
    bool m_batchChecked = false;
    bool m_batchPressRow = false;
    QWidget *m_indexCol = nullptr;
    QLabel *m_indexLbl = nullptr;
    QPushButton *m_batchBox = nullptr;
    QLabel *m_localBadge = nullptr;
    QLabel *m_titleLbl = nullptr;
    QLabel *m_artistLbl = nullptr;
    QPushButton *m_moreBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
};

void releasePlaylistCard(PlaylistItemCard *card, QList<QWidget *> &pool)
{
    if (!card)
        return;
    card->hide();
    card->onClicked = nullptr;
    card->removeRequested = nullptr;
    card->entryRemoveRequested = nullptr;
    card->menuRequested = nullptr;
    card->batchToggleRequested = nullptr;
    card->setBatchChecked(false);
    pool.append(card);
}

PlaylistItemCard *acquirePlaylistCard(QWidget *container, QList<QWidget *> &pool)
{
    if (!pool.isEmpty())
        return static_cast<PlaylistItemCard *>(pool.takeLast());
    return new PlaylistItemCard(MusicInfo{}, 0, false, container);
}

// ─── PlaylistPanel ──────────────────────────────────────

PlaylistPanel::PlaylistPanel(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kDrawerWidth);
    hide();

    m_drawerShadow = new QGraphicsDropShadowEffect(this);
    m_drawerShadow->setBlurRadius(28);
    m_drawerShadow->setOffset(-6, 0);
    m_drawerShadow->setColor(QColor(0, 0, 0, 100));
    setGraphicsEffect(m_drawerShadow);

    setupUi();
    applyPanelChrome();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this](Theme::ThemeMode) {
                applyPanelChrome();
                refresh();
            });

    connect(&PlaylistManager::instance(), &PlaylistManager::playlistChanged, this, &PlaylistPanel::refresh);
    connect(&LanDeviceManager::instance(), &LanDeviceManager::devicesChanged,
            this, &PlaylistPanel::refreshDevices);
    connect(&LanDeviceManager::instance(), &LanDeviceManager::remoteQueueChanged,
            this, &PlaylistPanel::refreshRemoteQueue);

    m_lastPlaylistSize = PlaylistManager::instance().count();
    m_displayPlaylist = PlaylistManager::instance().playlist();
    m_displayCurrentIndex = PlaylistManager::instance().currentIndex();
    refreshDevices();
    refresh();
}

/** 收藏缓存变化后刷新行状态（菜单里的收藏项文案/图标按它决定） */
void PlaylistPanel::refreshFavoriteDisplay()
{
    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it) {
        auto *card = static_cast<PlaylistItemCard *>(it.value());
        if (card)
            card->updateCurrentState(it.key() == m_displayCurrentIndex);
    }
}

/** 把某一行绑定到卡片（含批量模式状态回灌） */
void PlaylistPanel::bindRowCard(PlaylistItemCard *card, int row)
{
    if (!card || row < 0 || row >= m_displayPlaylist.size())
        return;

    const MusicInfo &info = m_displayPlaylist[row];
    const QString entryId =
        m_togetherView && row < m_togetherEntryIds.size() ? m_togetherEntryIds.at(row) : QString();
    card->bind(info, row, row == m_displayCurrentIndex, entryId);
    card->setReadOnly(m_remoteView);
    card->setMenuVisible(!m_togetherView);

    // 远端设备的队列是只读视图：批量模式与移除/菜单都不开放
    card->setBatchMode(m_batchMode && !m_remoteView && !m_togetherView);

    const int musicId = info.id;
    card->onClicked = [this, musicId](int) {
        if (m_batchMode)
            return; // 批量模式下整行点击只用于选中，不触发播放
        emit playRequested(musicId);
    };
    card->removeRequested = m_remoteView ? std::function<void(int)>() :
        [](int localId) { PlaylistManager::instance().removeFromPlaylist(localId); };
    card->entryRemoveRequested = m_togetherView
        ? [this](const QString &entryId) { emit togetherRemoveRequested(entryId); }
        : std::function<void(const QString &)>();
    card->menuRequested = [this](const MusicInfo &song, const QPoint &pos) {
        showSongMenu(song, pos);
    };
    card->batchToggleRequested = [this](const MusicInfo &song) {
        if (!m_batchMode)
            return;
        if (m_batchSelectedIds.contains(song.id))
            m_batchSelectedIds.remove(song.id);
        else
            m_batchSelectedIds.insert(song.id);
        syncBatchToVisibleRows();
        refreshBatchBar();
    };

    // 行被卡片池复用后选中态必须按 music_id 重新回灌，否则滚动会把选中态带错行
    if (m_batchMode || m_rowBatchChecked.contains(row)) {
        const bool checked = m_batchMode && m_batchSelectedIds.contains(info.id);
        if (checked)
            m_rowBatchChecked.insert(row);
        else
            m_rowBatchChecked.remove(row);
        card->setBatchChecked(checked);
    }

    const int w = qMax(40, m_listContainer->width() - 2 * kListPad);
    card->setParent(m_listContainer);
    card->setFixedSize(w, kPlaylistRowHeight);
    card->move(kListPad, kListPad + row * kPlaylistRowStride);
    card->show();
    m_rowCards[row] = card;
}

/** 单曲统一菜单（与其它列表页同一套 SongMenuBuilder） */
void PlaylistPanel::showSongMenu(const MusicInfo &info, const QPoint &globalPos)
{
    if (m_remoteView)
        return;
    if (info.id <= 0 && info.localPath.isEmpty())
        return;

    SongMenuBuilder::State state;
    // 队列里的曲目必然“已在播放队列”，所以这一项固定显示「从播放队列移除」
    state.inPlayQueue = true;
    state.favorited = isFavorited ? isFavorited(info.id) : false;
    state.downloaded = MusicDownloadManager::instance().isDownloaded(info.id);
    state.canQueue = info.id > 0;

    SongMenuBuilder::Handlers handlers;
    handlers.playNext = [info]() { SongMenuBuilder::playNextNow(info); };
    if (onToggleFavorite)
        handlers.toggleFavorite = [this, info]() { onToggleFavorite(info); };
    handlers.toggleQueue = [info]() {
        // PlaylistManager::removeFromPlaylist 内部按 music id 匹配（见 findIndexByLocalId）
        PlaylistManager::instance().removeFromPlaylist(info.id);
    };
    if (onDownload)
        handlers.download = [this, info]() { onDownload(info); };
    state.canFavorite = static_cast<bool>(handlers.toggleFavorite);
    state.canDownload = static_cast<bool>(handlers.download);

    const QList<SongContextMenuPopup::Entry> entries =
        SongMenuBuilder::buildStandard(info, state, handlers);
    if (entries.isEmpty())
        return;

    SongContextMenuPopup::showAt(window() ? window() : this, globalPos, entries);
}

/** 当前队列里已选中的曲目（按队列顺序返回） */
QList<MusicInfo> PlaylistPanel::batchSelectedSongs() const
{
    QList<MusicInfo> picked;
    if (m_batchSelectedIds.isEmpty())
        return picked;
    picked.reserve(m_batchSelectedIds.size());
    for (const MusicInfo &song : m_displayPlaylist) {
        if (m_batchSelectedIds.contains(song.id))
            picked.append(song);
    }
    return picked;
}

/**
 * 组装批量浮层的条目（计数行 + 全选/取消全选 + 加入歌单 + 下载 +
 * 取消收藏 + 移出播放队列 + 退出）。
 *
 * 抽成独立函数是为了让「全选」能**原地刷新**同一份浮层：
 * 它需要一份内容相同、只有文案与置灰状态变了的新条目表。
 */
QList<SongContextMenuPopup::Entry> PlaylistPanel::buildBatchMenuEntries()
{    const int count = m_batchSelectedIds.size();
    const bool hasSelection = count > 0;
    auto &i18n = I18n::instance();

    QList<SongContextMenuPopup::Entry> entries;

    // 1) 计数行：不可点击，只做展示
    SongContextMenuPopup::Entry counter;
    counter.label = i18n.tr(QStringLiteral("batchSelected")).arg(count);
    counter.disabled = true;
    entries.append(counter);

    // 2) 全选 / 取消全选（跟随当前状态切文案）
    SongContextMenuPopup::Entry selectAll;
    selectAll.iconName = "PlaylistAddCheck";
    selectAll.label = i18n.tr(m_batchSelectedIds.size() >= m_displayPlaylist.size()
                                  ? QStringLiteral("batchCancelSelectAll")
                                  : QStringLiteral("batchSelectAll"));
    selectAll.action = [this]() {
        const bool selectAll = m_batchSelectedIds.size() < m_displayPlaylist.size();
        m_batchSelectedIds.clear();
        if (selectAll) {
            for (const MusicInfo &song : m_displayPlaylist)
                m_batchSelectedIds.insert(song.id);
        }
        m_rowBatchChecked.clear();
        syncBatchToVisibleRows();
        refreshBatchBar();
        // 全选后计数、全选文案、动作项置灰状态都变了：
        // 直接原地换掉同一份浮层的内容，不关窗重开（避免一次可见闪动 / 叠层）
        if (m_batchMenu)
            m_batchMenu->setEntries(buildBatchMenuEntries());
    };
    entries.append(selectAll);

    // 3) 加入歌单
    SongContextMenuPopup::Entry addToPlaylist;
    addToPlaylist.iconName = "PlaylistAdd";
    addToPlaylist.label = i18n.tr(QStringLiteral("batchAddToPlaylist"));
    addToPlaylist.disabled = !hasSelection;
    if (hasSelection) {
        addToPlaylist.action = [this]() {
            const QList<MusicInfo> picked = batchSelectedSongs();
            if (!picked.isEmpty())
                emit batchAddToPlaylistRequested(picked);
            exitBatchMode();
        };
    }
    entries.append(addToPlaylist);

    // 4) 下载
    SongContextMenuPopup::Entry download;
    download.iconName = "Download";
    download.label = i18n.tr(QStringLiteral("batchDownload"));
    download.disabled = !hasSelection;
    if (hasSelection) {
        download.action = [this]() {
            const QList<MusicInfo> picked = batchSelectedSongs();
            if (!picked.isEmpty())
                emit batchDownloadRequested(picked);
            exitBatchMode();
        };
    }
    entries.append(download);

    // 5) 取消收藏（沿用「只取消不添加」语义）
    SongContextMenuPopup::Entry unfavorite;
    unfavorite.iconName = "HeartBroken";
    unfavorite.label = i18n.tr(QStringLiteral("batchUnfavorite"));
    unfavorite.disabled = !hasSelection;
    if (hasSelection) {
        unfavorite.action = [this]() {
            QList<int> ids;
            const QList<MusicInfo> picked = batchSelectedSongs();
            ids.reserve(picked.size());
            for (const MusicInfo &info : picked)
                ids.append(info.id);
            if (!ids.isEmpty())
                emit batchUnfavoriteRequested(ids);
            exitBatchMode();
        };
    }
    entries.append(unfavorite);

    // 6) 移出播放队列（队列抽屉特有）
    SongContextMenuPopup::Entry removeFromQueue;
    removeFromQueue.iconName = "Delete";
    removeFromQueue.label = i18n.tr(QStringLiteral("batchRemoveFromQueue"));
    removeFromQueue.disabled = !hasSelection;
    if (hasSelection)
        removeFromQueue.action = [this]() { removeSelectedFromQueue(); };
    entries.append(removeFromQueue);

    // 7) 退出批量模式
    SongContextMenuPopup::Entry exitMode;
    exitMode.iconName = "Close";
    exitMode.label = i18n.tr(QStringLiteral("batchExit"));
    exitMode.action = [this]() { exitBatchMode(); };
    entries.append(exitMode);

    return entries;
}

/**
 * 弹出批量操作的竖排悬浮菜单。
 *
 * 直接复用 SongContextMenuPopup（与其同一套面板 chrome / 行高 / hover 高亮），
 * 只在条目上扩展：第一行是置灰的「已选 N 首」计数，其余为动作项，
 * 选中 0 首时动作项置灰不可点。位置由 SongContextMenuPopup 做屏幕边界收敛，
 * 因此在 400px 宽的抽屉里也不会溢出被裁。
 *
 * m_batchMenu 持有这份浮层，供「全选」原地 setEntries() 刷新与退出时关闭。
 */
void PlaylistPanel::showBatchActionsMenu()
{
    if (m_remoteView || !m_batchMode)
        return;

    const QList<SongContextMenuPopup::Entry> entries = buildBatchMenuEntries();
    if (entries.isEmpty())
        return;

    // 已经开着就先收掉，避免叠两层
    if (m_batchMenu)
        m_batchMenu->close();

    QWidget *anchor = window() ? window() : this;
    // 拿到实例是为了让「全选」能原地 setEntries() 刷新，而不是关掉重开
    m_batchMenu = SongContextMenuPopup::showAt(anchor, batchMenuAnchorPos(), entries, true);
}

/** 竖排浮层的锚点：紧凑触发按钮的右下角（取不到就退回面板右上角） */
QPoint PlaylistPanel::batchMenuAnchorPos() const
{
    if (m_batchBar) {
        if (QPushButton *trigger = m_batchBar->triggerButton()) {
            if (trigger->isVisible())
                return trigger->mapToGlobal(QPoint(0, trigger->height() + 4));
        }
    }
    return mapToGlobal(QPoint(width() - 16, 60));
}

void PlaylistPanel::syncBatchToVisibleRows()
{
    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it) {
        auto *card = static_cast<PlaylistItemCard *>(it.value());
        if (!card)
            continue;
        const bool checked = m_batchMode && m_batchSelectedIds.contains(card->info().id);
        if (checked)
            m_rowBatchChecked.insert(it.key());
        else
            m_rowBatchChecked.remove(it.key());
        card->setBatchChecked(checked);
    }
}

void PlaylistPanel::refreshBatchBar()
{
    if (!m_batchBar)
        return;
    m_batchBar->setSelectedCount(m_batchSelectedIds.size());
    m_batchBar->setAllSelected(!m_displayPlaylist.isEmpty()
                               && m_batchSelectedIds.size() >= m_displayPlaylist.size());
}

void PlaylistPanel::enterBatchMode()
{
    if (m_remoteView || m_batchMode || m_displayPlaylist.isEmpty())
        return;

    m_batchMode = true;
    m_batchSelectedIds.clear();
    m_rowBatchChecked.clear();

    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it) {
        if (auto *card = static_cast<PlaylistItemCard *>(it.value()))
            card->setBatchMode(true);
    }
    syncBatchToVisibleRows();
    refreshBatchBar();

    if (m_actionStack)
        m_actionStack->setCurrentWidget(m_batchBar);
}

void PlaylistPanel::exitBatchMode()
{
    if (!m_batchMode)
        return;

    m_batchMode = false;
    m_batchSelectedIds.clear();
    m_rowBatchChecked.clear();

    // 批量模式退出了，浮层也没必要留着
    if (m_batchMenu && m_batchMenu->isVisible())
        m_batchMenu->close();

    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it) {
        if (auto *card = static_cast<PlaylistItemCard *>(it.value())) {
            card->setBatchMode(false);
            card->setBatchChecked(false);
        }
    }
    if (m_batchBar)
        m_batchBar->reset();
    if (m_actionStack && m_actionRow)
        m_actionStack->setCurrentWidget(m_actionRow);
}

/** 批量移出播放队列：本地操作，按序移除后收尾一次 Toast；执行完自动退出批量模式 */
void PlaylistPanel::removeSelectedFromQueue()
{
    const QList<MusicInfo> picked = batchSelectedSongs();
    if (picked.isEmpty())
        return;

    auto &manager = PlaylistManager::instance();
    for (const MusicInfo &info : picked)
        manager.removeFromPlaylist(info.id); // 内部按 music id 匹配

    Toast::show(this, I18n::instance().tr(QStringLiteral("batchQueueRemoved")).arg(picked.size()),
                Toast::Success);
    exitBatchMode();
    refresh();
}

void PlaylistPanel::setFullPlayerMode(bool on)
{
    if (m_fullPlayerMode == on)
        return;
    m_fullPlayerMode = on;
    setAttribute(Qt::WA_TranslucentBackground, on);

    // 这里**不能**用 setGraphicsEffect(nullptr) 来关阴影：
    // Qt 的 QWidget::setGraphicsEffect() 会把旧 effect 交给 deleteLater() 销毁，
    // 而 m_drawerShadow 仍指向它；下一次 setGraphicsEffect(m_drawerShadow) 就会把
    // 一个「已排队销毁」的 effect 重新挂上，等 DeferredDelete 执行后控件的
    // d->graphicsEffect 变成悬垂指针 —— 再调用时就崩在
    // QWidget::setGraphicsEffect（2026-10-28：Qt6Widgets 0x45523）。
    // 正确做法是只开关效果，不摘除。
    if (m_drawerShadow)
        m_drawerShadow->setEnabled(!on);

    applyPanelChrome();
    update();
}

void PlaylistPanel::applyPanelChrome()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString textMain = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#182B45");
    const QString textSub = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.62)");

    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 16px; font-weight: 700; color: %1; }").arg(textMain));
    }
    if (m_countLabel) {
        m_countLabel->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 12px; color: %1; margin-top: 2px; }").arg(textSub));
    }
    const QString footerBtn = QStringLiteral(
        "QPushButton { background: rgba(255,255,255,%1); border: none; border-radius: 8px; "
        "color: %2; font-size: 14px; font-weight: 500; min-height: 40px; padding: 0 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,%3); }")
                                    .arg(dark ? 8 : 12)
                                    .arg(textMain)
                                    .arg(dark ? 14 : 20);
    if (m_clearBtn)
        m_clearBtn->setStyleSheet(footerBtn);
    if (m_scrollCurrentBtn)
        m_scrollCurrentBtn->setStyleSheet(footerBtn);
    if (m_batchBtn) {
        const QColor ic = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
        m_batchBtn->setIcon(Icons::renderNamed("Batch", 16, ic));
        m_batchBtn->setIconSize(QSize(16, 16));
        m_batchBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: rgba(255,255,255,%1); border: none; border-radius: 8px; "
            "color: %2; font-size: 13px; font-weight: 500; min-height: 32px; padding: 0 14px; }"
            "QPushButton:hover:enabled { background: rgba(255,255,255,%3); }"
            "QPushButton:disabled { color: rgba(128,128,128,0.8); }")
                                      .arg(dark ? 8 : 12)
                                      .arg(textMain)
                                      .arg(dark ? 14 : 20));
    }
    if (m_batchBar)
        m_batchBar->applyTheme();
    const QColor closeIc = dark ? QColor(244, 246, 255, 180) : QColor(33, 37, 41, 180);
    if (m_closeBtn) {
        m_closeBtn->setIcon(Icons::renderNamed("Close", 18, closeIc));
        m_closeBtn->setIconSize(QSize(18, 18));
        m_closeBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: none; border-radius: 8px; min-width: 32px; min-height: 32px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.08); }"));
    }
    if (m_scroll) {
        m_scroll->setStyleSheet(QStringLiteral(
            "QScrollArea#ppScroll { border: none; background: transparent; }"
            "QScrollBar:vertical { width: 5px; background: transparent; margin: 2px 0 4px 0; }"
            "QScrollBar::handle:vertical { background: rgba(22, 119, 184,%1); border-radius: 3px; min-height: 40px; }"
            "QScrollBar::handle:vertical:hover { background: rgba(22, 119, 184,%2); }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }")
                                    .arg(dark ? 70 : 82)
                                    .arg(dark ? 108 : 125));
    }
    updateFooterButtonIcons();
}

void PlaylistPanel::setupUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("ppHeader"));
    auto *headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(16, 16, 12, 8);

    auto *titleCol = new QVBoxLayout();
    titleCol->setSpacing(0);
    m_titleLabel = new QLabel(I18n::instance().tr(QStringLiteral("playQueue")), header);
    m_countLabel = new QLabel(header);
    titleCol->addWidget(m_titleLabel);
    titleCol->addWidget(m_countLabel);
    headerLay->addLayout(titleCol, 1);

    m_deviceCombo = new QComboBox(header);
    m_deviceCombo->setMinimumWidth(130);
    m_deviceCombo->setCursor(Qt::PointingHandCursor);
    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_deviceCombo || index < 0)
            return;
        const QString id = m_deviceCombo->itemData(index).toString();
        LanDeviceManager::instance().selectDevice(id);
        m_remoteView = !id.isEmpty();
        refresh();
    });
    headerLay->addWidget(m_deviceCombo, 0, Qt::AlignVCenter);

    m_closeBtn = new QPushButton(header);
    m_closeBtn->setFixedSize(32, 32);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setFlat(true);
    connect(m_closeBtn, &QPushButton::clicked, this, &PlaylistPanel::hideRequested);
    headerLay->addWidget(m_closeBtn, 0, Qt::AlignTop);

    lay->addWidget(header);

    // 批量模式：「批量」按钮那排与批量操作条占同一行位置，进出模式互相切换
    m_actionStack = new QStackedWidget(this);
    m_actionStack->setFixedHeight(40);

    m_actionRow = new QWidget(m_actionStack);
    auto *actionLay = new QHBoxLayout(m_actionRow);
    actionLay->setContentsMargins(16, 0, 16, 8);
    actionLay->setSpacing(12);

    m_batchBtn = new QPushButton(I18n::instance().tr(QStringLiteral("batchManage")), m_actionRow);
    m_batchBtn->setCursor(Qt::PointingHandCursor);
    m_batchBtn->setFixedHeight(32);
    connect(m_batchBtn, &QPushButton::clicked, this, &PlaylistPanel::enterBatchMode);
    actionLay->addWidget(m_batchBtn);
    actionLay->addStretch();

    m_actionStack->addWidget(m_actionRow);

    m_batchBar = new BatchActionBar(m_actionStack);
    // 400px 宽的抽屉放不下横排 5 个动作按钮（文案会被压扁截断）：
    // 只留「已选 N 首」计数 + 「批量操作 (N) ▾」触发按钮，点了弹竖排浮层。
    // 「移出歌单」这个按钮的语义在队列里是「移出播放队列」，文案在浮层里给。
    m_batchBar->setCapabilities(true, false, true, true, true);
    m_batchBar->setCompactTrigger(true);
    m_batchBar->setActionText("removeFromPlaylist",
                              I18n::instance().tr(QStringLiteral("batchRemoveFromQueue")));
    connect(m_batchBar, &BatchActionBar::actionsRequested, this, &PlaylistPanel::showBatchActionsMenu);
    // 紧凑模式下横排按钮都藏着，动作条上的「退出」也用不上——退出入口在浮层里
    connect(m_batchBar, &BatchActionBar::exitRequested, this, &PlaylistPanel::exitBatchMode);
    m_actionStack->addWidget(m_batchBar);
    m_actionStack->setCurrentIndex(0);

    lay->addWidget(m_actionStack);

    // 用一层右侧留白的容器包住滚动区，把垂直滚动条整体左移 kScrollbarEdgeInset，
    // 脱离主窗口无边框边缘的缩放/拖动手感区（见 kScrollbarEdgeInset 注释）
    auto *scrollHost = new QWidget(this);
    auto *scrollHostLay = new QVBoxLayout(scrollHost);
    scrollHostLay->setContentsMargins(0, 0, kScrollbarEdgeInset, 0);
    scrollHostLay->setSpacing(0);

    m_scroll = new QScrollArea(scrollHost);
    m_scroll->setObjectName(QStringLiteral("ppScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_listContainer = new QWidget(m_scroll);

    m_emptyLabel = new QLabel(I18n::instance().tr(QStringLiteral("emptyPlaylistHint")), m_listContainer);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->hide();

    m_scroll->setWidget(m_listContainer);
    nekoPolishScrollAreaViewport(m_scroll);
    scrollHostLay->addWidget(m_scroll);
    lay->addWidget(scrollHost, 1);

    m_footer = new QWidget(this);
    auto *footerLay = new QHBoxLayout(m_footer);
    footerLay->setContentsMargins(16, 8, 16, 16);
    footerLay->setSpacing(16);

    m_clearBtn = new QPushButton(m_footer);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        if (m_togetherView) {
            emit togetherClearRequested();
            return;
        }
        if (m_remoteView) {
            emit remoteReplaceRequested(false);
            return;
        }
        auto &manager = PlaylistManager::instance();
        const int currentIndex = manager.currentIndex();
        MusicInfo currentMusic;
        const bool hasCurrent = (currentIndex >= 0 && currentIndex < manager.count());
        if (hasCurrent)
            currentMusic = manager.playlist()[currentIndex];

        if (hasCurrent) {
            manager.clearPlaylist();
            manager.addToPlaylist(currentMusic);
            manager.setCurrentIndex(0);
        } else {
            manager.clearPlaylist();
        }
        refresh();
    });
    footerLay->addWidget(m_clearBtn, 1);

    m_scrollCurrentBtn = new QPushButton(m_footer);
    m_scrollCurrentBtn->setCursor(Qt::PointingHandCursor);
    m_scrollCurrentBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_scrollCurrentBtn, &QPushButton::clicked, this, [this]() {
        scrollToCurrent();
    });
    footerLay->addWidget(m_scrollCurrentBtn, 1);

    lay->addWidget(m_footer);

    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        updateVisibleRows();
    });

    updateFooterButtonIcons();
}

void PlaylistPanel::updateFooterButtonIcons()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QColor ic = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
    if (m_clearBtn) {
        const QString label = m_togetherView
            ? QStringLiteral("清空房间列表")
            : m_remoteView ? QStringLiteral("替换本机列表")
                           : I18n::instance().tr(QStringLiteral("clear"));
        const char *icon = m_togetherView ? "DeleteSweep"
            : m_remoteView ? "AddList"
                           : "DeleteSweep";
        m_clearBtn->setText(label);
        m_clearBtn->setIcon(Icons::renderNamed(icon, 18, ic));
        m_clearBtn->setIconSize(QSize(18, 18));
    }
    if (m_scrollCurrentBtn) {
        const QString label = m_remoteView
            ? QStringLiteral("当前播放")
            : I18n::instance().tr(QStringLiteral("scrollToCurrentPlay"));
        const char *icon = "Location";
        m_scrollCurrentBtn->setText(label);
        m_scrollCurrentBtn->setIcon(Icons::renderNamed(icon, 18, ic));
        m_scrollCurrentBtn->setIconSize(QSize(18, 18));
    }
}

void PlaylistPanel::updateCountLabel()
{
    if (m_countLabel) {
        if (m_togetherView) {
            m_countLabel->setText(
                QStringLiteral("%1 首 · %2")
                    .arg(m_displayPlaylist.size())
                    .arg(m_togetherConnected ? QStringLiteral("实时同步")
                                             : QStringLiteral("正在重连")));
        } else {
            m_countLabel->setText(
                I18n::instance().tr(QStringLiteral("playlistSongCount"))
                    .arg(m_displayPlaylist.size()));
        }
    }
}

void PlaylistPanel::syncContainerHeight()
{
    const int count = m_displayPlaylist.size();
    const int contentH = count > 0
        ? kListPad * 2 + count * kPlaylistRowStride - kPlaylistRowSpacing
        : 0;
    const int viewH = m_scroll ? m_scroll->viewport()->height() : 0;
    m_listContainer->setMinimumHeight(qMax(contentH, viewH));
    if (m_scroll)
        m_listContainer->setFixedWidth(m_scroll->viewport()->width());
}

void PlaylistPanel::clearAllCards()
{
    const QList<int> rows = m_rowCards.keys();
    for (int row : rows)
        releasePlaylistCard(static_cast<PlaylistItemCard *>(m_rowCards.take(row)), m_cardPool);
}

void PlaylistPanel::updateVisibleRows()
{
    const int count = m_displayPlaylist.size();
    if (count <= 0 || !m_scroll)
        return;

    const int scrollY = m_scroll->verticalScrollBar()->value();
    const int viewH = m_scroll->viewport()->height();
    const int first = qMax(0, (scrollY - kListPad) / kPlaylistRowStride - kVisibleBuffer);
    const int last = qMin(count - 1, (scrollY + viewH - kListPad) / kPlaylistRowStride + kVisibleBuffer);

    QList<int> stale;
    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it) {
        if (it.key() < first || it.key() > last)
            stale.append(it.key());
    }
    for (int row : stale) {
        m_rowBatchChecked.remove(row);
        releasePlaylistCard(static_cast<PlaylistItemCard *>(m_rowCards.take(row)), m_cardPool);
    }

    const int w = qMax(40, m_listContainer->width() - 2 * kListPad);
    for (int row = first; row <= last; ++row) {
        if (m_rowCards.contains(row)) {
            auto *card = static_cast<PlaylistItemCard *>(m_rowCards[row]);
            card->move(kListPad, kListPad + row * kPlaylistRowStride);
            card->setFixedWidth(w);
            continue;
        }
        bindRowCard(acquirePlaylistCard(m_listContainer, m_cardPool), row);
    }
}

void PlaylistPanel::refresh()
{
    int scrollPos = 0;
    if (m_scroll)
        scrollPos = m_scroll->verticalScrollBar()->value();

    m_remoteView = !m_togetherView
        && !LanDeviceManager::instance().selectedDeviceId().isEmpty();
    if (m_togetherView) {
        m_displayPlaylist.clear();
        m_togetherEntryIds.clear();
        m_togetherCurrentEntryId =
            m_togetherPlaylist.value(QStringLiteral("currentEntryId")).toString();
        const QVariantList items = m_togetherPlaylist.value(QStringLiteral("items")).toList();
        for (const QVariant &value : items) {
            const QVariantMap item = value.toMap();
            const QVariantMap track = item.value(QStringLiteral("track")).toMap();
            MusicInfo info;
            info.id = track.value(QStringLiteral("id")).toInt();
            info.title = track.value(QStringLiteral("title")).toString();
            info.artist = track.value(QStringLiteral("artist")).toString();
            info.album = track.value(QStringLiteral("album")).toString();
            info.duration = track.value(QStringLiteral("duration")).toInt();
            if (info.id > 0) {
                info.coverUrl = QString::fromUtf8("%1/api/music/cover/%2")
                                    .arg(QString::fromUtf8(Theme::kApiBase))
                                    .arg(info.id);
            }
            const QString entryId = item.value(QStringLiteral("entryId")).toString();
            m_togetherEntryIds.append(entryId);
            m_displayPlaylist.append(info);
        }
        m_displayCurrentIndex = -1;
        for (int i = 0; i < m_togetherEntryIds.size(); ++i) {
            if (m_togetherEntryIds.at(i) == m_togetherCurrentEntryId) {
                m_displayCurrentIndex = i;
                break;
            }
        }
    } else if (m_remoteView) {
        m_displayPlaylist = LanDeviceManager::instance().remoteQueue().items;
        m_displayCurrentIndex = LanDeviceManager::instance().remoteQueue().currentIndex;
    } else {
        m_displayPlaylist = PlaylistManager::instance().playlist();
        m_displayCurrentIndex = PlaylistManager::instance().currentIndex();
    }

    const int count = m_displayPlaylist.size();

    // 队列增删后：把已经不在队列里的选中项丢掉，避免「已选 N 首」把看不见的曲目也算进去
    {
        QSet<int> valid;
        valid.reserve(count);
        for (const MusicInfo &song : m_displayPlaylist)
            valid.insert(song.id);
        const int before = m_batchSelectedIds.size();
        m_batchSelectedIds.intersect(valid);
        m_rowBatchChecked.clear();
        if (m_batchSelectedIds.size() != before)
            refreshBatchBar();
    }

    updateCountLabel();
    updateFooterButtonIcons();
    applyTogetherHeader();
    if (m_batchBtn) {
        // 远端设备的队列是只读视图，也没有可批量移除的东西
        m_batchBtn->setEnabled(!m_remoteView && !m_togetherView && !m_displayPlaylist.isEmpty());
    }
    // 远端视图 / 空队列下批量模式不成立（切设备后要自动退出）
    if (m_batchMode && (m_remoteView || m_togetherView || count == 0))
        exitBatchMode();
    if (m_clearBtn)
        m_clearBtn->setEnabled(m_togetherView || !m_remoteView || !m_displayPlaylist.isEmpty());
    if (m_scrollCurrentBtn)
        m_scrollCurrentBtn->setEnabled(!m_remoteView || !m_displayPlaylist.isEmpty());

    if (count == 0) {
        clearAllCards();
        m_lastPlaylistSize = 0;
        m_listContainer->setMinimumHeight(0);
        if (m_emptyLabel) {
            const bool dark = Theme::ThemeManager::instance().isDarkMode();
            const QString c = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.55)");
            m_emptyLabel->setText(m_togetherView
                                      ? QStringLiteral("房间临时播放列表为空")
                                      : I18n::instance().tr(QStringLiteral("emptyPlaylistHint")));
            m_emptyLabel->setStyleSheet(QStringLiteral(
                "QLabel { color: %1; font-size: 13px; padding: 48px 24px; line-height: 1.5; }")
                                            .arg(c));
            m_emptyLabel->setGeometry(m_listContainer->rect());
            m_emptyLabel->show();
        }
        return;
    }

    if (m_emptyLabel)
        m_emptyLabel->hide();

    clearAllCards();
    m_lastPlaylistSize = count;

    syncContainerHeight();
    updateVisibleRows();

    const int cur = m_displayCurrentIndex;
    for (auto it = m_rowCards.constBegin(); it != m_rowCards.constEnd(); ++it)
        static_cast<PlaylistItemCard *>(it.value())->updateCurrentState(it.key() == cur);

    if (m_scroll)
        m_scroll->verticalScrollBar()->setValue(scrollPos);
}

void PlaylistPanel::scrollToCurrent()
{
    const int cur = m_displayCurrentIndex;
    if (cur < 0 || !m_scroll)
        return;
    const int y = kListPad + cur * kPlaylistRowStride;
    const int viewH = m_scroll->viewport()->height();
    const int itemBottom = y + kPlaylistRowHeight;
    int scrollVal = m_scroll->verticalScrollBar()->value();
    if (y < scrollVal)
        scrollVal = y;
    else if (itemBottom > scrollVal + viewH)
        scrollVal = itemBottom - viewH;
    m_scroll->verticalScrollBar()->setValue(scrollVal);
    updateVisibleRows();
}

void PlaylistPanel::refreshDevices()
{
    if (!m_deviceCombo)
        return;
    if (m_togetherView) {
        m_deviceCombo->hide();
        return;
    }

    const QString selected = LanDeviceManager::instance().selectedDeviceId();
    QSignalBlocker blocker(m_deviceCombo);
    m_deviceCombo->clear();
    m_deviceCombo->addItem(QStringLiteral("本机"), QString());
    for (const LanDeviceInfo &device : LanDeviceManager::instance().devices())
        m_deviceCombo->addItem(device.deviceName, device.deviceId);

    int index = m_deviceCombo->findData(selected);
    if (index < 0)
        index = 0;
    m_deviceCombo->setCurrentIndex(index);
    m_remoteView = index > 0;
    refresh();
}

void PlaylistPanel::refreshRemoteQueue()
{
    if (m_remoteView)
        refresh();
}

void PlaylistPanel::setTogetherState(bool active, const QVariantMap &playlist, bool connected)
{
    const bool changed = m_togetherView != active;
    m_togetherView = active;
    m_togetherConnected = connected;
    m_togetherPlaylist = active ? playlist : QVariantMap();
    if (changed)
        exitBatchMode();
    applyTogetherHeader();
    refresh();
}

void PlaylistPanel::applyTogetherHeader()
{
    if (m_titleLabel)
        m_titleLabel->setText(m_togetherView ? QStringLiteral("房间临时播放列表")
                                            : I18n::instance().tr(QStringLiteral("playQueue")));
    if (m_deviceCombo)
        m_deviceCombo->setVisible(!m_togetherView);
    if (m_actionStack)
        m_actionStack->setVisible(!m_togetherView);
    if (m_scrollCurrentBtn)
        m_scrollCurrentBtn->setText(m_togetherView
                                        ? QStringLiteral("当前播放")
                                        : I18n::instance().tr(
                                              QStringLiteral("scrollToCurrentPlay")));
}

void PlaylistPanel::retranslate()
{
    if (m_titleLabel) {
        m_titleLabel->setText(m_togetherView ? QStringLiteral("房间临时播放列表")
                                            : I18n::instance().tr(QStringLiteral("playQueue")));
    }
    if (m_emptyLabel) {
        m_emptyLabel->setText(m_togetherView
                                  ? QStringLiteral("房间临时播放列表为空")
                                  : I18n::instance().tr(QStringLiteral("emptyPlaylistHint")));
    }
    if (m_batchBtn)
        m_batchBtn->setText(I18n::instance().tr(QStringLiteral("batchManage")));
    if (m_batchBar) {
        // 队列语境下这个按钮是「移出播放队列」而不是「移出歌单」
        m_batchBar->setActionText("removeFromPlaylist",
                                  I18n::instance().tr(QStringLiteral("batchRemoveFromQueue")));
        m_batchBar->retranslate();
    }
    updateCountLabel();
    updateFooterButtonIcons();
    applyPanelChrome();
}

void PlaylistPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_scroll || !m_listContainer)
        return;
    m_listContainer->setFixedWidth(m_scroll->viewport()->width());
    if (m_emptyLabel && m_emptyLabel->isVisible())
        m_emptyLabel->setGeometry(m_listContainer->rect());
    const int w = qMax(40, m_listContainer->width() - 2 * kListPad);
    for (auto *card : m_rowCards)
        static_cast<PlaylistItemCard *>(card)->setFixedWidth(w);
}
void PlaylistPanel::syncToHost()
{
    QWidget *host = parentWidget();
    if (!host)
        return;
    const int h = host->height();
    setFixedHeight(h);
    const int x = m_drawerOpen ? host->width() - kDrawerWidth : host->width();
    setGeometry(x, 0, kDrawerWidth, h);
}

void PlaylistPanel::openDrawer()
{
    QWidget *host = parentWidget();
    if (!host)
        return;

    if (!m_slideAnim.isNull()) {
        // DeleteWhenStopped：stop() 已让 Qt 负责销毁，不能再 deleteLater() 二次删除
        m_slideAnim->stop();
        m_slideAnim = nullptr;
    }

    refresh();
    scrollToCurrent();

    const int h = host->height();
    setFixedHeight(h);
    show();
    raise();

    m_drawerOpen = true;
    const QRect end(host->width() - kDrawerWidth, 0, kDrawerWidth, h);
    const QRect start(host->width(), 0, kDrawerWidth, h);

    if (geometry() == end) {
        m_animating = false;
        return;
    }

    m_animating = true;
    setGeometry(start);
    m_slideAnim = new QPropertyAnimation(this, "geometry", this);
    m_slideAnim->setDuration(Theme::kAnimNormal);
    m_slideAnim->setStartValue(start);
    m_slideAnim->setEndValue(end);
    m_slideAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slideAnim.data(), &QPropertyAnimation::finished, this, [this]() {
        m_animating = false;
        m_slideAnim = nullptr;
    });
    m_slideAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void PlaylistPanel::closeDrawer()
{
    QWidget *host = parentWidget();
    if (!host) {
        hide();
        m_drawerOpen = false;
        emit drawerClosed();
        return;
    }

    if (!m_drawerOpen && !isVisible())
        return;

    if (!m_slideAnim.isNull()) {
        // DeleteWhenStopped：stop() 已让 Qt 负责销毁，不能再 deleteLater() 二次删除
        m_slideAnim->stop();
        m_slideAnim = nullptr;
    }

    m_drawerOpen = false;
    const int h = host->height();
    const QRect start(geometry());
    const QRect end(host->width(), 0, kDrawerWidth, h);

    if (!isVisible() || start == end) {
        hide();
        syncToHost();
        emit drawerClosed();
        return;
    }

    m_animating = true;
    m_slideAnim = new QPropertyAnimation(this, "geometry", this);
    m_slideAnim->setDuration(Theme::kAnimNormal);
    m_slideAnim->setStartValue(start);
    m_slideAnim->setEndValue(end);
    m_slideAnim->setEasingCurve(QEasingCurve::InCubic);
    connect(m_slideAnim.data(), &QPropertyAnimation::finished, this, [this]() {
        m_animating = false;
        m_slideAnim = nullptr;
        hide();
        syncToHost();
        emit drawerClosed();
    });
    m_slideAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void PlaylistPanel::showPanel()
{
    openDrawer();
}

void PlaylistPanel::hidePanel()
{
    closeDrawer();
}

void PlaylistPanel::togglePanel()
{
    if (m_drawerOpen)
        closeDrawer();
    else
        openDrawer();
}

void PlaylistPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    if (m_fullPlayerMode)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool dark = Theme::ThemeManager::instance().isDarkMode();

    QPainterPath clip;
    const QRect r = rect();
    const int rad = 12;
    clip.moveTo(r.right(), r.top());
    clip.lineTo(r.left() + rad, r.top());
    clip.arcTo(r.left(), r.top(), rad * 2, rad * 2, 90, 90);
    clip.lineTo(r.left(), r.bottom() - rad);
    clip.arcTo(r.left(), r.bottom() - rad * 2, rad * 2, rad * 2, 180, 90);
    clip.lineTo(r.right(), r.bottom());
    clip.closeSubpath();

    p.fillPath(clip, dark ? QColor(22, 32, 50, 248) : QColor(246, 250, 255, 248));
    p.setPen(QPen(dark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 30), 1.0));
    p.drawPath(clip);
}
