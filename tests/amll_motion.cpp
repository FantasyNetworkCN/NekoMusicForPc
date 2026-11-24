#include "core/lyriceffectsettings.h"
#include "ui/amllbackgroundwidget.h"
#include "ui/amlllyricsview.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

static int failures = 0;
static void check(bool condition, const char *description)
{
    if (!condition) {
        qCritical() << "FAIL:" << description;
        ++failures;
    } else {
        qInfo() << "PASS:" << description;
    }
}

static int differingPixels(const QImage &a, const QImage &b)
{
    if (a.size() != b.size())
        return std::numeric_limits<int>::max();
    int count = 0;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb *la = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        const QRgb *lb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            if (la[x] != lb[x])
                ++count;
        }
    }
    return count;
}

static qreal alphaCenterY(const QImage &image, const QRect &area)
{
    qreal sum = 0;
    qreal weightedY = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = area.left(); x <= area.right(); ++x) {
            const int alpha = qAlpha(row[x]);
            sum += alpha;
            weightedY += y * alpha;
        }
    }
    return sum > 0 ? weightedY / sum : -1.0;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    app.setOrganizationName(QStringLiteral("NekoMusicAmllMotionTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());

    if (qEnvironmentVariableIsSet("NEKO_UI_TEST_AMLL_GL")) {
        AmllBackgroundWidget background;
        background.resize(360, 240);
        background.setScrimStrength(0.2);
        QPixmap red(64, 64);
        red.fill(QColor("#fe6050"));
        background.setAlbum(red);
        background.show();
        if (!QTest::qWaitFor([&] { return background.isValid(); }, 3000)) {
            check(false, "AMLL 背景可建立 OpenGL 上下文");
            return 1;
        }
        const auto center = [&] {
            const auto frame = background.grabFramebuffer();
            return frame.isNull() ? QColor() : frame.pixelColor(frame.rect().center());
        };
        const QColor initial = center();
        background.setAlbum(red);
        QTest::qWait(320);
        const QColor identical = center();
        check(initial.isValid() && initial.red() > 50
                  && qAbs(initial.red() - identical.red()) <= 2,
              "相同封面交叉淡化时不会暗一下");
        QPixmap blue(64, 64);
        blue.fill(QColor("#507bfe"));
        background.setAlbum(blue);
        QTest::qWait(300);
        const QColor mixing = center();
        QTest::qWait(520);
        const QColor finished = center();
        check(initial.red() > mixing.red() && mixing.red() > finished.red()
                  && initial.blue() < mixing.blue() && mixing.blue() < finished.blue(),
              "不同封面颜色平滑交叉淡化");
        return failures ? 1 : 0;
    }

    {
        AmllLyricsView sweep;
        sweep.resize(460, 300);
        auto &effects = LyricEffectSettings::instance();
        const bool wordFloat = effects.wordFloat();
        effects.setWordFloat(false);
        LyricLine phrase;
        phrase.time = 1000;
        phrase.endTime = 1600;
        phrase.text = QStringLiteral("MMMM");
        phrase.words = {{1000, 1600, phrase.text}};
        sweep.setLyrics({phrase});
        sweep.setPosition(1150, true);
        sweep.show();
        QTest::qWait(240);

        const auto capture = [&] {
            return sweep.grab().toImage().convertToFormat(QImage::Format_ARGB32);
        };
        sweep.setPlaying(true);
        sweep.setPosition(1150);
        const auto first = capture();
        QTest::qWait(72);
        const auto midway = capture();
        QTest::qWait(120);
        const auto bounded = capture();
        check(differingPixels(first, midway) > 30 && differingPixels(midway, bounded) > 30,
              "引擎进度通知之间仍有连续扫光");
        QTest::qWait(180);
        check(differingPixels(bounded, capture()) == 0,
              "进度停滞时扫光不会无限向前");
        sweep.setPosition(1350);
        sweep.setPlaying(false);
        const auto paused = capture();
        QTest::qWait(170);
        check(differingPixels(paused, capture()) == 0, "暂停时逐字动效停在原处");
        sweep.setPosition(1040, true);
        check(differingPixels(paused, capture()) > 30, "回跳会立即回正扫光位置");
        effects.setWordFloat(wordFloat);
    }

    {
        AmllLyricsView interlude;
        interlude.resize(460, 380);
        LyricLine before;
        before.time = 0;
        before.endTime = 2000;
        before.text = QStringLiteral("Before the break");
        LyricLine after;
        after.time = 10000;
        after.endTime = 12000;
        after.text = QStringLiteral("After the break");
        interlude.setLyrics({before, after});
        interlude.show();
        QTest::qWait(280);
        const auto capture = [&] {
            return interlude.grab().toImage().convertToFormat(QImage::Format_ARGB32);
        };
        const auto beforeGap = capture();
        interlude.setPosition(2000, true);
        check(differingPixels(beforeGap, capture()) == 0,
              "进入间奏的首帧不会突然插入留白或圆点");
        QTest::qWait(280);
        const auto inGap = capture();
        check(differingPixels(beforeGap, inGap) > 100, "间奏圆点和留白渐入");
        interlude.setPosition(10000, true);
        check(differingPixels(inGap, capture()) == 0,
              "间奏结束时留白不会立即消失");
    }

    {
        auto &effects = LyricEffectSettings::instance();
        effects.setWordSweep(false);
        effects.setEmphasisGlow(false);
        effects.setDistanceBlur(false);

        AmllLyricsView lift;
        lift.resize(360, 280);
        lift.setTextStyle(Qt::white, QColor(255, 255, 255, 77), Qt::white, 64);
        LyricLine phrase;
        phrase.time = 1000;
        phrase.endTime = 3600;
        phrase.text = QStringLiteral("MM");
        phrase.words = {{2000, 2700, phrase.text}};
        lift.setLyrics({phrase});
        lift.setPosition(2000, true);
        lift.show();
        QTest::qWait(220);
        const auto captureLift = [&] {
            return lift.grab().toImage().convertToFormat(QImage::Format_ARGB32);
        };
        const auto baseline = captureLift();
        QFont mainFont = lift.font();
        mainFont.setPixelSize(64);
        mainFont.setWeight(QFont::Normal);
        mainFont.setHintingPreference(QFont::PreferNoHinting);
        const int half = qRound((34 + QFontMetricsF(mainFont).horizontalAdvance(QLatin1Char('M')))
                                * baseline.devicePixelRatio());
        const QRect left(0, 0, half, baseline.height());
        const QRect right(half, 0, baseline.width() - half, baseline.height());
        const qreal leftBaseline = alphaCenterY(baseline, left);
        const qreal rightBaseline = alphaCenterY(baseline, right);
        lift.setPosition(2025);
        check(leftBaseline >= 0 && rightBaseline >= 0
                  && qAbs(leftBaseline - alphaCenterY(captureLift(), left)) < 0.65,
              "逐字上浮从基线柔和起步");
        lift.setPosition(2240);
        const auto rising = captureLift();
        const qreal leftRise = leftBaseline - alphaCenterY(rising, left);
        const qreal rightRise = rightBaseline - alphaCenterY(rising, right);
        check(leftRise > 1.0 && rightRise > 0.25 && leftRise > rightRise + 0.15,
              "同一词的字形按扫光方向依次上浮");
        lift.setPosition(2650);
        const auto lifted = captureLift();
        check(leftBaseline - alphaCenterY(lifted, left) > leftRise + 0.75,
              "上浮过程平滑推进至最终高度");
        lift.setPosition(500, true);
        check(differingPixels(lifted, captureLift()) == 0,
              "回跳时淡出的旧歌词不会立刻掉回基线");

        AmllLyricsView roll;
        roll.resize(360, 400);
        LyricLine previous;
        previous.time = 1000;
        previous.endTime = 2000;
        previous.text = QStringLiteral("I");
        previous.words = {{1000, 2000, previous.text}};
        LyricLine next;
        next.time = 2000;
        next.endTime = 3000;
        next.text = QStringLiteral("MMMM");
        next.words = {{2000, 3000, next.text}};
        roll.setLyrics({previous, next});
        roll.setPosition(1950, true);
        roll.show();
        QTest::qWait(220);
        const auto nextLineY = [&] {
            const QImage frame = roll.grab().toImage().convertToFormat(QImage::Format_ARGB32);
            const int x = qRound(65 * frame.devicePixelRatio());
            return alphaCenterY(frame, QRect(x, 0, frame.width() - x, frame.height()));
        };
        const qreal before = nextLineY();
        roll.setPosition(2000);
        const qreal firstFrame = nextLineY();
        QTest::qWait(95);
        const qreal during = nextLineY();
        QTest::qWait(500);
        const qreal after = nextLineY();
        check(after > 0 && qAbs(firstFrame - before) < 1.0
                  && before > during + 8.0 && during > after + 8.0,
              "自动切行连续上移，不在换句首帧瞬移");
    }

    return failures ? 1 : 0;
}
