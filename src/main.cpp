/**
 * @file main.cpp
 * @brief NekoMusic 日系动漫风入口
 */

#include <QApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QDebug>
#include <QSettings>
#include <QStyleFactory>
#include <QLocalSocket>
#include <QLocalServer>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include "core/applog.h"
#include "core/crashhandler.h"
#include <QStandardPaths>
#include <QProcess>
#include <QUrl>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include "ui/mainwindow.h"
#include "ui/scrollareafix.h"
#include "ui/legaldialog.h"
#include "core/i18n.h"
#include "core/legaltext.h"
#include "core/playlistdb.h"
#include "core/landevicemanager.h"
#include "core/localmusicmeta.h"
#include "version.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// 单实例服务器名称
static const QString kServerName = QStringLiteral("NekoMusicSingleInstance");

#ifdef Q_OS_LINUX
static QString desktopExecutable(const QString &path)
{
    QString escaped = path;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

static QString linuxDesktopEntry(const QString &appPath)
{
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Version=1.5\n"
                          "Name=Neko歌姬计划\n"
                          "Name[en]=Neko Music\n"
                          "Comment=高品质无损音乐播放器 (Qt6 + C++)\n"
                          "Comment[en]=High-quality Lossless Music Player\n"
                          "Exec=")
            + desktopExecutable(appPath)
            + QStringLiteral(
                    " %U\n"
                    "Icon=nekomusic\n"
                    "Terminal=false\n"
                    "StartupNotify=true\n"
                    "StartupWMClass=NekoMusic\n"
                    "Categories=AudioVideo;Audio;Player;Qt;Music;\n"
                    "Keywords=Music;Player;Neko;Flac;Lossless;\n"
                    "MimeType=audio/flac;audio/mpeg;audio/ogg;audio/x-wav;audio/aac;audio/mp4;"
                    "audio/mpegurl;audio/vnd.rn-realaudio;audio/x-mpegurl;audio/x-ms-wma;"
                    "audio/x-musepack;audio/x-pn-realaudio;audio/x-scpls;audio/x-speex;"
                    "x-scheme-handler/nekomusic;\n");
}

static QString runAndCapture(const QString &program, const QStringList &arguments, int timeoutMs = 1500)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs))
        return {};
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

/**
 * 保证 Linux 当前用户能接管 nekomusic://。
 *
 * <p>系统包里的 desktop 文件可能来自旧版本；这里同时刷新用户级 desktop 文件、MIME 数据库和默认
 * 协议关联。只在没有默认处理器时设置默认值，避免覆盖用户自行选择的处理器。</p>
 */
