#include "amlllyricsview.h"

#include "core/lyriceffectsettings.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QTextOption>
#include <QTimer>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace {

constexpr qreal kLinePadEm = 0.40;
constexpr qreal kSubGapEm = 0.25;
constexpr qreal kBlurMax = 5.0;
constexpr qint64 kTailMs = 8000;
constexpr qint64 kInterludeMinGapMs = 7000;
constexpr qint64 kAutoAlignResumeDelayMs = 500;
constexpr qreal kMaxFrameDeltaSecs = 0.10;
constexpr qreal kPi = 3.14159265358979323846;

int effectiveFontPx(int baseFontPx)
{
    return qBound(16,
                  qRound(baseFontPx
                         * LyricEffectSettings::instance().fontScale()),
                  96);
}

struct Spring {
    Spring() = default;
    explicit Spring(qreal position) { setPosition(position); }

    void setPosition(qreal position)
    {
        m_target = position;
        m_current = position;
        m_time = 0;
        m_queuedTarget.reset();
        m_value = [position](qreal) { return position; };
        m_velocity = [](qreal) { return 0.0; };
        m_acceleration = [](qreal) { return 0.0; };
    }

    void setTarget(qreal target, qreal delaySecs = 0)
    {
        if (delaySecs <= 0 && qAbs(m_target - target) < 0.001) {
            m_queuedTarget.reset();
            return;
        }
        if (delaySecs > 0) {
            m_queuedTarget = QueuedTarget{target, delaySecs};
            return;
        }
        m_queuedTarget.reset();
        if (qAbs(m_target - target) < 0.0001)
            return;
        const qreal velocity = m_velocity(m_time);
        m_target = target;
        resetSolver(velocity);
    }

    void setParams(qreal stiffness, qreal damping, qreal mass = 1.0)
    {
        stiffness = qMax<qreal>(1.0, stiffness);
        damping = qMax<qreal>(0.0, damping);
        mass = qMax<qreal>(0.01, mass);
        if (qFuzzyCompare(m_stiffness, stiffness)
            && qFuzzyCompare(m_damping, damping)
            && qFuzzyCompare(m_mass, mass)) {
            return;
        }
        const qreal velocity = m_velocity(m_time);
        m_stiffness = stiffness;
        m_damping = damping;
        m_mass = mass;
        resetSolver(velocity);
    }

    bool arrived() const
    {
        return !m_queuedTarget
            && qAbs(m_target - m_current) < 0.01
            && qAbs(m_velocity(m_time)) < 0.01
            && qAbs(m_acceleration(m_time)) < 0.01;
    }

    void update(qreal deltaSecs)
    {
        if (deltaSecs <= 0)
            return;
        if (m_queuedTarget) {
            m_queuedTarget->remaining -= deltaSecs;
            if (m_queuedTarget->remaining <= 0) {
                const qreal target = m_queuedTarget->position;
                m_queuedTarget.reset();
                setTarget(target);
            }
        }
        m_time += deltaSecs;
        m_current = m_value(m_time);
        if (arrived())
            setPosition(m_target);
    }

    qreal current() const { return m_current; }
    qreal target() const { return m_target; }

private:
    struct QueuedTarget {
        qreal position = 0;
        qreal remaining = 0;
    };

    void resetSolver(qreal initialVelocity)
    {
        const qreal from = m_current;
        const qreal to = m_target;
        const qreal mass = m_mass;
        const qreal stiffness = m_stiffness;
        const qreal damping = m_damping;
        const qreal delta = to - from;
        const qreal omega = std::sqrt(stiffness / mass);
        const qreal zeta = damping / (2.0 * std::sqrt(stiffness * mass));
        m_time = 0;

        if (zeta >= 1.0) {
            const qreal leftover = omega * delta - initialVelocity;
            m_value = [from, to, omega, delta, leftover](qreal t) {
                if (t <= 0)
                    return from;
                return to - (delta + t * leftover) * std::exp(-omega * t);
            };
            m_velocity = [omega, delta, leftover](qreal t) {
                if (t <= 0)
                    return 0.0;
                return std::exp(-omega * t)
                    * (omega * (delta + t * leftover) - leftover);
            };
            m_acceleration = [omega, delta, leftover](qreal t) {
                if (t <= 0)
                    return 0.0;
                return std::exp(-omega * t) * omega
                    * (leftover - omega * (delta + t * leftover));
            };
            return;
        }

        const qreal omegaD = omega * std::sqrt(1.0 - zeta * zeta);
        const qreal a = delta;
        const qreal b = (initialVelocity + zeta * omega * a) / omegaD;
        m_value = [from, to, omega, zeta, omegaD, a, b](qreal t) {
            if (t <= 0)
                return from;
            return to + std::exp(-zeta * omega * t)
                * (a * std::cos(omegaD * t) + b * std::sin(omegaD * t));
        };
        m_velocity = [omega, zeta, omegaD, a, b](qreal t) {
            if (t <= 0)
                return 0.0;
            const qreal decay = std::exp(-zeta * omega * t);
            return decay
                * ((-zeta * omega * a + omegaD * b) * std::cos(omegaD * t)
                   + (-zeta * omega * b - omegaD * a) * std::sin(omegaD * t));
        };
        m_acceleration = [omega, zeta, omegaD, a, b](qreal t) {
            if (t <= 0)
                return 0.0;
            const qreal decay = std::exp(-zeta * omega * t);
            const qreal c = -zeta * omega * a + omegaD * b;
            const qreal d = -zeta * omega * b - omegaD * a;
            return decay
                * ((-zeta * omega * c - omegaD * d) * std::cos(omegaD * t)
                   + (-zeta * omega * d + omegaD * c) * std::sin(omegaD * t));
        };
    }

    qreal m_current = 0;
    qreal m_target = 0;
    qreal m_time = 0;
    qreal m_mass = 1.0;
    qreal m_stiffness = 100.0;
    qreal m_damping = 10.0;
    std::function<qreal(qreal)> m_value = [](qreal) { return 0.0; };
    std::function<qreal(qreal)> m_velocity = [](qreal) { return 0.0; };
    std::function<qreal(qreal)> m_acceleration = [](qreal) { return 0.0; };
    std::optional<QueuedTarget> m_queuedTarget;
};

