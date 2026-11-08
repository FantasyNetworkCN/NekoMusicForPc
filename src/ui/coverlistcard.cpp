/**
 * @file coverlistcard.cpp
 * @brief SPlayer CoverList playlist cover-item
 */

#include "coverlistcard.h"
#include "core/covercache.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QResizeEvent>

namespace {

QString formatCompactCount(int n)
{
    if (n >= 100000000)
        return QString::number(n / 100000000.0, 'f', 1) + QStringLiteral("亿");
    if (n >= 10000)
        return QString::number(n / 10000.0, 'f', 1) + QStringLiteral("万");
    return QString::number(n);
}

} // namespace

CoverGridHost::CoverGridHost(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("coverGridHost"));
}

void CoverGridHost::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (onResized)
        onResized();
}

CoverListCard::CoverListCard(const CoverListItemData &data, QWidget *parent)
    : QWidget(parent), m_data(data)
{
    setObjectName(QStringLiteral("coverListCard"));
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_StyledBackground, true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_coverWrap = new QWidget(this);
    m_coverWrap->setObjectName(QStringLiteral("coverListCover"));
    m_coverWrap->setAttribute(Qt::WA_StyledBackground, false);
    m_coverWrap->installEventFilter(this);

    m_playCountRow = new QLabel(m_coverWrap);
    m_playCountRow->setAttribute(Qt::WA_TransparentForMouseEvents);
    if (m_data.musicCount > 0) {
        auto *pcLay = new QHBoxLayout(m_playCountRow);
        pcLay->setContentsMargins(0, 0, 0, 0);
        pcLay->setSpacing(4);
        auto *pcIcon = new QLabel(m_playCountRow);
        pcIcon->setPixmap(Icons::renderNamed("Play", 16, Qt::white));
        pcIcon->setFixedSize(16, 16);
        pcLay->addWidget(pcIcon);
        auto *pcNum = new QLabel(formatCompactCount(m_data.musicCount), m_playCountRow);
        pcNum->setStyleSheet(QStringLiteral("QLabel { color: #fff; font-size: 12px; font-weight: 700; }"));
        pcLay->addWidget(pcNum);
    } else {
        m_playCountRow->hide();
    }

    m_descOverlay = new QLabel(m_coverWrap);
    m_descOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_descOverlay->setWordWrap(true);
    m_descOverlay->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
    const QString desc = m_data.description.trimmed();
    if (desc.isEmpty()) {
        m_descOverlay->hide();
    } else {
        QFontMetrics fm(m_descOverlay->font());
        m_descOverlay->setText(fm.elidedText(desc, Qt::ElideRight, 200));
        m_descOverlay->setStyleSheet(QStringLiteral(
            "QLabel { color: #fff; font-size: 12px; padding: 40px 52px 12px 12px; "
            "background: transparent; }"));
    }

    m_playBtn = new QPushButton(m_coverWrap);
    m_playBtn->setObjectName(QStringLiteral("coverListPlayBtn"));
    m_playBtn->setFixedSize(42, 42);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    m_playBtn->setIcon(Icons::renderNamed("Play", 32, Qt::white));
    m_playBtn->setIconSize(QSize(32, 32));
    m_playBtn->hide();
    connect(m_playBtn, &QPushButton::clicked, this, [this]() {
        emit playClicked(m_data.id);
    });

    root->addWidget(m_coverWrap);

    auto *dataWrap = new QWidget(this);
    dataWrap->setObjectName(QStringLiteral("coverListData"));
    auto *dataLay = new QVBoxLayout(dataWrap);
    dataLay->setContentsMargins(12, 12, 12, 12);
    dataLay->setSpacing(4);

    m_nameLbl = new QLabel(dataWrap);
    m_nameLbl->setWordWrap(true);
    m_nameLbl->setMaximumHeight(44);
    m_nameLbl->setText(m_data.name);
    dataLay->addWidget(m_nameLbl);

    const QString creator = m_data.creator.trimmed().isEmpty()
                                ? I18n::instance().tr(QStringLiteral("unknown"))
                                : m_data.creator.trimmed();
    m_creatorLbl = new QLabel(creator, dataWrap);
    dataLay->addWidget(m_creatorLbl);

    root->addWidget(dataWrap);

    loadCover();
    applyTheme();
    setCellWidth(160);
}

void CoverListCard::setCellWidth(int width)
{
    m_cellWidth = qMax(120, width);
    m_coverSide = m_cellWidth;
    setFixedWidth(m_cellWidth);
    setFixedHeight(m_coverSide + 88);
    updateCoverGeometry();
    update();
}

void CoverListCard::applyTheme()
{
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    const QString titleFg = dark ? QString::fromUtf8(Theme::kTextMain) : QStringLiteral("#182B45");
    const QString metaFg = dark ? QString::fromUtf8(Theme::kTextSub) : QStringLiteral("rgba(33,37,41,0.65)");

    if (m_nameLbl) {
        m_nameLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 16px; font-weight: 600; color: %1; }").arg(titleFg));
    }
    if (m_creatorLbl) {
        m_creatorLbl->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 13px; color: %1; }").arg(metaFg));
    }
    updatePlayButtonStyle();
}

void CoverListCard::updatePlayButtonStyle()
{
    if (!m_playBtn)
        return;
    m_playBtn->setStyleSheet(QStringLiteral(
        "QPushButton#coverListPlayBtn {"
        "  background: rgba(255,255,255,0.4);"
        "  border: none;"
        "  border-radius: 21px;"
        "}"
        "QPushButton#coverListPlayBtn:hover {"
        "  background: rgba(255,255,255,0.55);"
        "}"
        "QPushButton#coverListPlayBtn:pressed {"
        "  background: rgba(255,255,255,0.25);"
        "}"));
}