static void ensureLinuxUrlHandler()
{
    const QString appPath = QCoreApplication::applicationFilePath();
    const bool installedLocation = appPath == QStringLiteral("/opt/Neko歌姬计划/NekoMusic")
            || appPath.startsWith(QStringLiteral("/usr/bin/"))
            || appPath.startsWith(QStringLiteral("/usr/local/bin/"));
    if (!installedLocation || !QFileInfo(appPath).isExecutable())
        return;

    const QString appsDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (appsDir.isEmpty() || !QDir().mkpath(appsDir)) {
        qWarning() << "NekoMusic: cannot create user applications directory for URL handler";
        return;
    }

    const QString desktopPath = QDir(appsDir).filePath(QStringLiteral("nekomusic.desktop"));
    const QByteArray expected = linuxDesktopEntry(appPath).toUtf8();
    bool writeDesktop = true;
    QFile existing(desktopPath);
    if (existing.open(QIODevice::ReadOnly))
        writeDesktop = existing.readAll() != expected;
    if (writeDesktop) {
        QSaveFile desktop(desktopPath);
        if (desktop.open(QIODevice::WriteOnly)
            && desktop.write(expected) == expected.size()
            && desktop.commit()) {
            QFile::setPermissions(desktopPath,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                          | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        } else {
            qWarning() << "NekoMusic: cannot write URL handler desktop entry:" << desktopPath;
            return;
        }
    }

    const QString updateDatabase = QStandardPaths::findExecutable(QStringLiteral("update-desktop-database"));
    if (!updateDatabase.isEmpty())
        runAndCapture(updateDatabase, {appsDir});

    const QString xdgMime = QStandardPaths::findExecutable(QStringLiteral("xdg-mime"));
    const QString current = xdgMime.isEmpty()
                                ? QString()
                                : runAndCapture(xdgMime, {
                                      QStringLiteral("query"), QStringLiteral("default"),
                                      QStringLiteral("x-scheme-handler/nekomusic")});
    if (!current.isEmpty())
        return;

    bool associated = false;
    if (!xdgMime.isEmpty()) {
        runAndCapture(xdgMime, {QStringLiteral("default"), QStringLiteral("nekomusic.desktop"),
                                QStringLiteral("x-scheme-handler/nekomusic")});
        associated = runAndCapture(xdgMime, {
                                       QStringLiteral("query"), QStringLiteral("default"),
                                       QStringLiteral("x-scheme-handler/nekomusic")})
                             == QStringLiteral("nekomusic.desktop");
    }

    const QString xdgSettings = QStandardPaths::findExecutable(QStringLiteral("xdg-settings"));
    if (!xdgSettings.isEmpty()) {
        runAndCapture(xdgSettings, {
            QStringLiteral("set"), QStringLiteral("default-url-scheme-handler"),
            QStringLiteral("nekomusic"), QStringLiteral("nekomusic.desktop")});
    }

    const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
    if (!associated && !gio.isEmpty()) {
        runAndCapture(gio, {QStringLiteral("mime"), QStringLiteral("x-scheme-handler/nekomusic"),
                            QStringLiteral("nekomusic.desktop")});
    }
}
#endif

static QString firstLaunchAudioPath(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromUtf8(argv[i]);
        if (a.startsWith(QLatin1Char('-')))
            continue;
        const QString local = LocalMusic::normalizeOpenPathArgument(a);
        if (local.isEmpty())
            continue;
        QFileInfo fi(local);
        if (!fi.exists() || !fi.isFile())
            continue;
        const QString abs = fi.canonicalFilePath().isEmpty() ? fi.absoluteFilePath() : fi.canonicalFilePath();
        if (LocalMusic::isSupportedLocalAudioFile(abs))
            return abs;
    }
    return {};
}

static QUrl firstLaunchSsoUrl(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString value = QString::fromUtf8(argv[i]);
        const QUrl url(value);
        if (url.isValid() && url.scheme().compare(QStringLiteral("nekomusic"), Qt::CaseInsensitive) == 0
            && url.host().compare(QStringLiteral("sso"), Qt::CaseInsensitive) == 0) {
            return url;
        }
    }
    return {};
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_LINUX
    // Qt 6 多媒体默认 FFmpeg；旧版设置的 "pulse" 对 FFmpeg 无效，且可能干扰设备选择。
    // 若遇播放崩溃，可在启动前尝试：QT_MEDIA_BACKEND=gstreamer（需系统安装 GStreamer Qt 插件）
    // 或保持默认 FFmpeg。PipeWire 日志中的 spaVisitChoice 解析告警一般可忽略。
#endif

    // 单实例互斥量：QLocalSocket 探测只等 350ms，已有实例主线程繁忙时会超时，
    // 结果就是连点两次图标起了两个窗口（两个进程还会抢 7788 MCP 端口 / 播放设备）。
    // 命名互斥量由内核维护，进程退出（包括崩溃）时自动释放，因此可以放心兜底。
#ifdef Q_OS_WIN
    HANDLE singleInstanceMutex =
            ::CreateMutexW(nullptr, FALSE, L"Local\\NekoMusic-SingleInstance-v1");
    const bool alreadyRunning =
            singleInstanceMutex != nullptr && ::GetLastError() == ERROR_ALREADY_EXISTS;
#else
    const bool alreadyRunning = false;
