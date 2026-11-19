/**
 * @file songcontextmenu.cpp
 * @brief 歌曲右键菜单 — 自定义 Popup 面板 + 图标行
 */

#include "songcontextmenu.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"
#include "ui/liquidglass.h"
#include "core/appearancesettings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QVariantAnimation>
#include <QCloseEvent>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QApplication>
#include <QScreen>

namespace {

constexpr int kMenuMinW = 200;
constexpr int kMenuMaxW = 280;
constexpr int kRowH = 44;
constexpr int kPad = 6;
constexpr int kIconSize = 18;
constexpr int kMenuMargin = 8;

class SongContextMenuPanel final : public QWidget
{
public:
    explicit SongContextMenuPanel(QWidget *parent) : QWidget(parent) {}
    void setMaterialProgress(qreal progress)
    {
        update();
        if (progress <= 0 && !AppearanceSettings::instance().liquidGlassEnabled())
            setBackdrop({}, {}, {});
    }

    void setBackdrop(const QImage &image, const QRect &bounds, const QRect &source)
    {
        m_backdrop.setImage(image, bounds);
        m_source = source;
        m_surface.clear();
        update();
    }

    bool hasGlass() const { return !m_backdrop.isEmpty(); }

    void setReveal(qreal progress, bool upwards)
    {
        m_reveal = progress;
        m_upwards = upwards;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = Theme::ThemeManager::instance().isDarkMode();
        const qreal t = qBound(0.0, m_reveal, 1.0);
        const QSize size(qRound(width() * (0.84 + 0.16 * t)),
                         qRound(height() * (0.60 + 0.40 * t)));
        const QPoint origin((width() - size.width()) / 2, m_upwards ? height() - size.height() : 0);
        const QRect shape(origin, size);
        const qreal radius = 12.0 + 7.0 * (1.0 - t);
        const qreal progress = hasGlass() ? AppearanceSettings::instance().liquidGlassProgress() : 0;
        if (progress < 1) {
            painter.setBrush(dark ? QColor(40, 40, 44) : QColor(Qt::white));
            painter.setPen(dark ? QColor(255, 255, 255, 25) : QColor(0, 0, 0, 20));
            painter.drawRoundedRect(QRectF(shape).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        }
        if (progress <= 0)
            return;
        painter.setOpacity(progress);
        m_surface.paint(painter, shape, m_backdrop, QRect(m_source.topLeft() + origin, size), radius, dark);
    }

private:
    LiquidGlass::Surface m_surface;
    LiquidGlass::Backdrop m_backdrop;
    QRect m_source;
    qreal m_reveal = 1.0;
    bool m_upwards = false;
};

class SongContextMenuRow final : public QWidget
{
public:
    explicit SongContextMenuRow(const SongContextMenuPopup::Entry &entry, QWidget *parent = nullptr)
        : QWidget(parent), m_entry(entry)
    {
        setFixedHeight(kRowH);
        setCursor(entry.disabled ? Qt::ArrowCursor : Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground, true);

        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(12, 0, 14, 0);
        lay->setSpacing(10);

        m_iconLbl = new QLabel(this);
        m_iconLbl->setFixedSize(kIconSize, kIconSize);
        m_iconLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        lay->addWidget(m_iconLbl);

        m_textLbl = new QLabel(entry.label, this);
        m_textLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        lay->addWidget(m_textLbl, 1);
    }

    const SongContextMenuPopup::Entry &entry() const { return m_entry; }

    void applyTheme();

protected:
    void enterEvent(QEnterEvent *) override
    {
        if (m_entry.disabled)
            return;
        m_hovered = true;
        update();
    }

    void leaveEvent(QEvent *) override
    {
        m_hovered = false;
        update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (m_entry.disabled) {
                // 置灰项点了不消失也不执行，避免"点了没反应还关掉菜单"的困惑
                event->accept();
                return;
            }
            if (m_entry.action)
                m_entry.action();
            if (auto *popup = window())
                popup->close();
        }
        event->accept();
    }

    void paintEvent(QPaintEvent *) override
    {
        if (!m_hovered || m_entry.disabled)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const bool dark = Theme::ThemeManager::instance().isDarkMode();
        QColor bg = dark ? QColor(255, 255, 255, 28) : QColor(22, 119, 184, 28);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(rect().adjusted(4, 2, -4, -2), 8, 8);
    }

private:
    SongContextMenuPopup::Entry m_entry;
    QLabel *m_iconLbl = nullptr;
    QLabel *m_textLbl = nullptr;
    bool m_hovered = false;
};

void SongContextMenuRow::applyTheme()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const bool off = m_entry.disabled;
    const QString fg = off ? (dark ? QStringLiteral("rgba(244,246,255,0.38)")
                                   : QStringLiteral("rgba(33,37,41,0.35)"))
                           : (dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#182B45"));
    const QColor ic = off ? (dark ? QColor(244, 246, 255, 90) : QColor(33, 37, 41, 85))
                          : (dark ? QColor(244, 246, 255, 210) : QColor(33, 37, 41, 210));
    if (m_iconLbl && m_entry.iconName)
        m_iconLbl->setPixmap(Icons::renderNamed(m_entry.iconName, kIconSize, ic));
    if (m_textLbl) {
        m_textLbl->setStyleSheet(
            QStringLiteral("QLabel { color: %1; font-size: 14px; font-weight: 500; }").arg(fg));
    }
}

} // namespace

