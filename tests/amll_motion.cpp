#include "core/lyriceffectsettings.h"
#include "ui/amllbackgroundwidget.h"
#include "ui/amlllyricsview.h"

#include <QApplication>
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

    return failures ? 1 : 0;
}
