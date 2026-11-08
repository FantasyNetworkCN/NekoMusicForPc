/**
 * @file playlistcard.cpp
 * @brief 歌单卡片（网格版）实现
 *
 * paintEvent 绘制：圆角裁剪封面 + 底部渐变遮罩 + 歌曲数。
 * 悬停平滑上浮 4px + 樱花粉描边。
 */

#include "playlistcard.h"
#include "theme/theme.h"
#include "theme/thememanager.h"
#include "ui/svgicon.h"
#include "core/i18n.h"
#include "core/covercache.h"

#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QEnterEvent>
#include <QPropertyAnimation>
#include <QKeyEvent>

PlaylistCard::PlaylistCard(const PlaylistInfo &info, QWidget *parent)
    : QWidget(parent), m_info(info)
{
    setFixedSize(180, 236);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    const QString metaText = !info.reason.isEmpty() ? info.reason
                            : !info.creator.isEmpty() ? info.creator
                                                      : I18n::instance().tr("unknown");
    setAccessibleName(info.name + QStringLiteral(" · ") + metaText);
    QStringList tooltipParts{info.name};
    if (!info.creator.isEmpty())
        tooltipParts.append(info.creator);
    if (!info.reason.isEmpty())
        tooltipParts.append(info.reason);
    if (!info.description.isEmpty())
        tooltipParts.append(info.description);
    setToolTip(tooltipParts.join(QLatin1Char('\n')));
    setAttribute(Qt::WA_StyledBackground, false);
    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setDuration(160);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    // 封面占位（由 paintEvent 绘制实际图片）
    auto *coverSpacer = new QWidget(this);
    coverSpacer->setFixedSize(Theme::kCoverSmall, Theme::kCoverSmall);
    lay->addWidget(coverSpacer, 0, Qt::AlignHCenter);

    // 歌单名
    m_name = new QLabel(this);
    m_name->setObjectName("plName");
    m_name->setFixedWidth(160);
    QFontMetrics fm(m_name->font());
    m_name->setText(fm.elidedText(m_info.name, Qt::ElideRight, 160));
    m_name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    lay->addWidget(m_name);

    m_meta = new QLabel(this);
    m_meta->setObjectName("plMeta");
    m_meta->setFixedWidth(160);
    m_meta->setText(m_meta->fontMetrics().elidedText(metaText, Qt::ElideRight, 160));
    m_meta->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    lay->addWidget(m_meta);
    lay->addStretch();

    loadCover();
}

int PlaylistCard::playlistId() const { return m_info.id; }