struct TextBlock {
    QString text;
    QFont font;
    std::unique_ptr<QTextLayout> layout;
    qreal height = 0;
};

struct WordSpan {
    int start = 0;
    int length = 0;
    qint64 startMs = 0;
    qint64 endMs = 0;
    int sourceIndex = -1;
};

struct LineLayout {
    LyricLine source;
    qint64 startMs = 0;
    qint64 endMs = 0;
    TextBlock main;
    TextBlock romanization;
    TextBlock translation;
    std::vector<WordSpan> words;
    qreal top = 0;
    qreal height = 0;
    Spring y;
    Spring scale{1.0};
    qreal opacity = 1.0;
    qreal blur = 0.0;
    bool highlighted = false;
    bool playing = false;
    QRectF screenRect;
    QImage sharpCache;
    QHash<int, QImage> blurCache;
    int cacheWidth = 0;
    bool cacheValid = false;
};

struct Interlude {
    qint64 startMs = 0;
    qint64 endMs = 0;
    int anchorIndex = -1;
};

QColor blendColor(const QColor &from, const QColor &to, qreal progress)
{
    const qreal t = std::clamp(progress, 0.0, 1.0);
    return QColor(
        qRound(from.red() + (to.red() - from.red()) * t),
        qRound(from.green() + (to.green() - from.green()) * t),
        qRound(from.blue() + (to.blue() - from.blue()) * t),
        qRound(from.alpha() + (to.alpha() - from.alpha()) * t));
}

