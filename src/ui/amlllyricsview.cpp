#include "amlllyricsview.h"

#include "core/lyriceffectsettings.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QLinearGradient>
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
constexpr qint64 kMaxVisualLeadMs = 120;
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
        if (delaySecs > 0 && qAbs(m_target - target) >= 0.001) {
            if (m_queuedTarget && qAbs(m_queuedTarget->position - target) < 0.001)
                return;
            m_queuedTarget = QueuedTarget{target, delaySecs};
            return;
        }
        m_queuedTarget.reset();
        if (qAbs(m_target - target) < 0.001)
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
        if (m_queuedTarget && m_queuedTarget->remaining <= deltaSecs) {
            const auto queued = *m_queuedTarget;
            m_time += queued.remaining;
            m_current = m_value(m_time);
            deltaSecs -= queued.remaining;
            setTarget(queued.position);
        } else if (m_queuedTarget) {
            m_queuedTarget->remaining -= deltaSecs;
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
        const qreal offset = from - to;
        const qreal omega = std::sqrt(stiffness / mass);
        const qreal zeta = damping / (2.0 * std::sqrt(stiffness * mass));
        m_time = 0;

        // 换目标时保留瞬时速度；三种阻尼都从当前坐标连续起步。
        if (zeta > 1.0001) {
            const qreal root = std::sqrt(zeta * zeta - 1.0);
            const qreal slow = -omega / (zeta + root);
            const qreal fast = -omega * (zeta + root);
            const qreal a = (initialVelocity - fast * offset) / (slow - fast);
            const qreal b = offset - a;
            m_value = [to, slow, fast, a, b](qreal t) {
                return to + a * std::exp(slow * t) + b * std::exp(fast * t);
            };
            m_velocity = [slow, fast, a, b](qreal t) {
                return slow * a * std::exp(slow * t) + fast * b * std::exp(fast * t);
            };
            m_acceleration = [slow, fast, a, b](qreal t) {
                return slow * slow * a * std::exp(slow * t)
                    + fast * fast * b * std::exp(fast * t);
            };
            return;
        }
        if (zeta >= 0.9999) {
            const qreal slope = initialVelocity + omega * offset;
            m_value = [to, omega, offset, slope](qreal t) {
                return to + (offset + t * slope) * std::exp(-omega * t);
            };
            m_velocity = [omega, initialVelocity, slope](qreal t) {
                return (initialVelocity - omega * t * slope) * std::exp(-omega * t);
            };
            m_acceleration = [omega, initialVelocity, slope](qreal t) {
                return -omega * (slope + initialVelocity - omega * t * slope)
                    * std::exp(-omega * t);
            };
            return;
        }

        const qreal omegaD = omega * std::sqrt(1.0 - zeta * zeta);
        const qreal a = offset;
        const qreal b = (initialVelocity + zeta * omega * a) / omegaD;
        m_value = [to, omega, zeta, omegaD, a, b](qreal t) {
            return to + std::exp(-zeta * omega * t)
                * (a * std::cos(omegaD * t) + b * std::sin(omegaD * t));
        };
        m_velocity = [omega, zeta, omegaD, a, b](qreal t) {
            const qreal decay = std::exp(-zeta * omega * t);
            return decay
                * ((-zeta * omega * a + omegaD * b) * std::cos(omegaD * t)
                   + (-zeta * omega * b - omegaD * a) * std::sin(omegaD * t));
        };
        m_acceleration = [omega, zeta, omegaD, a, b](qreal t) {
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
    bool canEmphasize = false;
    qreal sweepWidth = 0;
};

struct GlyphMotion {
    QString text;
    qreal x = 0;
    qreal advance = 0;
    qreal baseline = 0;
    int word = -1;
    qreal sweepStart = 0;
    qreal sweepEnd = 0;
};

struct LineLayout {
    qint64 startMs = 0;
    qint64 endMs = 0;
    bool background = false;
    TextBlock main;
    TextBlock romanization;
    TextBlock translation;
    std::vector<WordSpan> words;
    std::vector<GlyphMotion> glyphs;
    qreal top = 0;
    qreal height = 0;
    Spring y;
    Spring scale{1.0};
    qreal highlight = 0.0;
    qreal hover = 0.0;
    qreal blur = 0.0;
    qreal blurTarget = 0.0;
    bool highlighted = false;
    bool playing = false;
    QRectF screenRect;
    QImage sharpCache;
    QHash<int, QImage> blurCache;
    int cacheWidth = 0;
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

bool approach(qreal &value, qreal target, qreal step)
{
    if (value == target)
        return false;
    value = value < target ? qMin(target, value + step)
                           : qMax(target, value - step);
    return value != target;
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
                                     const QVector<LyricWord> &words, qint64 lineEndMs)
{
    std::vector<WordSpan> spans;
    if (text.isEmpty() || words.isEmpty())
        return spans;

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
                                 word.endMs});
        searchFrom = start + word.text.size();
    }
    for (int i = 0; i < static_cast<int>(spans.size()); ++i) {
        WordSpan &span = spans[static_cast<std::size_t>(i)];
        if (span.endMs <= span.startMs) {
            const qint64 nextStart = i + 1 < static_cast<int>(spans.size())
                ? spans[static_cast<std::size_t>(i + 1)].startMs
                : lineEndMs;
            span.endMs = nextStart > span.startMs ? nextStart : span.startMs + 140;
        }
        if (span.endMs - span.startMs >= 1000) {
            const QString wordText = text.mid(span.start, span.length).trimmed();
            span.canEmphasize = isCjkText(wordText)
                || (wordText.size() > 1 && wordText.size() <= 7);
        }
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

void buildGlyphMotion(LineLayout &line)
{
    if (!line.main.layout || line.words.empty())
        return;

    QVector<int> offsets;
    const auto graphemes = graphemeSegments(line.main.text, &offsets);
    for (int li = 0, gi = 0; li < line.main.layout->lineCount(); ++li) {
        const QTextLine row = line.main.layout->lineAt(li);
        const int end = row.textStart() + row.textLength();
        for (; gi < offsets.size() && offsets.at(gi) < end; ++gi) {
            const int start = offsets.at(gi);
            const qreal x = row.cursorToX(start);
            const qreal advance = row.cursorToX(start, QTextLine::Trailing) - x;
            const auto &text = graphemes.at(gi);
            GlyphMotion glyph{text.trimmed().isEmpty() ? QString() : text,
                              x, advance, row.y() + row.ascent()};
            for (int wi = 0; wi < static_cast<int>(line.words.size()); ++wi) {
                auto &word = line.words[static_cast<std::size_t>(wi)];
                if (start < word.start || start >= word.start + word.length)
                    continue;
                glyph.word = wi;
                glyph.sweepStart = word.sweepWidth;
                word.sweepWidth += qMax(1.0, qAbs(advance));
                glyph.sweepEnd = word.sweepWidth;
                break;
            }
            line.glyphs.push_back(std::move(glyph));
        }
    }
    for (auto &glyph : line.glyphs) {
        if (glyph.word < 0)
            continue;
        const qreal wordWidth = line.words[static_cast<std::size_t>(glyph.word)].sweepWidth;
        glyph.sweepStart /= wordWidth;
        glyph.sweepEnd /= wordWidth;
    }
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
        apply(line, 125, 21);
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
    apply(line, stiffness, std::sqrt(stiffness) * 1.75);
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
    qint64 sampledPositionMs = 0;
    QElapsedTimer sampleClock;
    bool waitingForSeek = false;
    qint64 maxEndMs = 0;
    bool playing = false;
    bool endOfSong = false;

    int scrollToIndex = 0;
    int latestHighlighted = -1;
    int activeInterlude = -1;
    int visibleInterlude = -1;
    qint64 interludePositionMs = 0;
    qreal interludeVisibility = 0.0;
    int hoveredIndex = -1;
    bool pointerInside = false;

    qreal manualScroll = 0;
    qreal manualScrollMin = 0;
    qreal manualScrollMax = 0;
    bool autoAlignSuspended = false;
    qreal anchorBase = 0;
    qreal frozenAnchorBase = 0;
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

        bool clockMoving = false;
        if (m_impl->playing && m_impl->sampleClock.isValid() && !m_impl->waitingForSeek) {
            const qint64 elapsed = m_impl->sampleClock.elapsed();
            const qint64 position = m_impl->sampledPositionMs
                + qMin(elapsed, kMaxVisualLeadMs);
            if (position > m_impl->positionMs)
                updateTimeline(position, false);
            clockMoving = elapsed < kMaxVisualLeadMs;
        }

        if (!qFuzzyIsNull(m_impl->inertiaVelocity)) {
            const qreal decay = std::exp(-5.6 * delta);
            const qreal next = m_impl->manualScroll
                + m_impl->inertiaVelocity * (1.0 - decay) / 5.6;
            m_impl->manualScroll = next;
            m_impl->inertiaVelocity *= decay;
            updateLineTargets(false, false);
            if (m_impl->manualScroll != next || qAbs(m_impl->inertiaVelocity) < 20.0)
                m_impl->inertiaVelocity = 0;
        }

        bool moving = false;
        for (int i = 0; i < static_cast<int>(m_impl->lines.size()); ++i) {
            auto &line = m_impl->lines[static_cast<std::size_t>(i)];
            if (!line.y.arrived())
                line.y.update(delta);
            if (!line.scale.arrived())
                line.scale.update(delta);
            moving |= approach(line.highlight, line.highlighted ? 1.0 : 0.0,
                               delta / (line.highlighted ? 0.14 : 0.23));
            moving |= approach(line.hover,
                               m_impl->pointerInside && m_impl->hoveredIndex == i ? 1.0 : 0.0,
                               delta / 0.16);
            moving |= approach(line.blur, line.blurTarget, delta * 44.0);
            moving = moving || !line.y.arrived() || !line.scale.arrived();
        }
        moving |= approach(m_impl->interludeVisibility,
                           m_impl->activeInterlude >= 0 ? 1.0 : 0.0,
                           delta / (m_impl->activeInterlude >= 0 ? 0.24 : 0.18));
        if (m_impl->interludeVisibility <= 0.0)
            m_impl->visibleInterlude = -1;
        update();
        if (!clockMoving && !moving && qFuzzyIsNull(m_impl->inertiaVelocity)) {
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
    m_impl->sampledPositionMs = 0;
    m_impl->sampleClock.invalidate();
    m_impl->waitingForSeek = false;
    m_impl->scrollToIndex = 0;
    m_impl->latestHighlighted = -1;
    m_impl->activeInterlude = -1;
    m_impl->visibleInterlude = -1;
    m_impl->interludeVisibility = 0;
    m_impl->manualScroll = 0;
    m_impl->inertiaVelocity = 0;
    m_impl->autoAlignSuspended = false;
    m_impl->touchScrolling = false;
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
    scheduleFrame();
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
    std::vector<qreal> previousHighlight;
    if (preservePosition) {
        previousY.reserve(m_impl->lines.size());
        previousScale.reserve(m_impl->lines.size());
        previousHighlight.reserve(m_impl->lines.size());
        for (const LineLayout &line : m_impl->lines) {
            previousY.push_back(line.y.current());
            previousScale.push_back(line.scale.current());
            previousHighlight.push_back(line.highlight);
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
        line.startMs = source.time;
        line.endMs = lyricLineEndMs(m_impl->lyrics, i);
        line.background = source.background;
        QString mainText = source.text;
        if (mainText.isEmpty() && !source.words.isEmpty()) {
            for (const LyricWord &word : source.words)
                mainText += word.text;
        }
        buildTextBlock(line.main, mainText, mainFont, contentWidth);
        buildTextBlock(line.romanization, source.romanization, romanFont, contentWidth);
        buildTextBlock(line.translation, source.translation, subFont, contentWidth);
        line.words = buildWordSpans(line.main.text, source.words, line.endMs);
        buildGlyphMotion(line);

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
            line.highlight = previousHighlight[static_cast<std::size_t>(i)];
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
    if (hasInterlude)
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
    const qreal anchorBase = m_impl->autoAlignSuspended ? m_impl->frozenAnchorBase
        : height() * fx.alignPosition() - focalTop - focalHeight * 0.5;
    m_impl->anchorBase = anchorBase;
    const qreal contentBottom = anchorBase + m_impl->lines.back().top
        + m_impl->lines.back().height
        + (hasInterlude ? interludeHeight : 0.0);
    m_impl->manualScrollMax = height() * 0.78 - anchorBase;
    m_impl->manualScrollMin = height() * 0.22 - contentBottom;
    m_impl->manualScroll = std::clamp(
        m_impl->manualScroll, m_impl->manualScrollMin, m_impl->manualScrollMax);
    const qreal base = anchorBase + m_impl->manualScroll;

    qreal delay = 0;
    const bool cascade = stagger && !m_impl->autoAlignSuspended;
    const qint64 intervalMs = focus > 0
        ? focusLine.startMs
            - m_impl->lines[static_cast<std::size_t>(focus - 1)].startMs
        : 0;
    for (int i = 0; i < lineCount; ++i) {
        LineLayout &line = m_impl->lines[static_cast<std::size_t>(i)];
        if (m_impl->autoAlignSuspended) {
            line.y.setParams((m_impl->touchScrolling ? 520 : 190) * fx.springStrength(),
                             (m_impl->touchScrolling ? 46 : 27.5) * fx.springDamping());
        } else
            setPosYSpringPolicy(line, seeking, hasInterlude, intervalMs, m_impl->endOfSong);
        const qreal extra = hasInterlude && i > interludeAnchor
            ? interludeHeight
            : 0.0;
        const qreal target = base + line.top + extra;
        line.y.setTarget(target, delay);
        const qreal scaleTarget = line.highlighted
            ? 1.0
            : (m_impl->playing ? fx.inactiveScale() : 1.0);
        line.scale.setParams(185, 25);
        line.scale.setTarget(scaleTarget, delay * 0.5);

        if (cascade && target + line.height >= 0)
            delay = qMin(0.11, delay + 0.022);
    }
    updateBlurTargets();
    scheduleFrame();
}

void AmllLyricsView::updateBlurTargets()
{
    const auto &fx = LyricEffectSettings::instance();
    const bool enabled = fx.distanceBlur() && !m_impl->pointerInside
        && !m_impl->autoAlignSuspended;
    const int latest = m_impl->latestHighlighted >= 0
        ? m_impl->latestHighlighted : m_impl->scrollToIndex;
    for (int i = 0; i < static_cast<int>(m_impl->lines.size()); ++i) {
        auto &line = m_impl->lines[static_cast<std::size_t>(i)];
        const int distance = i < m_impl->scrollToIndex
            ? m_impl->scrollToIndex - i + 1 : qAbs(i - latest);
        line.blurTarget = enabled && !line.highlighted
            ? fx.blurStrength() * qMin(kBlurMax, (width() <= 1024 ? 0.8 : 1.0) * (1.0 + distance))
            : 0.0;
    }
}

void AmllLyricsView::scheduleFrame()
{
    if (!m_impl->frameTimer->isActive() && isVisible() && !m_impl->lines.empty()) {
        m_impl->frameClock.start();
        m_impl->frameTimer->start();
    }
}

void AmllLyricsView::updateTimeline(qint64 positionMs, bool jumped)
{
    if (m_impl->lines.empty() || (!jumped && positionMs == m_impl->positionMs))
        return;

    const int previousScrollTo = m_impl->scrollToIndex;
    const int previousInterlude = m_impl->activeInterlude;
    const bool previousEnd = m_impl->endOfSong;
    m_impl->positionMs = positionMs;

    bool addedPlaying = false;
    bool anyPlaying = false;
    for (auto &line : m_impl->lines) {
        const bool playing = positionMs >= line.startMs && positionMs < line.endMs;
        addedPlaying |= playing && !line.playing;
        anyPlaying |= playing;
        line.playing = playing;
    }

    int active = -1;
    for (int i = 0; i < static_cast<int>(m_impl->interludes.size()); ++i) {
        const auto &inter = m_impl->interludes[static_cast<std::size_t>(i)];
        if (positionMs >= inter.startMs && positionMs < inter.endMs) {
            active = i;
            break;
        }
    }
    m_impl->activeInterlude = active;
    if (active >= 0) {
        if (active != m_impl->visibleInterlude) {
            m_impl->visibleInterlude = active;
            m_impl->interludeVisibility = 0;
        }
        m_impl->interludePositionMs = positionMs;
    }
    m_impl->endOfSong = m_impl->maxEndMs > 0 && positionMs >= m_impl->maxEndMs;

    int firstHighlighted = -1;
    int latestHighlighted = -1;
    bool highlightedChanged = false;
    for (int i = 0; i < static_cast<int>(m_impl->lines.size()); ++i) {
        auto &line = m_impl->lines[static_cast<std::size_t>(i)];
        const bool wasHighlighted = line.highlighted;
        if (m_impl->endOfSong || (active >= 0 && !anyPlaying))
            line.highlighted = false;
        else if (addedPlaying || jumped)
            line.highlighted = line.playing;
        highlightedChanged |= wasHighlighted != line.highlighted;
        if (line.highlighted) {
            if (firstHighlighted < 0)
                firstHighlighted = i;
            latestHighlighted = i;
        }
    }
    if (firstHighlighted >= 0)
        m_impl->scrollToIndex = firstHighlighted;
    else if (jumped && active < 0 && !m_impl->endOfSong) {
        const auto next = std::upper_bound(m_impl->lines.cbegin(), m_impl->lines.cend(), positionMs,
            [](qint64 position, const LineLayout &line) { return position < line.startMs; });
        m_impl->scrollToIndex = qMax(0, int(next - m_impl->lines.cbegin()) - 1);
    }
    m_impl->latestHighlighted = latestHighlighted;

    const bool resume = m_impl->autoAlignSuspended && !m_impl->touchScrolling
        && (jumped || (addedPlaying && qFuzzyIsNull(m_impl->inertiaVelocity)
                       && QDateTime::currentMSecsSinceEpoch() - m_impl->interactionEndMs
                           >= kAutoAlignResumeDelayMs));
    if (resume) {
        m_impl->manualScroll = 0;
        m_impl->autoAlignSuspended = false;
        m_impl->inertiaVelocity = 0;
    }
    if (jumped || resume || previousInterlude != active || highlightedChanged
        || previousScrollTo != m_impl->scrollToIndex || previousEnd != m_impl->endOfSong) {
        updateLineTargets(!jumped, jumped);
    }
}

void AmllLyricsView::setPosition(qint64 positionMs, bool seek)
{
    if (m_impl->lyrics.isEmpty() || height() <= 0)
        return;
    positionMs = qMax<qint64>(0, positionMs);
    if (m_impl->waitingForSeek) {
        // 点击歌词后，等待引擎确认，避免用残留的旧进度反向拉动歌词。
        if (!seek && m_impl->sampleClock.isValid() && m_impl->sampleClock.elapsed() < 350
            && qAbs(positionMs - m_impl->sampledPositionMs) > 120) {
            return;
        }
        m_impl->waitingForSeek = false;
    }
    const bool jumped = seek || positionMs < m_impl->sampledPositionMs - 250
        || positionMs > m_impl->positionMs + 500;
    if (!m_impl->sampleClock.isValid() || jumped || positionMs != m_impl->sampledPositionMs) {
        m_impl->sampledPositionMs = positionMs;
        m_impl->sampleClock.start();
    }
    // 播放时只补齐相邻位置通知之间的小空隙，不倒播扫光，也不在缓冲时继续跑。
    if (jumped || !m_impl->playing || !isVisible() || positionMs > m_impl->positionMs)
        updateTimeline(positionMs, jumped);
    scheduleFrame();
    update();
}

void AmllLyricsView::setPlaying(bool playing)
{
    if (m_impl->playing == playing)
        return;
    m_impl->playing = playing;
    m_impl->sampleClock.invalidate();
    const auto &fx = LyricEffectSettings::instance();
    for (auto &line : m_impl->lines)
        line.scale.setTarget(playing && !line.highlighted ? fx.inactiveScale() : 1.0);
    scheduleFrame();
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
    for (LineLayout &line : m_impl->lines) {
        line.y.setPosition(line.y.target());
        line.blur = line.blurTarget;
    }
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
    const qreal dpr = devicePixelRatioF();
    if (line.cacheWidth == logicalWidth && !line.sharpCache.isNull()
        && line.sharpCache.devicePixelRatio() == dpr) {
        return line.sharpCache;
    }

    QImage image(qCeil(logicalWidth * dpr), qCeil(logicalHeight * dpr),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(dpr);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

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
    if (line.glyphs.empty() || (!fx.wordSweep() && !fx.wordFloat() && !fx.emphasisGlow())) {
        painter.setPen(m_impl->activeColor);
        line.main.layout->draw(&painter, QPointF(0, padY));
        drawSubLines();
        return;
    }

    for (const auto &glyph : line.glyphs) {
        if (glyph.text.isEmpty())
            continue;
        QPen pen(fx.wordSweep() ? m_impl->dimColor : m_impl->activeColor);
        qreal floatY = 0.0;
        qreal emphasis = 0.0;
        if (glyph.word >= 0) {
            const auto &word = line.words[static_cast<std::size_t>(glyph.word)];
            const qreal elapsed = m_impl->positionMs - word.startMs;
            const qreal duration = qMax<qreal>(1.0, word.endMs - word.startMs);
            const qreal progress = std::clamp(elapsed / duration, 0.0, 1.0);
            if (progress >= 1.0) {
                pen.setColor(m_impl->activeColor);
            } else if (progress > 0.0 && fx.wordSweep()) {
                // 按文字的实际宽度推进柔边扫光，而不是整字一起切换颜色。
                const qreal feather = qMin(0.25, fontPx * 0.23 / word.sweepWidth);
                const qreal head = progress * (1.0 + 2.0 * feather) - feather;
                if (head - feather >= glyph.sweepEnd) {
                    pen.setColor(m_impl->activeColor);
                } else if (head + feather > glyph.sweepStart) {
                    const qreal fraction = glyph.sweepEnd - glyph.sweepStart;
                    if (qAbs(glyph.advance) >= 1.0) {
                        const auto xAt = [&](qreal t) {
                            return glyph.x + glyph.advance * (t - glyph.sweepStart) / fraction;
                        };
                        QLinearGradient sweep(xAt(head - feather), 0, xAt(head + feather), 0);
                        sweep.setColorAt(0.0, m_impl->activeColor);
                        sweep.setColorAt(0.25, blendColor(m_impl->activeColor, m_impl->dimColor, 0.16));
                        sweep.setColorAt(0.75, blendColor(m_impl->activeColor, m_impl->dimColor, 0.84));
                        sweep.setColorAt(1.0, m_impl->dimColor);
                        pen.setBrush(sweep);
                    } else {
                        pen.setColor(blendColor(m_impl->dimColor, m_impl->activeColor,
                            smoothStep((head + feather - glyph.sweepStart) / (2.0 * feather))));
                    }
                }
            }
            if (fx.wordFloat()) {
                const qreal lift = easeOutCubic(elapsed / qBound(360.0, duration, 900.0));
                floatY = -fontPx * (line.background ? 0.065 : 0.045) * lift;
            }
            if (fx.emphasisGlow() && word.canEmphasize) {
                const qreal pulse = std::sin(progress * kPi);
                emphasis = pulse * pulse;
            }
        }

        const QPointF position(qMin(glyph.x, glyph.x + glyph.advance),
                               padY + glyph.baseline + floatY);
        painter.save();
        if (emphasis > 0.001) {
            const QPointF pivot = position + QPointF(qAbs(glyph.advance) * 0.5, -fontPx * 0.3);
            painter.translate(pivot);
            const qreal scale = 1.0 + emphasis * 0.047;
            painter.scale(scale, scale);
            painter.translate(-pivot);
            QColor glow = m_impl->activeColor;
            glow.setAlphaF(glow.alphaF() * 0.11 * emphasis);
            painter.setPen(glow);
            painter.drawText(position + QPointF(-0.85, -0.6), glyph.text);
            painter.drawText(position + QPointF(0.85, 0.35), glyph.text);
        }
        painter.setPen(pen);
        painter.drawText(position, glyph.text);
        painter.restore();
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
    const qreal sidePadding = qBound<qreal>(16.0, fontPx, 34.0);

    for (int i = 0; i < static_cast<int>(m_impl->lines.size()); ++i) {
        auto &line = m_impl->lines[static_cast<std::size_t>(i)];
        const qreal y = line.y.current();
        const qreal scale = line.scale.current();
        const qreal scaledHeight = line.height * scale;
        if (y + scaledHeight < -16 || y > height() + 16) {
            line.screenRect = {};
            continue;
        }

        line.screenRect = QRectF(0, y, width(), qMax(1.0, scaledHeight));
        if (line.hover > 0.0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 255, 255, qRound(19 * smoothStep(line.hover))));
            painter.drawRoundedRect(
                QRectF(sidePadding * 0.35, y + 2,
                       width() - sidePadding * 0.70, qMax(1.0, scaledHeight - 4)),
                8, 8);
        }

        const qreal focus = smoothStep(line.highlight);
        const bool lineTimed = line.glyphs.empty();
        painter.save();
        painter.translate(sidePadding, y);
        painter.scale(scale, scale);
        if (focus < 1.0) {
            const QImage sharp = sharpLineImage(i);
            QImage image = sharp;
            const int key = qRound(line.blur * 4.0);
            if (key > 0) {
                const auto cached = line.blurCache.constFind(key);
                if (cached != line.blurCache.cend()) {
                    image = cached.value();
                } else {
                    image = blurImage(sharp, key / 4.0);
                    if (line.blurCache.size() >= 6)
                        line.blurCache.erase(line.blurCache.begin());
                    line.blurCache.insert(key, image);
                }
            }
            painter.setOpacity((1.0 - focus) * (lineTimed ? 0.20 : 1.0));
            painter.drawImage(QPointF(0, 0), image);
        }
        if (focus > 0.0) {
            painter.setOpacity(0.85 * focus);
            drawKaraokeLine(painter, i);
        }
        painter.restore();
    }

    if (m_impl->visibleInterlude >= 0 && m_impl->interludeVisibility > 0.0) {
        const auto &interlude = m_impl->interludes[static_cast<std::size_t>(m_impl->visibleInterlude)];
        const auto &anchor = m_impl->lines[static_cast<std::size_t>(interlude.anchorIndex)];
        const qreal visibility = smoothStep(m_impl->interludeVisibility);
        const qreal dotSize = fontPx * 0.22;
        const qreal spacing = fontPx * 0.44;
        const qreal y = anchor.y.current() + anchor.height + fontPx * (0.35 + 0.25 * visibility);
        const qreal time = m_impl->interludePositionMs - interlude.startMs;
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < 3; ++i) {
            const qreal wave = 0.5 - 0.5 * std::cos(2.0 * kPi * (time / 1350.0 - i * 0.17));
            const qreal radius = dotSize * (0.44 + 0.13 * wave);
            QColor dot = m_impl->activeColor;
            dot.setAlphaF(dot.alphaF() * visibility * (0.38 + 0.45 * wave));
            painter.setBrush(dot);
            painter.drawEllipse(QPointF(sidePadding + dotSize * 0.5 + i * spacing,
                                        y - fontPx * 0.12 * wave), radius, radius);
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
    scheduleFrame();
}

void AmllLyricsView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_impl->frameTimer->stop();
    m_impl->inertiaVelocity = 0;
    m_impl->touchScrolling = false;
    m_impl->pointerInside = false;
    m_impl->hoveredIndex = -1;
    updateBlurTargets();
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
        m_impl->frozenAnchorBase = m_impl->anchorBase;
    m_impl->manualScroll += delta;
    m_impl->autoAlignSuspended = true;
    m_impl->interactionEndMs = QDateTime::currentMSecsSinceEpoch();
    m_impl->inertiaVelocity = 0;
    updateLineTargets(false, false);
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
    const bool entered = !m_impl->pointerInside;
    m_impl->pointerInside = true;
    const int index = lineIndexAt(event->position());
    if (entered) {
        updateBlurTargets();
        scheduleFrame();
    }
    if (m_impl->hoveredIndex != index) {
        m_impl->hoveredIndex = index;
        setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        scheduleFrame();
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
            setPosition(targetMs, true);
            m_impl->waitingForSeek = true;
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
    updateBlurTargets();
    scheduleFrame();
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
                m_impl->frozenAnchorBase = m_impl->anchorBase;
            m_impl->inertiaVelocity = 0;
            m_impl->touchScrolling = true;
            m_impl->touchStart = points.first().position();
            m_impl->touchLastY = m_impl->touchStart.y();
            m_impl->touchStartScroll = m_impl->manualScroll;
            m_impl->touchVelocity = 0;
            m_impl->touchLastTimeMs = QDateTime::currentMSecsSinceEpoch();
            m_impl->interactionEndMs = m_impl->touchLastTimeMs;
            m_impl->autoAlignSuspended = true;
            updateLineTargets(false, false);
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
            const qreal velocity = (y - m_impl->touchLastY) / dt;
            m_impl->touchVelocity += (velocity - m_impl->touchVelocity)
                * (1.0 - std::exp(-dt / 0.026));
            m_impl->touchLastY = y;
            m_impl->touchLastTimeMs = now;
            m_impl->interactionEndMs = now;
            updateLineTargets(false, false);
            update();
            touch->accept();
            return true;
        }
        if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (event->type() == QEvent::TouchEnd && now - m_impl->touchLastTimeMs <= 90) {
                m_impl->inertiaVelocity = std::clamp<qreal>(
                    m_impl->touchVelocity, -2200.0, 2200.0);
                if (qAbs(m_impl->inertiaVelocity) < 20.0)
                    m_impl->inertiaVelocity = 0;
            } else {
                m_impl->inertiaVelocity = 0;
            }
            m_impl->touchScrolling = false;
            m_impl->interactionEndMs = now;
            updateLineTargets(false, false);
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
