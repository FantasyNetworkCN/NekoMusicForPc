/**
 * @file batchactionbar.cpp
 * @brief 单曲列表批量编辑模式的操作条
 */

#include "batchactionbar.h"

#include "core/i18n.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/headeractionmenu.h"
#include "ui/svgicon.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

BatchActionBar::BatchActionBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("batchActionBar"));
    setFixedHeight(40);

    m_countLbl = new QLabel(this);
    m_countLbl->setObjectName(QStringLiteral("batchCountLabel"));

    m_selectAllBtn = makeActionButton("PlaylistAddCheck", "batchSelectAll");
    connect(m_selectAllBtn, &QPushButton::clicked, this, &BatchActionBar::selectAllToggled);

    m_addToPlaylistBtn = makeActionButton("PlaylistAdd", "batchAddToPlaylist");
    connect(m_addToPlaylistBtn, &QPushButton::clicked, this, &BatchActionBar::addToPlaylistRequested);

    m_addToQueueBtn = makeActionButton("MusicList", "batchAddToQueue");
    connect(m_addToQueueBtn, &QPushButton::clicked, this, &BatchActionBar::addToQueueRequested);

    m_downloadBtn = makeActionButton("Download", "batchDownload");
    connect(m_downloadBtn, &QPushButton::clicked, this, &BatchActionBar::downloadRequested);

    m_unfavoriteBtn = makeActionButton("HeartBroken", "batchUnfavorite");
    connect(m_unfavoriteBtn, &QPushButton::clicked, this, &BatchActionBar::unfavoriteRequested);

    m_removeFromPlaylistBtn = makeActionButton("Delete", "batchRemoveFromPlaylist");
    connect(m_removeFromPlaylistBtn, &QPushButton::clicked, this,
            &BatchActionBar::removeFromPlaylistRequested);

    m_exitBtn = makeActionButton("Close", "batchExit");
    connect(m_exitBtn, &QPushButton::clicked, this, &BatchActionBar::exitRequested);

    setCapabilities(false, true, true, false, false);
    rebuildLayout();
}

QPushButton *BatchActionBar::makeActionButton(const char *iconName, const char *textKey)
{
    auto *btn = new QPushButton(this);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(40);
    btn->setProperty("batchIcon", QString::fromUtf8(iconName));
    btn->setProperty("batchTextKey", QString::fromUtf8(textKey));
    return btn;
}

void BatchActionBar::setCapabilities(bool canAddToPlaylist, bool canAddToQueue, bool canDownload,
                                     bool canUnfavorite, bool canRemoveFromPlaylist)
{
    m_canAddToPlaylist = canAddToPlaylist;
    m_canAddToQueue = canAddToQueue;
    m_canDownload = canDownload;
    m_canUnfavorite = canUnfavorite;
    m_canRemoveFromPlaylist = canRemoveFromPlaylist;
    if (layout())
        rebuildLayout();
    setSelectedCount(m_selectedCount);
}

void BatchActionBar::setActionText(const char *actionKey, const QString &text)
{
    const QString key = QString::fromUtf8(actionKey);
    m_textOverrides.insert(key, text);
    for (QPushButton *btn : {m_addToPlaylistBtn, m_addToQueueBtn, m_downloadBtn, m_unfavoriteBtn,
                             m_removeFromPlaylistBtn}) {
        if (btn && btn->property("batchTextKey").toString() == key) {
            btn->setText(text);
            return;
        }
    }
}

namespace {

/** 拆掉旧布局但不动其中的 widget（QHBoxLayout 只 QLayoutItem::takeAt 不会删 widget）。 */
void dropLayout(QLayout *lay)
{
    if (!lay)
        return;
    while (QLayoutItem *item = lay->takeAt(0))
        delete item;
    delete lay;
}

} // namespace

void BatchActionBar::setCompactTrigger(bool compact)
{
    if (m_compactTrigger == compact)
        return;
    m_compactTrigger = compact;
    if (compact)
        m_actionMenuMode = false;
    rebuildLayout();
}