qreal smoothStep(qreal value)
{
    const qreal t = std::clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

qreal easeOutCubic(qreal value)
{
    const qreal t = 1.0 - std::clamp(value, 0.0, 1.0);
    return 1.0 - t * t * t;
}

bool isCjkText(const QString &text)
{
    for (QChar ch : text) {
        const ushort u = ch.unicode();
        if ((u >= 0x3040 && u <= 0x30ff) || (u >= 0x3400 && u <= 0x9fff)
            || (u >= 0xac00 && u <= 0xd7af)) {
            return true;
        }
    }
    return false;
}

qint64 lyricLineEndMs(const QVector<LyricLine> &lyrics, int index)
{
    if (index < 0 || index >= lyrics.size())
        return 0;
    const LyricLine &line = lyrics.at(index);
    if (line.endTime > line.time)
        return line.endTime;
    if (index + 1 < lyrics.size() && lyrics.at(index + 1).time > line.time)
        return lyrics.at(index + 1).time;
    return line.time + kTailMs;
}

QTextLayout *buildTextBlock(TextBlock &block, const QString &text,
                            const QFont &font, qreal width)
{
    block.text = text.trimmed();
    block.font = font;
    block.layout.reset();
    block.height = 0;
    if (block.text.isEmpty() || width <= 1)
        return nullptr;

    block.layout = std::make_unique<QTextLayout>(block.text, block.font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(Qt::AlignLeft | Qt::AlignTop);
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

std::vector<WordSpan> buildWordSpans(const QString &text,
                                     const QVector<LyricWord> &words,
                                     qint64 lineStartMs, qint64 lineEndMs)
{
    std::vector<WordSpan> spans;
    if (text.isEmpty())
        return spans;
    if (words.isEmpty()) {
        spans.push_back(WordSpan{0, int(text.size()), lineStartMs,
                                 lineEndMs, -1});
        return spans;
    }

    int searchFrom = 0;
    for (int i = 0; i < words.size(); ++i) {
        const LyricWord &word = words.at(i);
        if (word.text.isEmpty())
            continue;
        int start = text.indexOf(word.text, searchFrom);
        if (start < 0)
            start = text.indexOf(word.text);
        if (start < 0)
            continue;
        spans.push_back(WordSpan{start, int(word.text.size()), word.startMs,
                                 word.endMs, i});
        searchFrom = start + word.text.size();
    }
    if (spans.empty())
        spans.push_back(WordSpan{0, int(text.size()), lineStartMs,
                                 lineEndMs, -1});
    for (int i = 0; i < static_cast<int>(spans.size()); ++i) {
        WordSpan &span = spans[static_cast<std::size_t>(i)];
        if (span.endMs > span.startMs)
            continue;
        const qint64 nextStart = i + 1 < static_cast<int>(spans.size())
            ? spans[static_cast<std::size_t>(i + 1)].startMs
            : lineEndMs;
        span.endMs = nextStart > span.startMs ? nextStart
                                              : span.startMs + 140;
    }
    return spans;
}

QVector<QString> graphemeSegments(const QString &text, QVector<int> *offsets = nullptr)
{
    QVector<QString> out;
    if (offsets)
        offsets->clear();
    QTextBoundaryFinder boundary(QTextBoundaryFinder::Grapheme, text);
    int offset = 0;
    while (offset < text.size()) {
        boundary.setPosition(offset);
        int next = boundary.toNextBoundary();
        if (next <= offset || next > text.size())
            next = offset + (text.at(offset).isHighSurrogate() ? 2 : 1);
        out.append(text.mid(offset, next - offset));
        if (offsets)
            offsets->append(offset);
        offset = next;
    }
    return out;
}

QImage blurImage(const QImage &source, qreal radius)
{
    if (source.isNull() || radius <= 0.1)
        return source;
    const qreal factor = 1.0 / (1.0 + radius * 0.24);
    const QSize small(
        qMax(1, qRound(source.width() * factor)),
        qMax(1, qRound(source.height() * factor)));
    QImage result = source.scaled(small, Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation)
                        .scaled(source.size(), Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation);
    result.setDevicePixelRatio(source.devicePixelRatio());
    return result;
}

void setPosYSpringPolicy(LineLayout &line, bool seeking, bool interlude,
                         qint64 intervalMs, bool endOfSong)
{
    const auto &fx = LyricEffectSettings::instance();
    const auto apply = [&fx](LineLayout &target, qreal stiffness, qreal damping) {
        target.y.setParams(stiffness * fx.springStrength(),
                           damping * fx.springDamping(), 0.9);
    };
    if (seeking || interlude) {
        apply(line, 90, 15);
        return;
    }
    if (endOfSong) {
        apply(line, 140, 22);
        return;
    }
    if (intervalMs <= 0) {
        apply(line, 90, 15);
        return;
    }
    const qreal clamped = std::clamp<qreal>(qreal(intervalMs), 100.0, 800.0);
    qreal ratio = 1.0 - (clamped - 100.0) / 700.0;
    ratio = std::pow(ratio, 0.2);
    const qreal stiffness = 170.0 + ratio * 50.0;
    apply(line, stiffness, std::sqrt(stiffness) * 2.2);
}

} // namespace

struct AmllLyricsView::Impl {
    QVector<LyricLine> lyrics;
    std::vector<LineLayout> lines;
    std::vector<Interlude> interludes;

    QColor activeColor = QColor(Qt::white);
    QColor dimColor = QColor(255, 255, 255, 77);
    QColor secondaryColor = QColor(255, 255, 255, 77);
    int mainFontPx = 42;
    QString emptyText;

    qint64 positionMs = 0;
    qint64 lastPositionMs = 0;
    qint64 maxEndMs = 0;
    bool playing = false;
    bool endOfSong = false;

    int scrollToIndex = 0;
    int latestHighlighted = -1;
    int activeInterlude = -1;
    int hoveredIndex = -1;
    bool pointerInside = false;

    qreal manualScroll = 0;
    qreal manualScrollMin = 0;
    qreal manualScrollMax = 0;
    bool autoAlignSuspended = false;
    int frozenFocusIndex = -1;
    qint64 interactionEndMs = 0;
    qreal inertiaVelocity = 0;
    bool touchScrolling = false;
    QPointF touchStart;
    qreal touchStartScroll = 0;
    qreal touchLastY = 0;
    qreal touchVelocity = 0;
    qint64 touchLastTimeMs = 0;

    QElapsedTimer frameClock;
    QTimer *frameTimer = nullptr;
};

AmllLyricsView::AmllLyricsView(QWidget *parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_impl->frameClock.start();
    m_impl->frameTimer = new QTimer(this);
    m_impl->frameTimer->setTimerType(Qt::PreciseTimer);
    m_impl->frameTimer->setInterval(16);
    connect(m_impl->frameTimer, &QTimer::timeout, this, [this]() {
        if (!isVisible() || m_impl->lines.empty()) {
            m_impl->frameTimer->stop();
            return;
        }
        const qreal delta = std::clamp<qreal>(
            m_impl->frameClock.restart() / 1000.0, 0.001, kMaxFrameDeltaSecs);

        if (!qFuzzyIsNull(m_impl->inertiaVelocity)) {
            m_impl->manualScroll += m_impl->inertiaVelocity * delta;
            m_impl->inertiaVelocity *= std::pow(0.95, delta * 60.0);
            if (qAbs(m_impl->inertiaVelocity) < 4.0)
                m_impl->inertiaVelocity = 0;
            updateLineTargets(false, false);
        }

        bool moving = false;
        for (LineLayout &line : m_impl->lines) {
            line.y.update(delta);
            line.scale.update(delta);
            moving = moving || !line.y.arrived() || !line.scale.arrived();
        }
        update();
        if (!m_impl->playing && !moving
            && qFuzzyIsNull(m_impl->inertiaVelocity)) {
            m_impl->frameTimer->stop();
        }
    });

    auto &fx = LyricEffectSettings::instance();
    connect(&fx, &LyricEffectSettings::layoutChanged, this, [this]() {
        rebuildLayout(true);
        updateTimeline(m_impl->positionMs, true);
        update();
    });
    connect(&fx, &LyricEffectSettings::effectsChanged, this, [this]() {
        updateLineTargets(false, false);
        update();
    });
}

AmllLyricsView::~AmllLyricsView() = default;

void AmllLyricsView::setLyrics(const QVector<LyricLine> &lyrics)
{
    m_impl->lyrics = lyrics;
    m_impl->positionMs = 0;
    m_impl->lastPositionMs = 0;
    m_impl->scrollToIndex = 0;
    m_impl->latestHighlighted = -1;
    m_impl->activeInterlude = -1;
    m_impl->manualScroll = 0;
    m_impl->inertiaVelocity = 0;
    m_impl->autoAlignSuspended = false;
    m_impl->frozenFocusIndex = -1;
    m_impl->hoveredIndex = -1;
    m_impl->endOfSong = false;

    m_impl->interludes.clear();
    qint64 maxEnd = 0;
    for (int i = 0; i < m_impl->lyrics.size(); ++i) {
        const qint64 end = lyricLineEndMs(m_impl->lyrics, i);
        if (i > 0 && m_impl->lyrics.at(i).time - maxEnd >= kInterludeMinGapMs) {
            m_impl->interludes.push_back(
                Interlude{maxEnd, m_impl->lyrics.at(i).time, i - 1});
        }
        maxEnd = qMax(maxEnd, end);
    }
    m_impl->maxEndMs = maxEnd;

    rebuildLayout(false);
    updateTimeline(0, true);
    resetView();
    if (isVisible())
        m_impl->frameTimer->start();
    update();
}

void AmllLyricsView::rebuildLayout(bool preservePosition)
{
    if (width() <= 0) {
        m_impl->lines.clear();
        update();
        return;
    }

    std::vector<qreal> previousY;
    std::vector<qreal> previousScale;
    if (preservePosition) {
        previousY.reserve(m_impl->lines.size());
        previousScale.reserve(m_impl->lines.size());
        for (const LineLayout &line : m_impl->lines) {
            previousY.push_back(line.y.current());
            previousScale.push_back(line.scale.current());
        }
    }

    const auto &fx = LyricEffectSettings::instance();
    const int fontPx = qBound(
        16, qRound(m_impl->mainFontPx * fx.fontScale()), 96);
    const qreal sidePadding = qBound<qreal>(16.0, fontPx, 34.0);
    const qreal contentWidth = qMax<qreal>(120.0, width() - sidePadding * 2.0);
    QFont mainFont = font();
    mainFont.setPixelSize(fontPx);
    mainFont.setWeight(QFont::Normal);
    QFont subFont = font();
    subFont.setPixelSize(qMax(10, qRound(fontPx * 0.5)));
    subFont.setWeight(QFont::Normal);
    QFont romanFont = subFont;
    romanFont.setItalic(true);

    const qreal padY = fontPx * kLinePadEm * fx.lineSpacing();
    const qreal subGap = fontPx * kSubGapEm * fx.lineSpacing();
    qreal cursorY = 0;
    m_impl->lines.clear();
    m_impl->lines.reserve(static_cast<std::size_t>(m_impl->lyrics.size()));

    for (int i = 0; i < m_impl->lyrics.size(); ++i) {
        const LyricLine &source = m_impl->lyrics.at(i);
        LineLayout line;
        line.source = source;
        line.startMs = source.time;
        line.endMs = lyricLineEndMs(m_impl->lyrics, i);
        QString mainText = source.text;
        if (mainText.isEmpty() && !source.words.isEmpty()) {
            for (const LyricWord &word : source.words)
                mainText += word.text;
        }
        buildTextBlock(line.main, mainText, mainFont, contentWidth);
        buildTextBlock(line.romanization, source.romanization, romanFont, contentWidth);
        buildTextBlock(line.translation, source.translation, subFont, contentWidth);
        line.words = buildWordSpans(line.main.text, source.words,
                                    line.startMs, line.endMs);

        const qreal mainH = qMax<qreal>(fontPx * 1.2, line.main.height);
        qreal contentH = mainH;
        if (line.romanization.height > 0)
            contentH += subGap + line.romanization.height;
        if (line.translation.height > 0)
            contentH += subGap + line.translation.height;
        line.height = padY * 2.0 + contentH;
        line.top = cursorY;
        cursorY += line.height;

        if (preservePosition && i < static_cast<int>(previousY.size())) {
            line.y.setPosition(previousY[static_cast<std::size_t>(i)]);
            line.scale.setPosition(previousScale[static_cast<std::size_t>(i)]);
        } else {
            line.y.setPosition(height() * 2.0 + i * 2.0);
            line.scale.setPosition(1.0);
        }
        m_impl->lines.push_back(std::move(line));
    }

    updateLineTargets(false, false);
}

void AmllLyricsView::updateLineTargets(bool stagger, bool seeking)
{
    if (m_impl->lines.empty() || height() <= 0)
        return;

    const auto &fx = LyricEffectSettings::instance();
    const qreal fontPx = effectiveFontPx(m_impl->mainFontPx);
    const qreal interludeHeight = fontPx * 1.2;
    const int lineCount = static_cast<int>(m_impl->lines.size());
    const bool hasInterlude = m_impl->activeInterlude >= 0;
    const int interludeAnchor = hasInterlude
        ? m_impl->interludes[static_cast<std::size_t>(m_impl->activeInterlude)]
              .anchorIndex
        : -1;

    int focus = qBound(0, m_impl->scrollToIndex, lineCount - 1);
    if (m_impl->autoAlignSuspended && m_impl->frozenFocusIndex >= 0)
        focus = qBound(0, m_impl->frozenFocusIndex, lineCount - 1);
    else if (hasInterlude)
        focus = qMax(0, interludeAnchor);
    else if (m_impl->endOfSong)
        focus = lineCount - 1;

    const LineLayout &focusLine = m_impl->lines[static_cast<std::size_t>(focus)];
    qreal focalTop = focusLine.top;
    qreal focalHeight = focusLine.height;
    if (hasInterlude) {
        focalTop += focusLine.height;
        focalHeight = interludeHeight;
    }
    const qreal anchorBase = height() * fx.alignPosition()
        - focalTop
        - focalHeight * 0.5;
    const qreal contentBottom = anchorBase + m_impl->lines.back().top
        + m_impl->lines.back().height
        + (hasInterlude ? interludeHeight : 0.0);
    m_impl->manualScrollMax = height() * 0.78 - anchorBase;
    m_impl->manualScrollMin = height() * 0.22 - contentBottom;
    m_impl->manualScroll = std::clamp(
        m_impl->manualScroll, m_impl->manualScrollMin, m_impl->manualScrollMax);
    const qreal base = anchorBase + m_impl->manualScroll;

    qreal delay = 0;
    qreal delayStep = stagger ? 0.05 : 0.0;
    const qint64 intervalMs = focus > 0
        ? focusLine.startMs
            - m_impl->lines[static_cast<std::size_t>(focus - 1)].startMs
        : 0;
    for (int i = 0; i < lineCount; ++i) {
        LineLayout &line = m_impl->lines[static_cast<std::size_t>(i)];
        setPosYSpringPolicy(line, seeking, hasInterlude, intervalMs,
                            m_impl->endOfSong);
        if (m_impl->autoAlignSuspended) {
            line.y.setParams(190 * fx.springStrength(),
                             29 * fx.springDamping(), 1.0);
        }
        const qreal extra = hasInterlude && i > interludeAnchor
            ? interludeHeight
            : 0.0;
        const qreal target = base + line.top + extra;
        line.y.setTarget(target, delay);
        const qreal scaleTarget = line.highlighted
            ? 1.0
            : (m_impl->playing ? fx.inactiveScale() : 1.0);
        line.scale.setParams(100, 25, 2.0);
        line.scale.setTarget(scaleTarget, delay);

        if (target + line.height >= 0) {
            delay += delayStep;
            if (i >= focus)
                delayStep /= 1.05;
        }
    }
}

bool AmllLyricsView::updateTimeline(qint64 positionMs, bool seek)
{
    if (m_impl->lines.empty())
        return false;

    const int lineCount = static_cast<int>(m_impl->lines.size());
    std::vector<uint8_t> previousPlaying(lineCount, 0);
    std::vector<uint8_t> previousHighlighted(lineCount, 0);
    for (int i = 0; i < lineCount; ++i) {
        previousPlaying[static_cast<std::size_t>(i)] =
            m_impl->lines[static_cast<std::size_t>(i)].playing ? 1 : 0;
        previousHighlighted[static_cast<std::size_t>(i)] =
            m_impl->lines[static_cast<std::size_t>(i)].highlighted ? 1 : 0;
    }

    const int previousScrollTo = m_impl->scrollToIndex;
    const int previousInterlude = m_impl->activeInterlude;
    const bool previousEnd = m_impl->endOfSong;
    const bool jumped = seek || positionMs < m_impl->lastPositionMs - 250;
    m_impl->positionMs = qMax<qint64>(0, positionMs);
    m_impl->lastPositionMs = m_impl->positionMs;

    std::vector<uint8_t> playing(lineCount, 0);
    for (int i = 0; i < lineCount; ++i) {
        LineLayout &line = m_impl->lines[static_cast<std::size_t>(i)];
        line.playing = line.endMs > line.startMs
            && m_impl->positionMs >= line.startMs
            && m_impl->positionMs < line.endMs;
        playing[static_cast<std::size_t>(i)] = line.playing ? 1 : 0;
    }

    bool addedPlaying = false;
    for (int i = 0; i < lineCount; ++i) {
        if (playing[static_cast<std::size_t>(i)]
            && !previousPlaying[static_cast<std::size_t>(i)]) {
            addedPlaying = true;
            break;
        }
    }

    if (addedPlaying) {
        for (int i = 0; i < lineCount; ++i) {
            if (!playing[static_cast<std::size_t>(i)])
                m_impl->lines[static_cast<std::size_t>(i)].highlighted = false;
        }
        for (int i = 0; i < lineCount; ++i) {
            if (playing[static_cast<std::size_t>(i)])
                m_impl->lines[static_cast<std::size_t>(i)].highlighted = true;
        }
    } else if (jumped) {
        for (int i = 0; i < lineCount; ++i) {
            m_impl->lines[static_cast<std::size_t>(i)].highlighted =
                playing[static_cast<std::size_t>(i)] != 0;
        }
    }

    int active = -1;
    for (int i = 0; i < static_cast<int>(m_impl->interludes.size()); ++i) {
        const Interlude &inter = m_impl->interludes[static_cast<std::size_t>(i)];
        if (m_impl->positionMs >= inter.startMs && m_impl->positionMs < inter.endMs) {
            active = i;
            break;
        }
    }
    if (active >= 0) {
        bool anyPlaying = false;
        for (uint8_t p : playing)
            anyPlaying = anyPlaying || p;
        if (!anyPlaying) {
            for (LineLayout &line : m_impl->lines)
                line.highlighted = false;
        }
    }

    m_impl->endOfSong = m_impl->maxEndMs > 0 && m_impl->positionMs >= m_impl->maxEndMs;
    if (m_impl->endOfSong) {
        for (LineLayout &line : m_impl->lines)
            line.highlighted = false;
    }
    m_impl->activeInterlude = active;

    int minHighlighted = lineCount;
    int latestHighlighted = -1;
    for (int i = 0; i < lineCount; ++i) {
        if (m_impl->lines[static_cast<std::size_t>(i)].highlighted) {
            minHighlighted = qMin(minHighlighted, i);
            latestHighlighted = qMax(latestHighlighted, i);
        }
    }
    if (minHighlighted < lineCount)
        m_impl->scrollToIndex = minHighlighted;
    m_impl->latestHighlighted = latestHighlighted;
    m_impl->scrollToIndex = qBound(0, m_impl->scrollToIndex, lineCount - 1);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_impl->autoAlignSuspended && addedPlaying
        && now - m_impl->interactionEndMs >= kAutoAlignResumeDelayMs) {
        m_impl->manualScroll = 0;
        m_impl->autoAlignSuspended = false;
        m_impl->frozenFocusIndex = -1;
    }
    if (jumped && !m_impl->touchScrolling) {
        m_impl->manualScroll = 0;
        m_impl->autoAlignSuspended = false;
        m_impl->frozenFocusIndex = -1;
    }

    bool highlightedChanged = false;
    for (int i = 0; i < lineCount; ++i) {
        const uint8_t nowHighlighted =
            m_impl->lines[static_cast<std::size_t>(i)].highlighted ? 1 : 0;
        if (nowHighlighted != previousHighlighted[static_cast<std::size_t>(i)]) {
            highlightedChanged = true;
            break;
        }
    }
    const bool needsTargets = jumped || previousInterlude != active
        || previousScrollTo != m_impl->scrollToIndex || highlightedChanged
        || previousEnd != m_impl->endOfSong;
    if (needsTargets)
        updateLineTargets(!jumped, jumped);
    else if (m_impl->playing) {
        const auto &fx = LyricEffectSettings::instance();
        for (LineLayout &line : m_impl->lines)
            line.scale.setTarget(
                line.highlighted ? 1.0 : fx.inactiveScale());
    }

    if (!m_impl->frameTimer->isActive() && isVisible())
        m_impl->frameTimer->start();
    return needsTargets;
}

void AmllLyricsView::setPosition(qint64 positionMs, bool seek)
{
    if (m_impl->lyrics.isEmpty() || height() <= 0)
        return;
    updateTimeline(positionMs, seek);
    update();
}

void AmllLyricsView::setPlaying(bool playing)
{
    if (m_impl->playing == playing)
        return;
    m_impl->playing = playing;
    m_impl->frameClock.restart();
    const auto &fx = LyricEffectSettings::instance();
    for (LineLayout &line : m_impl->lines)
        line.scale.setTarget(
            playing && !line.highlighted ? fx.inactiveScale() : 1.0);
    if (playing && isVisible())
        m_impl->frameTimer->start();
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
    m_impl->secondaryColor = secondary.isValid() ? secondary : dim;
    m_impl->mainFontPx = safeFontPx;
    rebuildLayout(true);
    updateTimeline(m_impl->positionMs, true);
    update();
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
    updateLineTargets(false, false);
    for (LineLayout &line : m_impl->lines)
        line.y.setPosition(line.y.target());
    update();
}

QImage AmllLyricsView::sharpLineImage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_impl->lines.size()))
        return {};
    LineLayout &line = m_impl->lines[static_cast<std::size_t>(index)];
    const auto &fx = LyricEffectSettings::instance();
    const qreal fontPx = effectiveFontPx(m_impl->mainFontPx);
    const qreal sidePadding = qBound<qreal>(16.0, fontPx, 34.0);
    const int logicalWidth = qMax(1, qRound(width() - sidePadding * 2.0));
    const int logicalHeight = qMax(1, qCeil(line.height));
    if (line.cacheValid && line.cacheWidth == logicalWidth
        && !line.sharpCache.isNull()) {
        return line.sharpCache;
    }

    const qreal dpr = devicePixelRatioF();
    QImage image(qCeil(logicalWidth * dpr), qCeil(logicalHeight * dpr),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(dpr);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.scale(dpr, dpr);

    QColor secondary = m_impl->secondaryColor;
    secondary.setAlphaF(qMin<qreal>(secondary.alphaF(), 0.30));
    qreal y = fontPx * kLinePadEm * fx.lineSpacing();
    if (line.main.layout) {
        painter.setPen(m_impl->dimColor);
        line.main.layout->draw(&painter, QPointF(0, y));
        y += qMax<qreal>(fontPx * 1.2, line.main.height);
    }
    if (line.romanization.layout) {
        y += fontPx * kSubGapEm * fx.lineSpacing();
        painter.setPen(secondary);
        line.romanization.layout->draw(&painter, QPointF(0, y));
        y += line.romanization.height;
    }
    if (line.translation.layout) {
        y += fontPx * kSubGapEm * fx.lineSpacing();
        painter.setPen(secondary);
        line.translation.layout->draw(&painter, QPointF(0, y));
    }
    painter.end();

    line.sharpCache = image;
    line.blurCache.clear();
    line.cacheWidth = logicalWidth;
    line.cacheValid = true;
    return line.sharpCache;
}