void CoverListCard::loadCover()
{
    QPixmap ph(m_coverSide, m_coverSide);
    ph.fill(Qt::transparent);
    QPainter p(&ph);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(0, 0, m_coverSide, m_coverSide, 16, 16);
    QLinearGradient g(0, 0, m_coverSide, m_coverSide);
    g.setColorAt(0, QColor(255, 143, 163));
    g.setColorAt(1, QColor(22, 119, 184));
    p.fillPath(path, g);
    p.setClipPath(path);
    p.drawPixmap((m_coverSide - 40) / 2, (m_coverSide - 40) / 2,
                 Icons::renderNamed("Music", 40, QColor(255, 255, 255, 140)));
    p.end();
    m_coverPixmap = ph;

    if (m_data.coverUrl.isEmpty())
        return;

    const QString cacheId = CoverCache::musicIdFromCoverUrl(m_data.coverUrl);
    if (cacheId.isEmpty())
        return;

    QPixmap cached = CoverCache::instance()->get(cacheId);
    if (!cached.isNull()) {
        int s = qMin(cached.width(), cached.height());
        m_coverPixmap =
            cached.copy((cached.width() - s) / 2, (cached.height() - s) / 2, s, s)
                .scaled(m_coverSide, m_coverSide, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        update();
        return;
    }

    disconnect(m_coverConn);
    m_coverConn = connect(CoverCache::instance(), &CoverCache::coverLoaded, this,
                          [this, cacheId](const QString &id, const QPixmap &pix) {
                              if (id != cacheId)
                                  return;
                              int s = qMin(pix.width(), pix.height());
                              m_coverPixmap =
                                  pix.copy((pix.width() - s) / 2, (pix.height() - s) / 2, s, s)
                                      .scaled(m_coverSide, m_coverSide, Qt::KeepAspectRatioByExpanding,
                                              Qt::SmoothTransformation);
                              update();
                          });
    CoverCache::instance()->fetchCover(cacheId, m_data.coverUrl);
}

void CoverListCard::updateCoverGeometry()
{
    if (!m_coverWrap)
        return;
    m_coverWrap->setFixedSize(m_cellWidth, m_coverSide);
    if (m_playCountRow)
        m_playCountRow->setGeometry(m_coverSide - 88, 10, 76, 22);
    if (m_descOverlay)
        m_descOverlay->setGeometry(0, m_coverSide / 2, m_coverSide, m_coverSide / 2);
    if (m_playBtn)
        m_playBtn->setGeometry(m_coverSide - 52, m_coverSide - 52, 42, 42);
}

void CoverListCard::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateCoverGeometry();
}

void CoverListCard::setHovered(bool hovered)
{
    if (m_hovered == hovered)
        return;
    m_hovered = hovered;
    if (m_playBtn)
        m_playBtn->setVisible(hovered);
    if (m_descOverlay)
        m_descOverlay->setVisible(hovered && !m_data.description.trimmed().isEmpty());
    update();
}

void CoverListCard::enterEvent(QEnterEvent *event)
{
    setHovered(true);
    QWidget::enterEvent(event);
}

void CoverListCard::leaveEvent(QEvent *event)
{
    setHovered(false);
    QWidget::leaveEvent(event);
}

bool CoverListCard::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_coverWrap) {
        if (event->type() == QEvent::Enter)
            setHovered(true);
        else if (event->type() == QEvent::Leave)
            setHovered(false);
    }
    return QWidget::eventFilter(watched, event);
}

void CoverListCard::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const bool onPlayBtn = m_playBtn && m_playBtn->isVisible()
                               && m_playBtn->geometry().contains(event->pos());
        if (!onPlayBtn)
            emit clicked(m_data.id);
    }
    QWidget::mousePressEvent(event);
}

void CoverListCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (m_hovered) {
        p.fillRect(rect(), QColor(22, 119, 184, 31));
    }

    const QRect coverRect(0, 0, m_coverSide, m_coverSide);
    QPainterPath clip;
    clip.addRoundedRect(coverRect, 16, 16);
    p.setClipPath(clip);
    p.drawPixmap(coverRect, m_coverPixmap);

    QLinearGradient topMask(coverRect.topLeft(), QPointF(coverRect.left(), coverRect.top() + coverRect.height() * 0.3));
    topMask.setColorAt(0, QColor(0, 0, 0, 76));
    topMask.setColorAt(1, QColor(0, 0, 0, 0));
    p.fillRect(coverRect, topMask);

    if (m_hovered && !m_data.description.trimmed().isEmpty()) {
        QLinearGradient bottomMask(QPointF(coverRect.left(), coverRect.bottom()),
                                 QPointF(coverRect.left(), coverRect.center().y()));
        bottomMask.setColorAt(0, QColor(0, 0, 0, 153));
        bottomMask.setColorAt(1, QColor(0, 0, 0, 0));
        p.fillRect(coverRect, bottomMask);
    }

    p.setClipping(false);

    if (m_hovered) {
        QPainterPath border;
        border.addRoundedRect(QRectF(coverRect).adjusted(0.5, 0.5, -0.5, -0.5), 16, 16);
        p.setPen(QPen(QColor(255, 183, 197, 90), 1.5));
        p.drawPath(border);
    }
}
