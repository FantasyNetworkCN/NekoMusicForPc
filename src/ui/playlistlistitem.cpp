#include "ui/playlistlistitem.h"
#include "ui/roundcoverlabel.h"
#include "core/covercache.h"
#include "core/i18n.h"
#include "theme/thememanager.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QMenu>
#include <QHBoxLayout>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QFontMetrics>

PlaylistListItem::PlaylistListItem(int playlistId, const QString& name, int musicCount, const QString& coverUrl, Mode mode, QWidget *parent)
    : QWidget(parent), m_playlistId(playlistId), m_name(name), m_musicCount(musicCount), m_mode(mode)
{
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(56);  // padding 10+36+10 = 56
    setObjectName(QStringLiteral("playlistListItem"));

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(16, 10, 16, 10);
    lay->setSpacing(10);

    // Cover
    m_coverLbl = new RoundCoverLabel(8, this);
    m_coverLbl->setFixedSize(36, 36);
    lay->addWidget(m_coverLbl);

    if (!coverUrl.isEmpty()) {
        const QString cacheKey = CoverCache::musicIdFromCoverUrl(coverUrl);
        if (!cacheKey.isEmpty()) {
            if (QPixmap cached = CoverCache::instance()->get(cacheKey); !cached.isNull()) {
                m_coverLbl->setPixmap(cached.scaled(36, 36, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            } else {
                setPlaceholderCover();
                QPointer<PlaylistListItem> self(this);
                m_coverConn = connect(CoverCache::instance(), &CoverCache::coverLoaded, this,
                                      [self, cacheKey](const QString &id, const QPixmap &pix) {
                                          if (!self || id != cacheKey || pix.isNull())
                                              return;
                                          QObject::disconnect(self->m_coverConn);
                                          self->m_coverConn = {};
                                          if (self->m_coverLbl) {
                                              self->m_coverLbl->setPixmap(
                                                  pix.scaled(36, 36, Qt::KeepAspectRatioByExpanding,
                                                             Qt::SmoothTransformation));
                                          }
                                      });
                CoverCache::instance()->fetchCover(cacheKey, coverUrl);
            }
        } else {
            setPlaceholderCover();
        }
    } else {
        setPlaceholderCover();
    }

    // Name（颜色由全局 QSS #playlistListItemName 按主题提供）
    m_nameLbl = new QLabel(this);
    m_nameLbl->setObjectName(QStringLiteral("playlistListItemName"));
    m_nameLbl->setText(m_name);
    m_nameLbl->setAlignment(Qt::AlignVCenter);
    m_nameLbl->setWordWrap(false);
    // 允许在空间不足时收缩，配合下面的 elide 使用；否则长名字会把条目撑宽再被硬切
    m_nameLbl->setMinimumWidth(0);
    m_nameLbl->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_nameLbl->setToolTip(m_name);
    lay->addWidget(m_nameLbl, 1);

    // 布局完成后再算一次省略号（此时标签宽度才确定）
    QTimer::singleShot(0, this, [this]() { applyElidedName(); });

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this, [this]() {
        update();
    });
}

void PlaylistListItem::setMusicCount(int count) {
    m_musicCount = count;
}

void PlaylistListItem::setPlaceholderCover() {
    QPixmap pix(36, 36);
    pix.fill(QColor(128, 128, 128, 60));
    m_coverLbl->setPixmap(pix);
}

void PlaylistListItem::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyElidedName();
}

void PlaylistListItem::applyElidedName()
{
    if (!m_nameLbl)
        return;
    const int avail = m_nameLbl->width();
    if (avail <= 0)
        return;
    // Qt 的 QLabel 不会自动加省略号，宽度不够时会在字形中间硬切（如“张信哲/拿…”被切成半字）
    const QFontMetrics fm(m_nameLbl->font());
    const QString elided = fm.elidedText(m_name, Qt::ElideRight, avail);
    if (m_nameLbl->text() != elided)
        m_nameLbl->setText(elided);
    m_nameLbl->setToolTip(m_name);
}

void PlaylistListItem::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_playlistId);
    }
    QWidget::mousePressEvent(event);
}

void PlaylistListItem::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    if (dark) {
        menu.setStyleSheet(
            "QMenu { background-color: rgba(40, 40, 50, 0.95); border: 1px solid rgba(255, 255, 255, 0.1); "
            "border-radius: 8px; padding: 4px; }"
            "QMenu::item { color: #e0e0e0; padding: 8px 24px; border-radius: 4px; }"
            "QMenu::item:selected { background-color: rgba(255, 255, 255, 0.1); }");
    } else {
        menu.setStyleSheet(
            "QMenu { background-color: rgba(255, 255, 255, 0.98); border: 1px solid rgba(64, 197, 241, 0.25); "
            "border-radius: 8px; padding: 4px; }"
            "QMenu::item { color: #182B45; padding: 8px 24px; border-radius: 4px; }"
            "QMenu::item:selected { background-color: rgba(22, 119, 184, 0.18); }");
    }

    if (m_mode == UserPlaylist) {
        QAction *renameAction = menu.addAction(QStringLiteral("重命名"));
        QAction *editDescAction = menu.addAction(QStringLiteral("修改描述"));
        QAction *deleteAction = menu.addAction(QStringLiteral("删除"));

        QAction *selected = menu.exec(event->globalPos());
        if (selected == renameAction) {
            emit renameRequested(m_playlistId);
        } else if (selected == editDescAction) {
            emit editDescriptionRequested(m_playlistId);
        } else if (selected == deleteAction) {
            emit deleteRequested(m_playlistId);
        }
    } else {
        // Favorite playlist: only unfavorite
        QAction *unfavAction = menu.addAction(I18n::instance().tr("uncollectPlaylist"));
        QAction *selected = menu.exec(event->globalPos());
        if (selected == unfavAction) {
            emit unfavoriteRequested(m_playlistId);
        }
    }
}

void PlaylistListItem::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Background + border-left
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    QRect r = rect();
    if (m_hovered) {
        QPainterPath path;
        path.addRoundedRect(r.adjusted(2, 2, -2, -2), 8, 8);
        painter.fillPath(path, dark ? QColor(255, 255, 255, 20) : QColor(22, 119, 184, 36));
    }
    // border-left 3px transparent (visible on hover/active in future)
    painter.fillRect(0, r.height() / 2 - 10, 3, 20, QColor(0, 0, 0, 1));
}

void PlaylistListItem::enterEvent(QEnterEvent *event) {
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void PlaylistListItem::leaveEvent(QEvent *event) {
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}