void SongContextMenuPopup::showAt(QWidget *anchor, const QPoint &globalPos, const QList<Entry> &entries)
{
    showAt(anchor, globalPos, entries, false);
}

SongContextMenuPopup *SongContextMenuPopup::showAt(QWidget *anchor, const QPoint &globalPos,
                                                   const QList<Entry> &entries, bool returnInstance)
{
    if (!anchor || entries.isEmpty())
        return nullptr;

    auto *menu = new SongContextMenuPopup(anchor, entries);
    menu->placeAt(globalPos);
    menu->animateGlassOpen();
    menu->show();
    menu->raise();
    menu->activateWindow();
    return returnInstance ? menu : nullptr;
}

SongContextMenuPopup::SongContextMenuPopup(QWidget *anchor, const QList<Entry> &entries)
    : QWidget(anchor, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    , m_anchor(anchor)
    , m_entries(entries)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setAttribute(Qt::WA_TranslucentBackground, true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kMenuMargin, kMenuMargin, kMenuMargin, kMenuMargin);
    outer->setSpacing(0);

    m_panel = new SongContextMenuPanel(this);
    m_panel->setObjectName(QStringLiteral("songContextMenuPanel"));
    auto *shadow = new QGraphicsDropShadowEffect(m_panel);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 6);
    shadow->setColor(QColor(0, 0, 0, 70));
    m_panel->setGraphicsEffect(shadow);

    m_panelLay = new QVBoxLayout(m_panel);
    m_panelLay->setContentsMargins(kPad, kPad, kPad, kPad);
    m_panelLay->setSpacing(2);

    outer->addWidget(m_panel);

    applyEntriesToLayout(entries);
    applyTheme();

    connect(&Theme::ThemeManager::instance(), &Theme::ThemeManager::themeChanged, this,
            [this]() { applyTheme(); });
    connect(&AppearanceSettings::instance(), &AppearanceSettings::liquidGlassChanged,
            this, [this](bool enabled) {
                if (enabled)
                    refreshGlassBackdrop();
            });
    connect(&AppearanceSettings::instance(), &AppearanceSettings::liquidGlassProgressChanged,
            static_cast<SongContextMenuPanel *>(m_panel), &SongContextMenuPanel::setMaterialProgress);
}

/** 按 entries 重建行部件并重算固定宽高（构造与 setEntries 共用，保证两处口径一致） */
void SongContextMenuPopup::applyEntriesToLayout(const QList<Entry> &entries)
{
    if (!m_panelLay)
        return;

    // 先摘掉旧行：deleteLater 避免在事件处理过程中自删
    for (QWidget *w : m_rowWidgets) {
        if (!w)
            continue;
        m_panelLay->removeWidget(w);
        w->hide();
        w->deleteLater();
    }
    m_rowWidgets.clear();
    m_entries = entries;

    int maxTextW = 0;
    QFontMetrics fm(font());
    for (const Entry &e : entries)
        maxTextW = qMax(maxTextW, fm.horizontalAdvance(e.label));
    const int w = qBound(kMenuMinW, maxTextW + 12 + kIconSize + 10 + 14 + 14, kMenuMaxW);
    setFixedWidth(w + 16);

    for (const Entry &e : entries) {
        auto *row = new SongContextMenuRow(e, m_panel);
        row->applyTheme();
        m_panelLay->addWidget(row);
        m_rowWidgets.append(row);
    }

    setFixedHeight(entries.size() * kRowH + 2 * kPad + 16);
}

void SongContextMenuPopup::setEntries(const QList<Entry> &entries)
{
    if (entries.isEmpty())
        return;
    applyEntriesToLayout(entries);
    clampToScreen();
    update();
}

/** 把当前位置夹回屏幕可用区域（setEntries 改了宽高后需要重新收敛一次） */
void SongContextMenuPopup::clampToScreen()
{
    QScreen *screen = (m_anchor && m_anchor->window()) ? m_anchor->window()->screen()
                                                       : QApplication::primaryScreen();
    if (!screen)
        screen = QApplication::primaryScreen();
    QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    if (m_anchor && AppearanceSettings::instance().liquidGlassEnabled()) {
        QWidget *host = m_anchor->window();
        const QRect appArea = avail.intersected(QRect(host->mapToGlobal(QPoint()), host->size()));
        if (appArea.width() >= width() && appArea.height() >= height())
            avail = appArea;
    }

    int x = geometry().x();
    int y = geometry().y();
    if (x + width() > avail.right())
        x = avail.right() - width();
    if (y + height() > avail.bottom())
        y = avail.bottom() - height();
    if (x < avail.left())
        x = avail.left();
    if (y < avail.top())
        y = avail.top();
    move(x, y);
    refreshGlassBackdrop();
}