void BatchActionBar::setActionMenuMode(bool enabled)
{
    if (m_actionMenuMode == enabled)
        return;
    m_actionMenuMode = enabled;
    if (enabled)
        m_compactTrigger = false;
    rebuildLayout();
}

void BatchActionBar::rebuildLayout()
{
    // 先把所有控件从旧布局摘下来（removeWidget 不删控件），再换布局重新排
    if (auto *old = layout()) {
        QWidget *widgets[] = {m_countLbl,       m_selectAllBtn, m_addToPlaylistBtn,
                              m_addToQueueBtn,  m_downloadBtn,  m_unfavoriteBtn,
                              m_removeFromPlaylistBtn, m_exitBtn, m_triggerBtn};
        for (QWidget *widget : widgets) {
            if (widget)
                old->removeWidget(widget);
        }
        dropLayout(old);
    }

    const bool needsTrigger = m_compactTrigger || m_actionMenuMode;
    if (needsTrigger && !m_triggerBtn) {
        m_triggerBtn = makeActionButton("Menu", "batchActions");
        connect(m_triggerBtn, &QPushButton::clicked, this, [this]() {
            if (m_actionMenuMode)
                showActionMenu();
            else
                emit actionsRequested();
        });
    } else if (!needsTrigger && m_triggerBtn) {
        m_triggerBtn->hide();
        m_triggerBtn->deleteLater();
        m_triggerBtn = nullptr;
    }

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(12);

    if (m_countLbl) {
        m_countLbl->setParent(this);
        m_countLbl->setMinimumWidth(m_compactTrigger ? 0 : 96);
        lay->addWidget(m_countLbl);
        m_countLbl->show();
    }

    if (m_compactTrigger) {
        // 窄抽屉：只留计数 + 触发按钮，具体动作由调用方弹竖排浮层
        for (QPushButton *btn : {m_selectAllBtn, m_addToPlaylistBtn, m_addToQueueBtn,
                                 m_downloadBtn, m_unfavoriteBtn, m_removeFromPlaylistBtn,
                                 m_exitBtn}) {
            if (btn)
                btn->hide();
        }
        if (m_triggerBtn) {
            m_triggerBtn->setParent(this);
            m_triggerBtn->setFixedHeight(32);
            lay->addWidget(m_triggerBtn);
            m_triggerBtn->show();
        }
    } else {
        if (m_selectAllBtn) {
            m_selectAllBtn->setParent(this);
            lay->addWidget(m_selectAllBtn);
            m_selectAllBtn->show();
        }

        if (m_actionMenuMode) {
            // 普通列表页：横排动作收进「批量操作」浮层，避免一排七个按钮挤在一起
            for (QPushButton *btn : {m_addToPlaylistBtn, m_addToQueueBtn, m_downloadBtn,
                                     m_unfavoriteBtn, m_removeFromPlaylistBtn}) {
                if (btn)
                    btn->hide();
            }
            if (m_triggerBtn) {
                m_triggerBtn->setParent(this);
                m_triggerBtn->setFixedHeight(40);
                lay->addWidget(m_triggerBtn);
                m_triggerBtn->show();
            }
        } else {
            auto placeAction = [lay, this](QPushButton *btn, bool visible) {
                if (!btn)
                    return;
                btn->setParent(this);
                if (visible) {
                    lay->addWidget(btn);
                    btn->show();
                } else {
                    btn->hide();
                }
            };
            placeAction(m_addToPlaylistBtn, m_canAddToPlaylist);
            placeAction(m_addToQueueBtn, m_canAddToQueue);
            placeAction(m_downloadBtn, m_canDownload);
            placeAction(m_unfavoriteBtn, m_canUnfavorite);
            placeAction(m_removeFromPlaylistBtn, m_canRemoveFromPlaylist);
        }
    }

    lay->addStretch();

    if (!m_compactTrigger && m_exitBtn) {
        m_exitBtn->setParent(this);
        lay->addWidget(m_exitBtn);
        m_exitBtn->show();
    } else if (m_exitBtn) {
        m_exitBtn->hide();
    }

    retranslate();
    applyTheme();
}

