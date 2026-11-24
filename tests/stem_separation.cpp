#include "core/apiclient.h"
#include "core/playerengine.h"
#include "core/stemaudiomixer.h"
#include "core/usermanager.h"
#include "theme/theme.h"
#include "ui/playerbar.h"
#include "ui/stemmixerdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMediaDevices>
#include <QNetworkProxy>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

namespace {

QByteArray trackFixture(quint32 seconds = 1)
{
    constexpr quint32 sampleRate = 44100;
    const quint32 frames = sampleRate * seconds;
    QByteArray data;
    QDataStream wav(&data, QIODevice::WriteOnly);
    wav.setByteOrder(QDataStream::LittleEndian);
    wav.writeRawData("RIFF", 4);
    wav << quint32(36 + frames * 2);
    wav.writeRawData("WAVEfmt ", 8);
    wav << quint32(16) << quint16(1) << quint16(1) << sampleRate
        << quint32(sampleRate * 2) << quint16(2) << quint16(16);
    wav.writeRawData("data", 4);
    wav << quint32(frames * 2);
    for (quint32 frame = 0; frame < frames; ++frame)
        wav << qint16(frame % 110 < 55 ? 600 : -600);
    return data;
}

class StemTest : public QObject
{
    Q_OBJECT

private slots:
    void masterVolumeFollowsEngine();
    void cachedJobWithOnlyDone();
    void reportsHttpErrorBody();
};

void StemTest::masterVolumeFollowsEngine()
{
    const auto volumeKey = QStringLiteral("player/volume");
    QSettings().setValue(volumeKey, 47);
    PlayerEngine engine;
    QWidget host;
    PlayerBar bar(&engine, &host);
    StemMixerDialog dialog(nullptr, &engine, {});
    auto *popup = host.findChild<QSlider *>(QStringLiteral("pbVolumeSlider"));
    auto *percentage = host.findChild<QLabel *>(QStringLiteral("pbVolumeLabel"));
    auto *master = dialog.findChild<QSlider *>(QStringLiteral("stemMasterSlider"));
    QVERIFY(popup && percentage && master);
    QCOMPARE(popup->value(), 47);
    QCOMPARE(master->value(), 47);
    QSignalSpy changes(&engine, &PlayerEngine::volumeChanged);
    master->setValue(66);
    QCOMPARE(popup->value(), 66);
    QCOMPARE(percentage->text(), QStringLiteral("66%"));
    popup->setValue(15);
    QCOMPARE(master->value(), 15);
    engine.setVolume(0.394f);
    QCOMPARE(master->value(), 39);
    QCOMPARE(popup->value(), 39);
    QCOMPARE(engine.volume(), 0.394f);
    QCOMPARE(changes.size(), 3);
    QTRY_COMPARE(QSettings().value(volumeKey).toInt(), 39);
}

void StemTest::cachedJobWithOnlyDone()
{
    constexpr int musicId = 319804121;
    const QByteArray audio = trackFixture();
    int streamRequests = 0;
    int trackRequests = 0;
    int authorized = 0;
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QTcpServer::newConnection, &server, [&]() {
        while (auto *socket = server.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, &server, [&, socket, request = QByteArray()]() mutable {
                request += socket->readAll();
                if (!request.contains("\r\n\r\n"))
                    return;
                if (request.toLower().contains("\r\nauthorization: bearer fixture")
                    && request.toLower().contains("\r\nuser-agent: nekomusic-pc/"))
                    ++authorized;
                if (request.startsWith("GET /api/music/file/319804121/stream?separate=1 ")) {
                    if (!request.toLower().contains("\r\naccept: text/event-stream"))
                        return socket->disconnectFromHost();
                    ++streamRequests;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                                  "Connection: close\r\n\r\n: connected\r\n\r\n"
                                  "event: done\r\ndata: {\"vocalsUrl\":\"/vocals.mp3\","
                                  "\"accompanimentUrl\":\"/accompaniment.mp3\"}");
                } else if (request.startsWith("GET /vocals.mp3 ") || request.startsWith("GET /accompaniment.mp3 ")) {
                    ++trackRequests;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: "
                                  + QByteArray::number(audio.size()) + "\r\nConnection: close\r\n\r\n" + audio);
                }
                socket->disconnectFromHost();
            });
        }
    });
    Theme::setApiBase(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    ApiClient api;
    PlayerEngine engine;
    engine.setVolume(0.42f);
    MusicInfo song;
    song.id = musicId;
    StemMixerDialog dialog(&api, &engine, song);
    auto *toggle = dialog.findChild<QCheckBox *>(QStringLiteral("stemToggle"));
    QVERIFY(toggle && !toggle->isEnabled());
    UserManager::instance().setLoginInfo(QStringLiteral("fixture"), {{QStringLiteral("isVip"), true}});
    QVERIFY(toggle->isEnabled());
    toggle->setChecked(true);
    auto *mixer = dialog.findChild<StemAudioMixer *>();
    QVERIFY(mixer);
    QSignalSpy fullReady(mixer, &StemAudioMixer::fullTracksReady);
    QSignalSpy fullFailed(mixer, &StemAudioMixer::fullTracksFailed);
    QSignalSpy output(mixer, &StemAudioMixer::mixOutputChanged);
    auto *original = engine.findChild<QAudioOutput *>();
    QVERIFY(original && !original->isMuted());
    QCOMPARE(engine.volume(), 0.42f);
    QTRY_COMPARE_WITH_TIMEOUT(fullReady.size() + fullFailed.size(), 1, 7000);
    QVERIFY2(fullFailed.isEmpty(), qPrintable(fullFailed.isEmpty() ? QString()
                                         : fullFailed.first().first().toString()));
    QCOMPARE(streamRequests, 1);
    QCOMPARE(trackRequests, 2);
    QCOMPARE(authorized, 3);
    QVERIFY(output.isEmpty());
    QVERIFY(!original->isMuted());
    QCOMPARE(engine.volume(), 0.42f);
    toggle->setChecked(false);
    QCOMPARE(engine.volume(), 0.42f);
    toggle->setChecked(true);
    QCOMPARE(streamRequests, 1);

    auto *vocals = dialog.findChild<QSlider *>(QStringLiteral("stemVocalsSlider"));
    auto *accompaniment = dialog.findChild<QSlider *>(QStringLiteral("stemAccompanimentSlider"));
    QVERIFY(vocals && accompaniment);
    QSignalSpy masterChanges(&engine, &PlayerEngine::volumeChanged);
    const auto vocalKey = QStringLiteral("stem/vocalsGain");
    const auto accompanimentKey = QStringLiteral("stem/accompanimentGain");
    vocals->setSliderDown(true);
    for (int gain = 100; gain >= 37; --gain)
        vocals->setSliderPosition(gain);
    QTest::qWait(320);
    QVERIFY(!QSettings().contains(vocalKey));
    QCOMPARE(vocals->value(), 37);
    vocals->setSliderDown(false);
    accompaniment->setValue(61);
    QTRY_COMPARE(QSettings().value(vocalKey).toInt(), 37);
    QCOMPARE(QSettings().value(accompanimentKey).toInt(), 61);
    QCOMPARE(engine.volume(), 0.42f);
    QVERIFY(masterChanges.isEmpty());

    // 手动运行可用隔离声卡验证 PCM 接管和缺片回退；默认测试不向用户声卡写入。
    if (qEnvironmentVariableIsSet("NEKO_STEM_TEST_AUDIO")) {
        const auto device = QMediaDevices::defaultAudioOutput();
        QVERIFY(!device.isNull());
        QTemporaryDir source;
        QVERIFY(source.isValid());
        QFile wave(source.filePath(QStringLiteral("original.wav")));
        const QByteArray originalAudio = trackFixture(3);
        QVERIFY(wave.open(QIODevice::WriteOnly));
        QCOMPARE(wave.write(originalAudio), originalAudio.size());
        wave.close();
        engine.setOutputDevice(device);
        engine.setCurrentMusic(song);
        engine.play(QUrl::fromLocalFile(wave.fileName()));
        QTRY_VERIFY_WITH_TIMEOUT(original->isMuted() && !output.isEmpty(), 5000);
        QVERIFY(output.last().first().toBool());
        const auto outputChanges = output.size();
        accompaniment->setSliderDown(true);
        for (int gain = 0; gain <= 100; ++gain)
            accompaniment->setSliderPosition(gain);
        QTest::qWait(120);
        QCOMPARE(output.size(), outputChanges);
        QCOMPARE(QSettings().value(accompanimentKey).toInt(), 61);
        QVERIFY(original->isMuted());
        accompaniment->setSliderDown(false);
        engine.setPosition(1400);
        QTRY_VERIFY_WITH_TIMEOUT(engine.position() >= 1400 && !original->isMuted(), 5000);
        QTest::qWait(170);
        QVERIFY(!original->isMuted() && !output.last().first().toBool());
        QCOMPARE(engine.volume(), 0.42f);
        engine.stop();
    }
}

void StemTest::reportsHttpErrorBody()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    connect(&server, &QTcpServer::newConnection, &server, [&]() {
        auto *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, &server, [socket]() {
            socket->readAll();
            socket->write("HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\n"
                          "Connection: close\r\n\r\n{\"message\":\"fixture VIP required\"}");
            socket->disconnectFromHost();
        });
    });
    Theme::setApiBase(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    ApiClient api;
    int status = -1;
    QString detail;
    api.streamStemSeparation(1, {}, [&status, &detail](int code, const QString &message) {
        status = code;
        detail = message;
    });
    QTRY_COMPARE_WITH_TIMEOUT(status, 403, 5000);
    QCOMPARE(detail, QStringLiteral("fixture VIP required"));
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", temporary.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", temporary.filePath(QStringLiteral("cache")).toUtf8());
    QApplication app(argc, argv);
#ifndef Q_OS_UNIX
    QStandardPaths::setTestModeEnabled(true);
#endif
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    app.setOrganizationName(QStringLiteral("NekoMusicStemTest"));
    app.setApplicationName(QFileInfo(temporary.path()).fileName());
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    StemTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "stem_separation.moc"