void SongContextMenuPopup::refreshGlassBackdrop()
{
    auto *panel = static_cast<SongContextMenuPanel *>(m_panel);
    if (!panel)
        return;
    QWidget *host = m_anchor ? m_anchor->window() : nullptr;
    if (host && AppearanceSettings::instance().liquidGlassEnabled()) {
        const QRect source = QRect(host->mapFromGlobal(pos()), size())
                                 .adjusted(kMenuMargin, kMenuMargin, -kMenuMargin, -kMenuMargin);
        const int margin = LiquidGlass::kCaptureMargin;
        const QRect area = source.adjusted(-margin, -margin, margin, margin).intersected(host->rect());
        // 菜单超出本应用窗口时，使用同主题的实色面板。
        if (host->rect().contains(source))
            panel->setBackdrop(LiquidGlass::capture(host, area), area, source);
        else
            panel->setBackdrop({}, {}, {});
    } else {
        panel->setBackdrop({}, {}, {});
    }
    applyTheme();
}

void SongContextMenuPopup::animateGlassOpen()
{
    if (!static_cast<SongContextMenuPanel *>(m_panel)->hasGlass())
        return;
    m_glassProgress = 0.0;
    animateGlass(1.0);
}

void SongContextMenuPopup::animateGlass(qreal end)
{
    if (!m_glassAnimation) {
        m_glassAnimation = new QVariantAnimation(this);
        connect(m_glassAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_glassProgress = value.toReal();
            static_cast<SongContextMenuPanel *>(m_panel)->setReveal(m_glassProgress, m_openUpwards);
            if (auto *fade = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect()))
                fade->setOpacity(qBound(0.0, m_glassProgress, 1.0));
            const qreal rowOpacity = qBound(0.0, (m_glassProgress - 0.86) / 0.14, 1.0);
            for (QWidget *row : m_rowWidgets) {
                if (auto *rowFade = qobject_cast<QGraphicsOpacityEffect *>(row->graphicsEffect()))
                    rowFade->setOpacity(rowOpacity);
            }
        });
        connect(m_glassAnimation, &QVariantAnimation::finished, this, [this]() {
            if (m_glassClosing) {
                m_closeReady = true;
                close();
                return;
            }
            setGraphicsEffect(nullptr);
            for (QWidget *row : m_rowWidgets)
                row->setGraphicsEffect(nullptr);
        });
    }
    m_glassAnimation->stop();
    auto *fade = new QGraphicsOpacityEffect(this);
    fade->setOpacity(m_glassProgress);
    setGraphicsEffect(fade);
    for (QWidget *row : m_rowWidgets) {
        auto *rowFade = new QGraphicsOpacityEffect(row);
        rowFade->setOpacity(qBound(0.0, (m_glassProgress - 0.86) / 0.14, 1.0));
        row->setGraphicsEffect(rowFade);
    }
    static_cast<SongContextMenuPanel *>(m_panel)->setReveal(m_glassProgress, m_openUpwards);
    m_glassAnimation->setDuration(m_glassClosing ? 150 : 270);
    m_glassAnimation->setStartValue(m_glassProgress);
    m_glassAnimation->setEndValue(end);
    m_glassAnimation->setEasingCurve(m_glassClosing ? QEasingCurve::InCubic : QEasingCurve::OutQuint);
    m_glassAnimation->start();
}

void SongContextMenuPopup::closeEvent(QCloseEvent *event)
{
    if (m_closeReady || !isVisible() || !static_cast<SongContextMenuPanel *>(m_panel)->hasGlass()) {
        QWidget::closeEvent(event);
        return;
    }
    event->ignore();
    if (!m_glassClosing) {
        m_glassClosing = true;
        animateGlass(0.0);
    }
}

void SongContextMenuPopup::applyTheme()
{
    m_panel->update();
    for (QWidget *w : m_rowWidgets)
        static_cast<SongContextMenuRow *>(w)->applyTheme();
}

void SongContextMenuPopup::placeAt(const QPoint &globalPos)
{
    // 先按锚点落位，再统一走 clampToScreen() 收敛边界：
    // 下边界贴不住时翻到锚点上方（"向上展开"），其余情况夹进可用区域。
    QScreen *screen = m_anchor && m_anchor->window() ? m_anchor->window()->screen()
                                                     : QApplication::primaryScreen();
    if (!screen)
        screen = QApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);

    int y = globalPos.y();
    m_openUpwards = y + height() > avail.bottom();
    if (m_openUpwards)
        y = globalPos.y() - height();
    move(globalPos.x(), y);
    clampToScreen();
}