#endif

    // 检查是否已有实例在运行
    QLocalSocket socket;
    socket.connectToServer(kServerName);
    const bool socketConnected = socket.waitForConnected(1500);
    if (alreadyRunning || socketConnected) {
        if (socketConnected) {
            const QUrl ssoUrl = firstLaunchSsoUrl(argc, argv);
            const QString audio = firstLaunchAudioPath(argc, argv);
            if (ssoUrl.isValid()) {
                QByteArray msg = QByteArrayLiteral("SSO\t") + ssoUrl.toString(QUrl::FullyEncoded).toUtf8() + '\n';
                socket.write(msg);
            } else if (!audio.isEmpty()) {
                QByteArray msg = QByteArrayLiteral("PLAY\t") + audio.toUtf8() + '\n';
                socket.write(msg);
            } else {
                socket.write("SHOW\n");
            }
            socket.flush();
            socket.waitForBytesWritten(500);
        } else {
            // 已有实例存在但无响应：不重复启动，避免两个进程同时占用设备/数据库。
            qWarning() << "NekoMusic: 检测到已有实例但无法连接单实例服务，本次启动已放弃。";
        }
#ifdef Q_OS_WIN
        if (singleInstanceMutex)
            ::CloseHandle(singleInstanceMutex);
#endif
        return 0;
    }

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

#if defined(Q_OS_LINUX)
    // 纯 QWidget 应用：禁用 xcb 与 GLX/EGL 集成，避免加载 libGL
    if (qgetenv("QT_XCB_GL_INTEGRATION").isEmpty())
        qputenv("QT_XCB_GL_INTEGRATION", "none");
#endif
    // 无 QML/Quick；避免 RHI 走 OpenGL 后端
    if (qgetenv("QSG_RHI_BACKEND").isEmpty())
        qputenv("QSG_RHI_BACKEND", "software");

    QApplication app(argc, argv);

    nekoInstallSmoothScroll(app);

    // 直连：不读取系统/环境变量代理（如 http_proxy），避免本机代理干扰 API 与流媒体。
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));

#ifdef Q_OS_WIN
    /* Windows 默认「windowsvista/11」样式会大量忽略或错误应用 QSS，自定义界面几乎全废。
     * Fusion 与 Qt Style Sheet 兼容性最好，与 Linux/macOS 观感一致。 */
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);
#endif

    app.setApplicationName(QStringLiteral("NekoMusic"));
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
    app.setOrganizationName(QStringLiteral("NekoMusic"));
    app.setOrganizationDomain(QStringLiteral("nekomusic.local"));

    // 文件日志：必须在设置组织/应用名之后（日志目录取自 AppDataLocation）。
    // 卡死/崩溃后进程可能来不及收尾，因此警告以上等级会立即 flush。
    AppLog::install();
    AppLog::startFreezeWatchdog();
    // 崩溃抓栈：把调用链写入 logs\crash-*.txt（不依赖 WER 转储、不改注册表）
    CrashHandler::install();

#ifdef Q_OS_LINUX
    ensureLinuxUrlHandler();