void BatchActionBar::showActionMenu()
{
    if (!m_triggerBtn)
        return;

    auto *menu = new HeaderActionMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setMinimumWidth(m_triggerBtn->width());

    const bool hasSelection = m_selectedCount > 0;
    auto actionText = [this](const char *key) {
        const QString textKey = QString::fromUtf8(key);
        const auto overridden = m_textOverrides.constFind(textKey);
        return overridden != m_textOverrides.constEnd() ? overridden.value()
                                                        : I18n::instance().tr(textKey);
    };

    if (m_canAddToPlaylist) {
        QAction *action = menu->addHeaderAction(actionText("batchAddToPlaylist"), "PlaylistAdd",
                                                hasSelection);
        connect(action, &QAction::triggered, this, [this]() { emit addToPlaylistRequested(); });
    }
    if (m_canAddToQueue) {
        QAction *action = menu->addHeaderAction(actionText("batchAddToQueue"), "MusicList",
                                                hasSelection);
        connect(action, &QAction::triggered, this, [this]() { emit addToQueueRequested(); });
    }
    if (m_canDownload) {
        QAction *action = menu->addHeaderAction(actionText("batchDownload"), "Download",
                                                hasSelection);
        connect(action, &QAction::triggered, this, [this]() { emit downloadRequested(); });
    }
    if (m_canUnfavorite) {
        QAction *action = menu->addHeaderAction(actionText("batchUnfavorite"), "HeartBroken",
                                                hasSelection);
        connect(action, &QAction::triggered, this, [this]() { emit unfavoriteRequested(); });
    }
    if (m_canRemoveFromPlaylist) {
        if (!menu->isEmpty())
            menu->addSeparator();
        QAction *action = menu->addHeaderAction(actionText("batchRemoveFromPlaylist"), "Delete",
                                                hasSelection);
        connect(action, &QAction::triggered, this,
                [this]() { emit removeFromPlaylistRequested(); });
    }

    if (menu->isEmpty()) {
        delete menu;
        return;
    }
    menu->popup(m_triggerBtn->mapToGlobal(QPoint(0, m_triggerBtn->height() + 4)));
}

void BatchActionBar::setSelectedCount(int count)
{
    m_selectedCount = qMax(0, count);
    updateCountLabels();

    const bool hasSelection = m_selectedCount > 0;
    for (QPushButton *btn : {m_addToPlaylistBtn, m_addToQueueBtn, m_downloadBtn, m_unfavoriteBtn,
                             m_removeFromPlaylistBtn}) {
        if (btn)
            btn->setEnabled(hasSelection);
    }
}

/**
 * 「已选 N 首」的两处显示：横排的计数标签 + 紧凑模式的「批量操作 (N) ▾」触发按钮。
 * 必须由 setSelectedCount() 直接驱动——只靠 retranslate() 会漏掉 0→1 的变化
 * （setAllSelected() 状态没变就 return，走不到 retranslate）。
 */
void BatchActionBar::updateCountLabels()
{
    auto &i18n = I18n::instance();
    if (m_countLbl) {
        m_countLbl->setText(i18n.tr(QStringLiteral("batchSelected")).arg(m_selectedCount));
    }
    if (m_triggerBtn) {
        if (m_compactTrigger) {
            m_triggerBtn->setText(i18n.tr(QStringLiteral("batchActionsCount")).arg(m_selectedCount));
        } else if (m_actionMenuMode) {
            // 计数已经在左侧标签里，触发按钮不用再重复一遍
            m_triggerBtn->setText(i18n.tr(QStringLiteral("batchActions")));
        }
    }
}

void BatchActionBar::setAllSelected(bool allSelected)
{
    if (m_allSelected == allSelected)
        return;
    m_allSelected = allSelected;
    retranslate();
}