void AmllLyricsView::drawKaraokeLine(QPainter &painter, int index)
{
    if (index < 0 || index >= static_cast<int>(m_impl->lines.size()))
        return;
    LineLayout &line = m_impl->lines[static_cast<std::size_t>(index)];
    const auto &fx = LyricEffectSettings::instance();
    const qreal fontPx = effectiveFontPx(m_impl->mainFontPx);
    const qreal padY = fontPx * kLinePadEm * fx.lineSpacing();
    if (!line.main.layout)
        return;

    painter.setFont(line.main.font);
    const auto drawSubLines = [&]() {
        QColor secondary = m_impl->secondaryColor;
        secondary.setAlphaF(qMin<qreal>(secondary.alphaF(), 0.30));
        qreal y = padY + qMax<qreal>(fontPx * 1.2, line.main.height);
        if (line.romanization.layout) {
            y += fontPx * kSubGapEm * fx.lineSpacing();
            painter.setPen(secondary);
            line.romanization.layout->draw(&painter, QPointF(0, y));
            y += line.romanization.height;
        }
        if (line.translation.layout) {
            y += fontPx * kSubGapEm * fx.lineSpacing();
            painter.setPen(secondary);
            line.translation.layout->draw(&painter, QPointF(0, y));
        }
    };
    if (!fx.wordSweep()) {
        painter.setPen(m_impl->activeColor);
        line.main.layout->draw(&painter, QPointF(0, padY));
        drawSubLines();
        return;
    }

    QVector<int> graphemeOffsets;
    const QVector<QString> graphemes =
        graphemeSegments(line.main.text, &graphemeOffsets);
    const auto wordAt = [&line](int position) -> const WordSpan * {
        for (const WordSpan &word : line.words) {
            if (position >= word.start && position < word.start + word.length)
                return &word;
        }
        return nullptr;
    };
    const WordSpan *cachedWord = nullptr;
    int cachedWordFirstGrapheme = 0;
    int cachedWordGraphemeCount = 1;

    for (int li = 0; li < line.main.layout->lineCount(); ++li) {
        const QTextLine textLine = line.main.layout->lineAt(li);
        const int lineStart = textLine.textStart();
        const int lineEnd = lineStart + textLine.textLength();
        for (int gi = 0; gi < graphemes.size(); ++gi) {
            const int globalStart = graphemeOffsets.at(gi);
            const int globalEnd = globalStart + graphemes.at(gi).size();
            if (globalEnd <= lineStart || globalStart >= lineEnd)
                continue;

            const WordSpan *word = wordAt(globalStart);
            qreal rawProgress = 0.0;
            qreal floatY = 0.0;
            qreal emphasis = 0.0;
            QColor color = m_impl->dimColor;
            if (word && word->length > 0) {
                qint64 wordStart = word->startMs;
                qint64 wordEnd = word->endMs;
                if (wordEnd <= wordStart)
                    wordEnd = wordStart + 180;
                const qreal duration = qMax<qint64>(1, wordEnd - wordStart);
                rawProgress = std::clamp<qreal>(
                    (m_impl->positionMs - wordStart) / duration, 0.0, 1.0);
                const qreal floatProgress = std::clamp<qreal>(
                    (m_impl->positionMs - wordStart)
                        / qMax<qint64>(1000, wordEnd - wordStart),
                    0.0,
                    1.0);
                if (fx.wordFloat()) {
                    floatY = -0.05 * fontPx * easeOutCubic(floatProgress)
                        * (line.source.background ? 2.0 : 1.0);
                }
                if (cachedWord != word) {
                    cachedWord = word;
                    cachedWordFirstGrapheme = qMax(
                        0, int(std::lower_bound(graphemeOffsets.cbegin(),
                                                graphemeOffsets.cend(),
                                                word->start)
                                   - graphemeOffsets.cbegin()));
                    cachedWordGraphemeCount = qMax(
                        1, int(graphemeSegments(
                                   line.main.text.mid(word->start, word->length))
                                   .size()));
                }
                const int wordGrapheme = gi - cachedWordFirstGrapheme;
                const qreal glyphProgress = std::clamp<qreal>(
                    (rawProgress * cachedWordGraphemeCount - wordGrapheme)
                        / qMax(1, int(cachedWordGraphemeCount * 0.5)),
                    0.0,
                    1.0);
                color = blendColor(m_impl->dimColor, m_impl->activeColor,
                                   smoothStep(glyphProgress));

                const QString wordText =
                    line.main.text.mid(word->start, word->length);
                const bool emphasize = fx.emphasisGlow()
                    && (wordEnd - wordStart) >= 1000
                    && (isCjkText(wordText)
                        || (wordText.trimmed().size() <= 7
                            && wordText.trimmed().size() > 1));
                emphasis = emphasize ? std::sin(rawProgress * kPi) : 0.0;
            }

            int cursor = globalStart;
            const qreal x = textLine.cursorToX(&cursor);
            const qreal baseline = textLine.y() + textLine.ascent();
            painter.save();
            if (emphasis > 0.001) {
                const qreal cx = textLine.cursorToX(globalEnd);
                const qreal cy = baseline + floatY;
                painter.translate(cx, cy);
                const qreal s = 1.0 + emphasis * 0.06;
                painter.scale(s, s);
                painter.translate(-cx, -cy);
                QColor glow = m_impl->activeColor;
                glow.setAlphaF(0.16 * emphasis);
                painter.setPen(glow);
                painter.drawText(QPointF(x - 1.0, baseline + floatY - 0.5),
                                 graphemes.at(gi));
            }
            painter.setPen(color);
            painter.drawText(QPointF(x, baseline + floatY), graphemes.at(gi));
            painter.restore();
        }
    }

    drawSubLines();
}