void PlaylistCard::loadCover()
{
    // 渐变占位封面
    QPixmap ph(Theme::kCoverSmall, Theme::kCoverSmall);
    ph.fill(Qt::transparent);
    QPainter p(&ph);
    QPainterPath pp;
    pp.addRoundedRect(0, 0, Theme::kCoverSmall, Theme::kCoverSmall,
                      Theme::kCoverRadius, Theme::kCoverRadius);
    QLinearGradient g(0, 0, Theme::kCoverSmall, Theme::kCoverSmall);
    g.setColorAt(0.0, QColor(28, 31, 38));
    g.setColorAt(1.0, QColor(64, 197, 241));
    p.fillPath(pp, g);
    p.setPen(Qt::NoPen);
    p.drawPixmap(QRect((Theme::kCoverSmall-40)/2, (Theme::kCoverSmall-40)/2, 40, 40),
                 Icons::renderNamed("Music", 40, QColor(255, 255, 255, 176)));
    p.end();
    m_coverPixmap = ph;

    // 异步加载封面（先查缓存）
    if (!m_info.coverUrl.isEmpty()) {
        const QString musicId = CoverCache::musicIdFromCoverUrl(m_info.coverUrl);
        if (musicId.isEmpty())
            return;

        QPixmap cached = CoverCache::instance()->get(musicId);
        if (!cached.isNull()) {
            int s = qMin(cached.width(), cached.height());
            m_coverPixmap = cached.copy((cached.width()-s)/2, (cached.height()-s)/2, s, s)
                .scaled(Theme::kCoverSmall, Theme::kCoverSmall,
                        Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
            update();
            return;
        }

        disconnect(m_coverConn);
        m_coverConn = connect(CoverCache::instance(), &CoverCache::coverLoaded, this,
                [this, musicId](const QString &id, const QPixmap &pix) {
            if (id == musicId) {
                int s = qMin(pix.width(), pix.height());
                m_coverPixmap = pix.copy((pix.width()-s)/2, (pix.height()-s)/2, s, s)
                    .scaled(Theme::kCoverSmall, Theme::kCoverSmall,
                            Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
                update();
            }
        });
        CoverCache::instance()->fetchCover(musicId, m_info.coverUrl);
    }
}

void PlaylistCard::animateHover(bool enter)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(enter ? 1.0 : 0.0);
    m_hoverAnimation->start();
}

void PlaylistCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const bool dark = Theme::ThemeManager::instance().isDarkMode();
    p.setPen(QPen(hasFocus() ? QColor(Theme::kLavender) : QColor(128, 180, 220, int(45 * m_hoverProgress)), 1));
    p.setBrush(dark ? QColor(170, 215, 250, int(14 * m_hoverProgress))
                    : QColor(255, 255, 255, int(150 * m_hoverProgress)));
    p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 18, 18);

    // 圆角裁剪封面
    QRect coverRect(10, 10, Theme::kCoverSmall, Theme::kCoverSmall);
    QPainterPath clip;
    clip.addRoundedRect(coverRect, Theme::kCoverRadius, Theme::kCoverRadius);
    p.setClipPath(clip);
    p.drawPixmap(coverRect, m_coverPixmap);

    // 底部渐变遮罩
    QLinearGradient mask(coverRect.bottomLeft(), coverRect.topLeft());
    mask.setColorAt(0.0, QColor(12, 14, 18, 188));
    mask.setColorAt(0.35, QColor(12, 14, 18, 92));
    mask.setColorAt(1.0, QColor(14, 14, 28, 0));
    p.fillRect(coverRect, mask);
    p.setClipping(false);

    // 歌曲数量
    if (m_info.musicCount > 0) {
        p.setPen(QColor(248, 249, 251, 210));
        p.setFont(QFont(QString(), 10, QFont::Bold));
        QRect countRect = coverRect.adjusted(0, 0, -8, -6);
        p.drawText(countRect, Qt::AlignRight | Qt::AlignBottom,
                   QString::number(m_info.musicCount) + I18n::instance().tr("musicCount"));
    }

    if (m_hoverProgress > 0 || hasFocus()) {
        p.setOpacity(hasFocus() ? 1 : m_hoverProgress);
        const QRect button(coverRect.right() - 46, coverRect.bottom() - 46, 36, 36);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(230, 248, 255));
        p.drawEllipse(button);
        p.drawPixmap(button.adjusted(9, 9, -9, -9),
                     Icons::renderNamed("Play", 18, QColor(20, 52, 78)));
    }
}

void PlaylistCard::enterEvent(QEnterEvent *e)
{
    m_hovered = true;
    animateHover(true);
    update();
    QWidget::enterEvent(e);
}

void PlaylistCard::leaveEvent(QEvent *e)
{
    m_hovered = false;
    animateHover(false);
    update();
    QWidget::leaveEvent(e);
}

void PlaylistCard::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        const QRect coverRect(10, 10, Theme::kCoverSmall, Theme::kCoverSmall);
        const QRect playButton(coverRect.right() - 46, coverRect.bottom() - 46, 36, 36);
        if (m_hoverProgress > 0.5 && playButton.contains(e->pos()))
            emit playClicked(m_info.id);
        else
            emit clicked(m_info.id);
    }
    QWidget::mousePressEvent(e);
}

void PlaylistCard::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space) {
        emit playClicked(m_info.id);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        emit clicked(m_info.id);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PlaylistCard::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (m_name && (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange))
        m_name->setText(m_name->fontMetrics().elidedText(m_info.name, Qt::ElideRight, 160));
    if (m_meta && (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange)) {
        const QString metaText = !m_info.reason.isEmpty() ? m_info.reason
                                : !m_info.creator.isEmpty() ? m_info.creator
                                                            : I18n::instance().tr("unknown");
        m_meta->setText(m_meta->fontMetrics().elidedText(metaText, Qt::ElideRight, 160));
    }
}
