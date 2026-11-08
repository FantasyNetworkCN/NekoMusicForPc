#include "amlllyricsview.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QTextOption>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr qreal kHorizontalPadding = 30.0;
constexpr qreal kLineGap = 18.0;
constexpr qreal kSubLineGap = 5.0;
constexpr qreal kAnchorRatio = 0.47;
// 歌词切行要跟得上音乐，不能像页面滚动一样拖很久。
// 这组参数接近临界阻尼，目标切换后约 250~320ms 收敛，避免明显回弹。
constexpr qreal kSpringStiffness = 420.0;
constexpr qreal kSpringDamping = 42.0;
constexpr qint64 kUserScrollPauseMs = 2400;
constexpr qreal kEdgeFadePx = 92.0;
constexpr qreal kActiveScale = 1.035;
constexpr qreal kNearestScale = 0.992;
constexpr qreal kDistantScale = 0.955;

struct TextBlock {
    QString text;
    QFont font;
    QColor color;
    std::unique_ptr<QTextLayout> layout;
    qreal height = 0;
};

struct LineLayout {
    qreal top = 0;
    qreal height = 0;
    qreal scale = 0.97;
    TextBlock main;
    TextBlock romanization;
    TextBlock translation;
};

QTextLayout *buildTextBlock(TextBlock &block, const QString &text,
                            const QFont &font, const QColor &color, qreal width)
{
    block.text = text.trimmed();
    block.font = font;
    block.color = color;
    block.layout.reset();
    block.height = 0;
    if (block.text.isEmpty() || width <= 1)
        return nullptr;

    block.layout = std::make_unique<QTextLayout>(block.text, block.font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    // AMLL 的歌词不是左对齐段落：每一行都应该围绕焦点轴居中，
    // 长句仍然按可用宽度换行，避免窗口变窄后整块文字贴在左边。
    option.setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    block.layout->setTextOption(option);

    qreal y = 0;
    block.layout->beginLayout();
    while (true) {
        QTextLine line = block.layout->createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    block.layout->endLayout();
    block.height = y;
    return block.layout.get();
}

qreal smoothStep(qreal value)
{
    const qreal t = std::clamp<qreal>(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

QColor blendColor(const QColor &from, const QColor &to, qreal progress)
{
    const qreal t = std::clamp<qreal>(progress, 0.0, 1.0);
    return QColor(
        qRound(from.red() + (to.red() - from.red()) * t),
        qRound(from.green() + (to.green() - from.green()) * t),
        qRound(from.blue() + (to.blue() - from.blue()) * t),
        qRound(from.alpha() + (to.alpha() - from.alpha()) * t));
}

void appendFormat(QList<QTextLayout::FormatRange> &ranges, int start,
                  int length, const QColor &color)
{
    if (length <= 0)
        return;
    if (!ranges.isEmpty()) {
        auto &last = ranges.last();
        if (last.start + last.length == start
            && last.format.foreground().color() == color) {
            last.length += length;
            return;
        }
    }
    QTextLayout::FormatRange range;
    range.start = start;
    range.length = length;
    range.format.setForeground(color);
    ranges.append(range);
}

QList<QTextLayout::FormatRange> karaokeFormats(const TextBlock &block,
                                               const QVector<LyricWord> &words,
                                               qint64 positionMs,
                                               const QColor &active,
                                               const QColor &dim)
{
    QList<QTextLayout::FormatRange> ranges;
    if (!block.layout || words.isEmpty())
        return ranges;

    int searchFrom = 0;
    for (const LyricWord &word : words) {
        if (word.text.isEmpty())
            continue;
        const int wordStart = block.text.indexOf(word.text, searchFrom);
        if (wordStart < 0)
            continue;
        const int wordLength = word.text.size();
        searchFrom = wordStart + wordLength;

        if (positionMs <= word.startMs)
            continue;
        qint64 endMs = word.endMs;
        if (endMs <= word.startMs) {
            // 部分后端数据只有逐字起点。用下一个字/行结束时间补齐，
            // 否则 endMs=0 会导致整句一出现就瞬间扫亮。
            for (const LyricWord &next : words) {
                if (next.startMs > word.startMs) {
                    endMs = next.startMs;
                    break;
                }
            }
            if (endMs <= word.startMs)
                endMs = word.startMs + 140;
        }
        if (positionMs >= endMs) {
            appendFormat(ranges, wordStart, wordLength, active);
            continue;
        }

        int glyphCount = 0;
        QTextBoundaryFinder countBoundary(QTextBoundaryFinder::Grapheme, word.text);
        for (int offset = 0; offset < wordLength; ++glyphCount) {
            countBoundary.setPosition(offset);
            int nextOffset = countBoundary.toNextBoundary();
            if (nextOffset <= offset || nextOffset > wordLength)
                nextOffset = offset + (word.text.at(offset).isHighSurrogate() ? 2 : 1);
            offset = nextOffset;
        }
        const qreal wordProgress = std::clamp<qreal>(
            qreal(positionMs - word.startMs)
                / qMax<qint64>(1, endMs - word.startMs),
            0.0, 1.0);

        QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, word.text);
        int offset = 0;
        int glyphIndex = 0;
        while (offset < wordLength) {
            boundary.setPosition(offset);
            int nextOffset = boundary.toNextBoundary();
            if (nextOffset <= offset || nextOffset > wordLength)
                nextOffset = offset + (word.text.at(offset).isHighSurrogate() ? 2 : 1);
            const int codeUnits = nextOffset - offset;
            const qreal glyphStart = qreal(glyphIndex) / qMax(1, glyphCount);
            const qreal glyphEnd = qreal(glyphIndex + 1) / qMax(1, glyphCount);
            const qreal glyphProgress = std::clamp<qreal>(
                (wordProgress - glyphStart)
                    / std::max<qreal>(0.0001, glyphEnd - glyphStart),
                0.0, 1.0);
            appendFormat(ranges, wordStart + offset, codeUnits,
                         blendColor(dim, active, smoothStep(glyphProgress)));
            offset = nextOffset;
        }
    }
    return ranges;
}

} // namespace

struct AmllLyricsView::Impl {
    QVector<LyricLine> lyrics;
    std::vector<LineLayout> lines;
    QColor activeColor = QColor(Qt::white);
    QColor dimColor = QColor(255, 255, 255, 77);
    QColor secondaryColor = QColor(255, 255, 255, 153);
    int mainFontPx = 42;
    QString emptyText;

    qint64 positionMs = 0;
    qint64 lastPositionMs = 0;
    qint64 userScrollPausedUntilMs = 0;
    int activeIndex = -1;
    bool playing = false;

    qreal scroll = 0;
    qreal scrollTarget = 0;
    qreal scrollVelocity = 0;
    QElapsedTimer frameClock;
};

AmllLyricsView::AmllLyricsView(QWidget *parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_impl->frameClock.start();
}

AmllLyricsView::~AmllLyricsView() = default;

void AmllLyricsView::setLyrics(const QVector<LyricLine> &lyrics)
{
    m_impl->lyrics = lyrics;
    m_impl->positionMs = 0;
    m_impl->lastPositionMs = 0;
    m_impl->scroll = 0;
    m_impl->scrollTarget = 0;
    m_impl->scrollVelocity = 0;
    m_impl->activeIndex = -1;

    const int viewWidth = width();
    if (viewWidth <= 0) {
        m_impl->lines.clear();
        update();
        return;
    }

    const qreal contentWidth = qMax<qreal>(120, viewWidth - kHorizontalPadding * 2);
    QFont mainFont = font();
    mainFont.setPixelSize(m_impl->mainFontPx);
    mainFont.setWeight(QFont::Medium);
    QFont subFont = font();
    subFont.setPixelSize(qMax(13, qRound(m_impl->mainFontPx * 0.48)));

    m_impl->lines.clear();
    m_impl->lines.reserve(static_cast<std::size_t>(m_impl->lyrics.size()));
    // 让第一句在没有滚动时也落在焦点线附近，前后留出足够呼吸空间。
    qreal y = qMax<qreal>(56, height() * kAnchorRatio);
    for (const LyricLine &lyric : m_impl->lyrics) {
        LineLayout line;
        line.top = y;
        buildTextBlock(line.main, lyric.text, mainFont, m_impl->activeColor, contentWidth);

        QFont romanFont = subFont;
        romanFont.setItalic(true);
        buildTextBlock(line.romanization, lyric.romanization,
                       romanFont, m_impl->secondaryColor, contentWidth);
        buildTextBlock(line.translation, lyric.translation,
                       subFont, m_impl->secondaryColor, contentWidth);

        line.height = line.main.height;
        if (line.romanization.height > 0) {
            line.height += kSubLineGap + line.romanization.height;
        }
        if (line.translation.height > 0) {
            line.height += kSubLineGap + line.translation.height;
        }
        line.height = qMax<qreal>(mainFont.pixelSize() * 1.25, line.height);
        y += line.height + kLineGap;
        m_impl->lines.push_back(std::move(line));
    }

    const qreal contentBottom = y + qMax<qreal>(80, height() * (1.0 - kAnchorRatio));
    const qreal maxScroll = qMax<qreal>(0, contentBottom - height());
    if (!m_impl->lines.empty()) {
        const LineLayout &line = m_impl->lines.front();
        m_impl->scrollTarget = std::clamp(
            line.top + line.height * 0.5 - height() * kAnchorRatio,
            0.0, maxScroll);
    }
    m_impl->scroll = m_impl->scrollTarget;
    update();
}

void AmllLyricsView::setPosition(qint64 positionMs, bool seek)
{
    if (m_impl->lyrics.isEmpty() || height() <= 0)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool jumped = seek
        || (m_impl->lastPositionMs > 0
            && (positionMs < m_impl->lastPositionMs - 300
                || positionMs > m_impl->lastPositionMs + 1800));
    m_impl->positionMs = qMax<qint64>(0, positionMs);
    m_impl->lastPositionMs = m_impl->positionMs;

    const auto firstAfter = std::upper_bound(
        m_impl->lyrics.cbegin(), m_impl->lyrics.cend(), m_impl->positionMs,
        [](qint64 value, const LyricLine &line) { return value < line.time; });
    const int active = firstAfter == m_impl->lyrics.cbegin()
        ? -1
        : int(std::distance(m_impl->lyrics.cbegin(), firstAfter)) - 1;
    const bool activeChanged = active != m_impl->activeIndex;
    m_impl->activeIndex = active;

    if (!m_impl->lines.empty()) {
        const int targetIndex = qMax(0, active);
        const LineLayout &line = m_impl->lines.at(static_cast<std::size_t>(targetIndex));
        const qreal contentBottom = m_impl->lines.back().top
            + m_impl->lines.back().height
            + qMax<qreal>(80, height() * (1.0 - kAnchorRatio));
        const qreal maxScroll = qMax<qreal>(0, contentBottom - height());
        m_impl->scrollTarget = std::clamp(
            line.top + line.height * 0.5 - height() * kAnchorRatio,
            0.0, maxScroll);
        if (jumped)
            m_impl->scroll = m_impl->scrollTarget;
    }

    const qint64 elapsedNs = m_impl->frameClock.restart();
    const qreal dt = std::clamp<qreal>(
        qreal(elapsedNs) / 1'000'000'000.0, 0.001, 0.05);
    if (now >= m_impl->userScrollPausedUntilMs) {
        int substeps = qMax(1, qCeil(dt / 0.004));
        const qreal step = dt / substeps;
        for (int i = 0; i < substeps; ++i) {
            const qreal acceleration =
                kSpringStiffness * (m_impl->scrollTarget - m_impl->scroll)
                - kSpringDamping * m_impl->scrollVelocity;
            m_impl->scrollVelocity += acceleration * step;
            m_impl->scroll += m_impl->scrollVelocity * step;
        }
        if (qAbs(m_impl->scrollTarget - m_impl->scroll) < 0.05
            && qAbs(m_impl->scrollVelocity) < 0.05) {
            m_impl->scroll = m_impl->scrollTarget;
            m_impl->scrollVelocity = 0;
        }
    }

    const qreal scaleStep = std::min<qreal>(1.0, dt * 13.0);
    for (int i = 0; i < static_cast<int>(m_impl->lines.size()); ++i) {
        LineLayout &line = m_impl->lines.at(static_cast<std::size_t>(i));
        const int distance = active < 0 ? i : qAbs(i - active);
        const qreal targetScale = active >= 0 && i == active
            ? kActiveScale
            : distance <= 1 ? kNearestScale : kDistantScale;
        line.scale += (targetScale - line.scale) * scaleStep;
    }

    if (activeChanged || jumped || m_impl->playing)
        update();
}

void AmllLyricsView::setPlaying(bool playing)
{
    if (m_impl->playing == playing)
        return;
    m_impl->playing = playing;
    m_impl->frameClock.restart();
    update();
}

void AmllLyricsView::setTextStyle(const QColor &active, const QColor &dim,
                                  const QColor &secondary, int mainFontPx)
{
    const int safeFontPx = qBound(24, mainFontPx, 64);
    if (m_impl->activeColor == active && m_impl->dimColor == dim
        && m_impl->secondaryColor == secondary
        && m_impl->mainFontPx == safeFontPx) {
        return;
    }
    m_impl->activeColor = active;
    m_impl->dimColor = dim;
    m_impl->secondaryColor = secondary;
    m_impl->mainFontPx = safeFontPx;
    const QVector<LyricLine> lyrics = m_impl->lyrics;
    const qint64 position = m_impl->positionMs;
    setLyrics(lyrics);
    setPosition(position, true);
}

void AmllLyricsView::setEmptyText(const QString &text)
{
    if (m_impl->emptyText == text)
        return;
    m_impl->emptyText = text;
    update();
}

void AmllLyricsView::resetView()
{
    m_impl->scroll = m_impl->scrollTarget;
    m_impl->scrollVelocity = 0;
    update();
}

void AmllLyricsView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    if (m_impl->lines.empty()) {
        const QFont iconFont = [this]() {
            QFont f = font();
            f.setPixelSize(56);
            return f;
        }();
        painter.setFont(iconFont);
        painter.setPen(m_impl->dimColor);
        const QRect iconRect(0, height() / 2 - 66, width(), 64);
        painter.drawText(iconRect, Qt::AlignCenter, QString::fromUtf8("♫"));
        painter.setFont(font());
        painter.setPen(m_impl->dimColor);
        painter.drawText(QRect(0, height() / 2 + 2, width(), 40),
                         Qt::AlignCenter, m_impl->emptyText);
        return;
    }

    const int active = m_impl->activeIndex;
    const qreal x = kHorizontalPadding;
    for (int i = 0; i < static_cast<int>(m_impl->lines.size()); ++i) {
        const LineLayout &line = m_impl->lines.at(static_cast<std::size_t>(i));
        const qreal y = line.top - m_impl->scroll;
        if (y + line.height < -40 || y > height() + 40)
            continue;

        const qreal centerY = y + line.height * 0.5;
        const qreal edgeFade = std::clamp<qreal>(
            std::min((centerY + line.height * 0.5) / kEdgeFadePx,
                     (height() - centerY + line.height * 0.5) / kEdgeFadePx),
            0.0, 1.0);
        const qreal distance = active < 0 ? qreal(i) : qAbs(qreal(i - active));
        const qreal focusOpacity = active >= 0 && i == active
            ? 1.0
            : std::clamp<qreal>(0.86 - distance * 0.10, 0.52, 0.82);
        const qreal opacity = edgeFade * focusOpacity;

        painter.save();
        painter.setOpacity(opacity);
        painter.translate(x, y + line.height * 0.5);
        painter.scale(line.scale, line.scale);
        painter.translate(0, -line.height * 0.5);

        if (active >= 0 && i == active) {
            QColor highlight = m_impl->activeColor;
            highlight.setAlpha(24);
            painter.setPen(Qt::NoPen);
            painter.setBrush(highlight);
            painter.drawRoundedRect(QRectF(-14, -8, width() - kHorizontalPadding * 2 + 28,
                                           line.height + 16), 16, 16);
        }

        qreal blockY = 0;
        if (line.main.layout) {
            // 有逐字时间轴时，未唱到的字必须保持 dimColor，不能先把整行
            // 设成高亮色，否则 FormatRange 只会“加亮”而不会形成扫亮效果。
            const bool hasKaraoke = i == active && !m_impl->lyrics.at(i).words.isEmpty();
            painter.setPen(i == active && !hasKaraoke
                               ? m_impl->activeColor
                               : m_impl->dimColor);
            const auto formats = i == active
                ? karaokeFormats(line.main, m_impl->lyrics.at(i).words,
                                 m_impl->positionMs, m_impl->activeColor, m_impl->dimColor)
                : QList<QTextLayout::FormatRange>();
            line.main.layout->draw(&painter, QPointF(0, blockY), formats);
            blockY += line.main.height;
        }
        if (line.romanization.layout) {
            blockY += kSubLineGap;
            painter.setPen(i == active ? m_impl->secondaryColor : m_impl->dimColor);
            line.romanization.layout->draw(&painter, QPointF(0, blockY));
            blockY += line.romanization.height;
        }
        if (line.translation.layout) {
            blockY += kSubLineGap;
            painter.setPen(i == active ? m_impl->secondaryColor : m_impl->dimColor);
            line.translation.layout->draw(&painter, QPointF(0, blockY));
        }
        painter.restore();
    }
}

void AmllLyricsView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const QVector<LyricLine> lyrics = m_impl->lyrics;
    const qint64 position = m_impl->positionMs;
    setLyrics(lyrics);
    setPosition(position, true);
}

void AmllLyricsView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_impl->frameClock.restart();
}

void AmllLyricsView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_impl->scrollVelocity = 0;
}

void AmllLyricsView::wheelEvent(QWheelEvent *event)
{
    if (m_impl->lines.empty()) {
        QWidget::wheelEvent(event);
        return;
    }
    const qreal maxScroll = qMax<qreal>(
        0, m_impl->lines.back().top + m_impl->lines.back().height
               + qMax<qreal>(80, height() * (1.0 - kAnchorRatio)) - height());
    const QPoint pixelDelta = event->pixelDelta();
    const qreal delta = !pixelDelta.isNull()
        ? pixelDelta.y()
        : event->angleDelta().y() * 0.65;
    m_impl->scroll = std::clamp(m_impl->scroll - delta, 0.0, maxScroll);
    m_impl->scrollVelocity = 0;
    m_impl->userScrollPausedUntilMs =
        QDateTime::currentMSecsSinceEpoch() + kUserScrollPauseMs;
    update();
    event->accept();
}

void AmllLyricsView::mousePressEvent(QMouseEvent *event)
{
    QWidget::mousePressEvent(event);
}