void AmllLyricsView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_impl->lines.empty()) {
        const QColor emptyColor = m_impl->dimColor;
        const int cx = width() / 2;
        const int cy = height() / 2 - 8;
        painter.setPen(QPen(emptyColor, 2.0));
        painter.setBrush(Qt::NoBrush);
        QPainterPath doc;
        doc.moveTo(cx - 15, cy - 20);
        doc.lineTo(cx + 8, cy - 20);
        doc.lineTo(cx + 16, cy - 12);
        doc.lineTo(cx + 16, cy + 20);
        doc.lineTo(cx - 15, cy + 20);
        doc.closeSubpath();
        painter.drawPath(doc);
        painter.drawLine(QPointF(cx - 8, cy - 7), QPointF(cx + 9, cy - 7));
        painter.drawLine(QPointF(cx - 8, cy + 2), QPointF(cx + 9, cy + 2));
        painter.drawLine(QPointF(cx - 8, cy + 11), QPointF(cx + 3, cy + 11));
        painter.setPen(emptyColor);
        painter.setFont(font());
        painter.drawText(QRect(0, cy + 36, width(), 36),
                         Qt::AlignHCenter | Qt::AlignTop, m_impl->emptyText);
        return;
    }

    const qreal fontPx = effectiveFontPx(m_impl->mainFontPx);
    const auto &fx = LyricEffectSettings::instance();
    const qreal sidePadding = qBound<qreal>(16.0, fontPx, 34.0);
    const bool hasInterlude = m_impl->activeInterlude >= 0;
    const int interludeAnchor = hasInterlude
        ? m_impl->interludes[static_cast<std::size_t>(m_impl->activeInterlude)]
              .anchorIndex
        : -1;
    const qreal interludeHeight = fontPx * 1.2;

    if (m_impl->pointerInside && m_impl->hoveredIndex >= 0
        && m_impl->hoveredIndex < static_cast<int>(m_impl->lines.size())) {
        LineLayout &line =
            m_impl->lines[static_cast<std::size_t>(m_impl->hoveredIndex)];
        const qreal y = line.y.current()
            + (hasInterlude && m_impl->hoveredIndex > interludeAnchor
                   ? interludeHeight
                   : 0.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 17));
        painter.drawRoundedRect(
            QRectF(sidePadding * 0.35, y + 2,
                   width() - sidePadding * 0.70, qMax(1.0, line.height - 4)),
            8, 8);
    }

    for (int i = 0; i < static_cast<int>(m_impl->lines.size()); ++i) {
        LineLayout &line = m_impl->lines[static_cast<std::size_t>(i)];
        const qreal extra = hasInterlude && i > interludeAnchor
            ? interludeHeight
            : 0.0;
        const qreal y = line.y.current() + extra;
        const qreal scaleValue = line.scale.current();
        const qreal scaledHeight = line.height * scaleValue;
        if (y + scaledHeight < -16 || y > height() + 16)
            continue;

        line.screenRect = QRectF(0, y, width(), qMax<qreal>(1.0, scaledHeight));
        const bool hovered = m_impl->pointerInside && m_impl->hoveredIndex == i;
        qreal blur = 0;
        if (!m_impl->pointerInside && !m_impl->autoAlignSuspended && !hovered
            && fx.distanceBlur()) {
            if (line.highlighted) {
                blur = 0;
            } else {
                const int latest = m_impl->latestHighlighted >= 0
                    ? m_impl->latestHighlighted
                    : m_impl->scrollToIndex;
                const int distance = i < m_impl->scrollToIndex
                    ? qAbs(m_impl->scrollToIndex - i) + 1
                    : qAbs(i - latest);
                blur = fx.blurStrength()
                    * qMin<qreal>(
                        kBlurMax,
                        (width() <= 1024 ? 0.8 : 1.0) * (1.0 + distance));
            }
        }
        line.blur = blur;
        const bool nonDynamic =
            line.words.size() == 1 && line.words.front().sourceIndex < 0;
        line.opacity = line.highlighted
            ? 0.85
            : (nonDynamic ? 0.20 : 1.0);

        painter.save();
        painter.setOpacity(line.opacity);
        painter.translate(sidePadding, y);
        painter.scale(scaleValue, scaleValue);
        if (line.highlighted) {
            drawKaraokeLine(painter, i);
        } else {
            const QImage sharp = sharpLineImage(i);
            QImage image = sharp;
            if (blur > 0.1) {
                const int key = qRound(blur * 4.0);
                auto it = line.blurCache.constFind(key);
                if (it == line.blurCache.cend()) {
                    image = blurImage(sharp, blur);
                    line.blurCache.insert(key, image);
                } else {
                    image = it.value();
                }
            }
            painter.drawImage(QPointF(0, 0), image);
        }
        painter.restore();
    }

    if (hasInterlude && interludeAnchor >= 0
        && interludeAnchor < static_cast<int>(m_impl->lines.size())) {
        const LineLayout &anchor =
            m_impl->lines[static_cast<std::size_t>(interludeAnchor)];
        const qreal dotSize = fontPx * 0.30;
        const qreal gap = fontPx * 0.18;
        const qreal x = sidePadding;
        const qreal y = anchor.y.current() + anchor.height
            + (interludeHeight - dotSize) * 0.5;
        const qreal phase = (m_impl->positionMs % 1200) / 1200.0;
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < 3; ++i) {
            const qreal local = std::fmod(phase + i * 0.16, 1.0);
            const qreal bounce = std::sin(local * kPi) * fontPx * 0.20;
            QColor dot = m_impl->activeColor;
            dot.setAlphaF(0.48 + 0.35 * std::sin(local * kPi));
            painter.setBrush(dot);
            painter.drawEllipse(
                QRectF(x + i * (dotSize + gap), y - bounce, dotSize, dotSize));
        }
    }
}