#endif

    // 加载用户设置
    QSettings settings;
    int lang = settings.value("language", static_cast<int>(I18n::ZhCN)).toInt();
    I18n::instance().setLanguage(static_cast<I18n::Language>(lang));

    // i18n 键集合自检：三张表对不上的键打到日志里，避免界面上直接显示标识符
    // （tr() 现在已经会回退到简中兜底，这里只是把缺口暴露出来便于补齐）
    const QStringList missingI18nKeys = I18n::missingKeys();
    if (!missingI18nKeys.isEmpty()) {
        qWarning() << "[i18n] 语言表缺键" << missingI18nKeys.size() << "个:" << missingI18nKeys;
    }

    // 首次启动：须先阅读并同意用户协议与隐私政策，否则退出
    if (!Legal::hasAcceptedConsent()) {
        LegalConsentDialog consentDialog;
        if (consentDialog.exec() != QDialog::Accepted)
            return 0;
        Legal::acceptConsent();
    }

    // 启动阶段计时：定位首屏到底慢在哪一步
    QElapsedTimer bootTimer;
    bootTimer.start();
    auto bootLog = [&bootTimer](const char *step) {
        qInfo().noquote() << QStringLiteral("[Startup] %1 累计 %2")
                                 .arg(QString::fromLatin1(step), QString::number(bootTimer.elapsed()));
    };

    // 初始化播放列表数据库
    PlaylistDatabase::instance().init();
    bootLog("DB init");

    QFont font(QStringLiteral("Segoe UI Variable"), 14);
    font.setStyleHint(QFont::SansSerif);
    app.setFont(font);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    bootLog("字体/图标");

    QApplication::setQuitOnLastWindowClosed(false);

    MainWindow *window = new MainWindow;
    bootLog("MainWindow 构造");

    // 创建单实例服务器
    QLocalServer::removeServer(kServerName);
    QLocalServer *server = new QLocalServer(&app);
    if (!server->listen(kServerName)) {
        qWarning() << "NekoMusic: single-instance socket listen failed:" << server->errorString()
                   << "- external open may spawn duplicate processes.";
    } else {
        QObject::connect(server, &QLocalServer::newConnection, [server, window]() {
            QLocalSocket *clientSocket = server->nextPendingConnection();
            auto *buf = new QByteArray;

            auto processLine = [window](const QByteArray &raw) {
                QByteArray line = raw;
                line = line.trimmed();
                if (line.isEmpty())
                    return;
                static const QByteArray kPlayPfx = QByteArrayLiteral("PLAY\t");
                static const QByteArray kSsoPfx = QByteArrayLiteral("SSO\t");
                if (line.startsWith(kPlayPfx)) {
                    QString path = QString::fromUtf8(line.mid(kPlayPfx.size())).trimmed();
                    path = LocalMusic::normalizeOpenPathArgument(path);
                    window->show();
                    window->raise();
                    window->activateWindow();
                    if (!path.isEmpty()) {
                        QTimer::singleShot(0, window, [window, path]() {
                            window->openAudioFileFromPath(path);
                        });
                    }
                } else if (line.startsWith(kSsoPfx)) {
                    const QUrl url(QString::fromUtf8(line.mid(kSsoPfx.size())).trimmed());
                    window->show();
                    window->raise();
                    window->activateWindow();
                    QTimer::singleShot(0, window, [window, url]() {
                        window->handleSsoCallback(url);
                    });
                } else if (line == "SHOW") {
                    window->show();
                    window->raise();
                    window->activateWindow();
                }
            };

            QObject::connect(clientSocket, &QLocalSocket::readyRead, clientSocket, [clientSocket, window, buf, processLine]() {
                buf->append(clientSocket->readAll());
                while (true) {
                    const int nl = buf->indexOf('\n');
                    if (nl < 0)
                        break;
                    QByteArray line = buf->left(nl);
                    buf->remove(0, nl + 1);
                    processLine(line);
                }
            });
            QObject::connect(clientSocket, &QLocalSocket::disconnected, clientSocket, [clientSocket, buf, processLine]() {
                if (!buf->isEmpty())
                    processLine(*buf);
                delete buf;
                clientSocket->deleteLater();
            });
        });
    }

    window->show();

    const QString launchAudio = firstLaunchAudioPath(argc, argv);
    if (!launchAudio.isEmpty()) {
        QTimer::singleShot(0, window, [window, launchAudio]() { window->openAudioFileFromPath(launchAudio); });
    }
    const QUrl launchSsoUrl = firstLaunchSsoUrl(argc, argv);
    if (launchSsoUrl.isValid()) {
        QTimer::singleShot(0, window, [window, launchSsoUrl]() {
            window->handleSsoCallback(launchSsoUrl);
        });
    }

    int result = app.exec();

    qInfo().noquote() << QStringLiteral("==== NekoMusic 退出，事件循环返回 %1 ====").arg(result);

    LanDeviceManager::instance().stop();
    // 清理数据库
    PlaylistDatabase::instance().close();
    AppLog::flush();

    AppLog::flush();

#ifdef Q_OS_WIN
    if (singleInstanceMutex)
        ::CloseHandle(singleInstanceMutex);
#endif

    return result;
}