void BatchActionBar::retranslate()
{
    auto &i18n = I18n::instance();
    if (m_selectAllBtn) {
        m_selectAllBtn->setText(i18n.tr(m_allSelected ? QStringLiteral("batchCancelSelectAll")
                                                      : QStringLiteral("batchSelectAll")));
    }

    for (QPushButton *btn : {m_selectAllBtn, m_addToPlaylistBtn, m_addToQueueBtn, m_downloadBtn,
                             m_unfavoriteBtn, m_removeFromPlaylistBtn, m_exitBtn, m_triggerBtn}) {
        if (!btn)
            continue;
        const QString key = btn->property("batchTextKey").toString();
        if (key.isEmpty())
            continue;
        // 调用方给过覆盖文案的（如队列抽屉的「移出播放队列」）优先
        const auto overridden = m_textOverrides.constFind(key);
        btn->setText(overridden != m_textOverrides.constEnd() ? overridden.value() : i18n.tr(key));
    }

    updateCountLabels();
}

void BatchActionBar::applyTheme()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString titleFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");

    const QString secondaryBg = dark ? QStringLiteral("#2a2a2a") : QStringLiteral("#f0f0f0");
    const QString secondaryFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#212529");
    const QColor secondaryIc = dark ? QColor(244, 246, 255, 200) : QColor(33, 37, 41, 200);
    const QColor primaryIc = dark ? QColor(255, 255, 255) : QColor(255, 255, 255);

    if (m_countLbl) {
        m_countLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 14px; font-weight: 600; color: %1; }").arg(titleFg));
    }

    // 与页面原有按钮排保持同一套胶囊样式：主操作（加入歌单）用品牌色，其余用次级底色
    const QString primaryStyle = QStringLiteral(
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
        "QPushButton:disabled { background: rgba(230,57,80,0.35); color: rgba(255,255,255,0.6); }");

    const QString secondaryStyle = QStringLiteral(
        "QPushButton {"
        "  background: %1;"
        "  color: %2;"
        "  border: none;"
        "  border-radius: 20px;"
        "  font-size: 14px;"
        "  font-weight: 500;"
        "  padding: 0 20px;"
        "}"
        "QPushButton:hover:enabled { background: rgba(230,57,80,0.15); }"
        "QPushButton:disabled { color: rgba(128,128,128,0.8); }")
                                        .arg(secondaryBg, secondaryFg);

    for (QPushButton *btn : {m_selectAllBtn, m_addToPlaylistBtn, m_addToQueueBtn, m_downloadBtn,
                             m_unfavoriteBtn, m_removeFromPlaylistBtn, m_exitBtn}) {
        if (!btn)
            continue;
        const bool primary = (btn == m_addToPlaylistBtn);
        btn->setStyleSheet(primary ? primaryStyle : secondaryStyle);
        btn->setIcon(Icons::renderNamed(btn->property("batchIcon").toString().toUtf8().constData(), 18,
                                        primary ? primaryIc : secondaryIc));
        btn->setIconSize(QSize(18, 18));
    }

    // 「批量操作」触发按钮：窄抽屉用短胶囊，普通列表页与横排按钮同样高
    if (m_triggerBtn) {
        if (m_compactTrigger) {
            m_triggerBtn->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "  background: %1;"
                "  color: %2;"
                "  border: none;"
                "  border-radius: 16px;"
                "  font-size: 13px;"
                "  font-weight: 500;"
                "  padding: 0 14px;"
                "}"
                "QPushButton:hover { background: rgba(230,57,80,0.15); }")
                                            .arg(secondaryBg, secondaryFg));
            m_triggerBtn->setIcon(Icons::renderNamed("Menu", 16, secondaryIc));
            m_triggerBtn->setIconSize(QSize(16, 16));
        } else {
            m_triggerBtn->setStyleSheet(secondaryStyle);
            m_triggerBtn->setIcon(Icons::renderNamed("Menu", 18, secondaryIc));
            m_triggerBtn->setIconSize(QSize(18, 18));
        }
    }
}