void AmllLyricsView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildLayout(true);
    updateTimeline(m_impl->positionMs, true);
}

void AmllLyricsView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_impl->frameClock.restart();
    if (!m_impl->lines.empty())
        m_impl->frameTimer->start();
}

void AmllLyricsView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_impl->frameTimer->stop();
    m_impl->inertiaVelocity = 0;
}

void AmllLyricsView::wheelEvent(QWheelEvent *event)
{
    if (m_impl->lines.empty()) {
        QWidget::wheelEvent(event);
        return;
    }
    const QPoint pixelDelta = event->pixelDelta();
    const qreal delta = !pixelDelta.isNull()
        ? pixelDelta.y()
        : event->angleDelta().y() * 0.55;
    // 与原生滚动方向一致：滚轮向上看更早的歌词（内容向下），向下看后续歌词。
    if (!m_impl->autoAlignSuspended)
        m_impl->frozenFocusIndex = m_impl->scrollToIndex;
    m_impl->manualScroll += delta;
    m_impl->autoAlignSuspended = true;
    m_impl->interactionEndMs = QDateTime::currentMSecsSinceEpoch();
    m_impl->inertiaVelocity = 0;
    updateLineTargets(false, false);
    if (!m_impl->frameTimer->isActive())
        m_impl->frameTimer->start();
    update();
    event->accept();
}

int AmllLyricsView::lineIndexAt(const QPointF &position) const
{
    for (int i = 0; i < static_cast<int>(m_impl->lines.size()); ++i) {
        if (m_impl->lines[static_cast<std::size_t>(i)].screenRect.contains(position))
            return i;
    }
    return -1;
}

void AmllLyricsView::mouseMoveEvent(QMouseEvent *event)
{
    m_impl->pointerInside = true;
    const int index = lineIndexAt(event->position());
    if (m_impl->hoveredIndex != index) {
        m_impl->hoveredIndex = index;
        setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void AmllLyricsView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int index = lineIndexAt(event->position());
        if (index >= 0) {
            const qint64 targetMs =
                m_impl->lines[static_cast<std::size_t>(index)].startMs;
            // 先在本画布内建立目标并启动弹簧动画，避免等待音频引擎异步
            // 回传 positionChanged 时先出现一帧无动画瞬移。
            m_impl->inertiaVelocity = 0;
            m_impl->touchScrolling = false;
            m_impl->autoAlignSuspended = false;
            m_impl->frozenFocusIndex = -1;
            setPosition(targetMs, true);
            emit seekRequested(targetMs);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void AmllLyricsView::leaveEvent(QEvent *event)
{
    m_impl->pointerInside = false;
    m_impl->hoveredIndex = -1;
    unsetCursor();
    update();
    QWidget::leaveEvent(event);
}

bool AmllLyricsView::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        auto *touch = static_cast<QTouchEvent *>(event);
        const auto points = touch->points();
        if (event->type() == QEvent::TouchBegin && !points.isEmpty()) {
            if (!m_impl->autoAlignSuspended)
                m_impl->frozenFocusIndex = m_impl->scrollToIndex;
            m_impl->touchScrolling = true;
            m_impl->touchStart = points.first().position();
            m_impl->touchLastY = m_impl->touchStart.y();
            m_impl->touchStartScroll = m_impl->manualScroll;
            m_impl->touchVelocity = 0;
            m_impl->touchLastTimeMs = QDateTime::currentMSecsSinceEpoch();
            m_impl->interactionEndMs = m_impl->touchLastTimeMs;
            m_impl->autoAlignSuspended = true;
            touch->accept();
            return true;
        }
        if (event->type() == QEvent::TouchUpdate && !points.isEmpty()) {
            const qreal y = points.first().position().y();
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            const qreal dt = qMax<qreal>(1.0, now - m_impl->touchLastTimeMs) / 1000.0;
            m_impl->manualScroll =
                m_impl->touchStartScroll + (y - m_impl->touchStart.y());
            // 手指上推时内容继续向上滑，惯性方向必须与最后一帧位移一致。
            m_impl->touchVelocity = (y - m_impl->touchLastY) / dt;
            m_impl->touchLastY = y;
            m_impl->touchLastTimeMs = now;
            m_impl->interactionEndMs = now;
            updateLineTargets(false, false);
            if (!m_impl->frameTimer->isActive())
                m_impl->frameTimer->start();
            update();
            touch->accept();
            return true;
        }
        if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
            if (event->type() == QEvent::TouchEnd) {
                m_impl->inertiaVelocity = std::clamp<qreal>(
                    m_impl->touchVelocity, -2200.0, 2200.0);
                if (qAbs(m_impl->inertiaVelocity) < 20.0)
                    m_impl->inertiaVelocity = 0;
            } else {
                m_impl->inertiaVelocity = 0;
            }
            m_impl->touchScrolling = false;
            m_impl->interactionEndMs = QDateTime::currentMSecsSinceEpoch();
            if (!m_impl->frameTimer->isActive())
                m_impl->frameTimer->start();
            touch->accept();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QWidget::event(event);
}
