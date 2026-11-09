#include "core/applog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>

#include <cstdio>

namespace {

/** 保留的日志文件个数（按修改时间淘汰最旧的）。 */
constexpr int kMaxLogFiles = 20;
/** 非紧急消息的最长滞留时间，避免高频 qDebug 每次都落盘。 */
constexpr qint64 kFlushIntervalMs = 500;

/** 看门狗定时器间隔。 */
constexpr int kWatchdogIntervalMs = 200;
/** 超过该间隔认为主线程被明显阻塞，记 info。 */
constexpr qint64 kWarnGapMs = 600;
/** 超过该间隔认为发生卡死，记 warning。 */
constexpr qint64 kHangGapMs = 1200;

QMutex g_mutex;
QFile g_file;
QString g_dir;
QString g_path;
QElapsedTimer g_sinceFlush;
bool g_installed = false;

const char *levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO ";
    case QtWarningMsg:
        return "WARN ";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }
    return "?????";
}

/** 调用线程的短标识：同一线程内稳定，跨线程可区分。 */
QString threadTag()
{
    const auto id = reinterpret_cast<quintptr>(QThread::currentThreadId());
    return QStringLiteral("T%1").arg(QString::number(id, 16).rightJustified(4, QLatin1Char('0')));
}

void writeToFile(const QString &line, bool urgent)
{
    QMutexLocker locker(&g_mutex);
    if (!g_file.isOpen())
        return;
    g_file.write(line.toUtf8());
    g_file.write("\n", 1);
    if (urgent || !g_sinceFlush.isValid() || g_sinceFlush.elapsed() >= kFlushIntervalMs) {
        g_file.flush();
        g_sinceFlush.restart();
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // 写日志的过程中若再触发日志（例如 QFile 内部 qWarning），不要递归。
    static thread_local bool reentered = false;
    if (reentered)
        return;

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    QString line = QStringLiteral("[%1][%2][%3] %4")
                       .arg(stamp, threadTag(), QString::fromLatin1(levelName(type)), message);
    if (context.file && *context.file) {
        const QString file = QString::fromUtf8(context.file);
        // 只保留 src/ 之后的相对路径，日志更短也更好读
        const int pos = file.indexOf(QStringLiteral("src/"));
        line += QStringLiteral("  (%1:%2)")
                    .arg(pos >= 0 ? file.mid(pos) : QFileInfo(file).fileName())
                    .arg(context.line);
    }

    reentered = true;
    writeToFile(line, type != QtDebugMsg && type != QtInfoMsg);
    // 保留 stderr 输出：命令行启动（含调试器）时仍能直接看到
    std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    reentered = false;

    if (type == QtFatalMsg) {
        QMutexLocker locker(&g_mutex);
        if (g_file.isOpen())
            g_file.flush();
    }
}

/** 按修改时间清理旧日志，只保留最近 kMaxLogFiles-1 个（留出一个给本次）。 */
void pruneOldFiles(const QString &dir)
{
    QDir d(dir);
    const QFileInfoList files = d.entryInfoList({QStringLiteral("neko-*.log")}, QDir::Files,
                                                QDir::Time);
    for (int i = kMaxLogFiles - 1; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}

/** 主线程事件循环看门狗：用定时器的实际触发间隔反推阻塞时长。 */
class FreezeWatchdog final : public QObject
{
public:
    explicit FreezeWatchdog(QObject *parent)
        : QObject(parent)
    {
        m_last.start();
        // 注意：m_timer 是成员，绝不能 setParent(this)，否则父对象析构时会连同成员一起 delete。
        m_timer.setInterval(kWatchdogIntervalMs);
        m_timer.setTimerType(Qt::CoarseTimer);
        connect(&m_timer, &QTimer::timeout, this, [this]() { onTick(); });
        m_timer.start();
    }

private:
    void onTick()
    {
        const qint64 gap = m_last.restart();
        if (gap >= kHangGapMs) {
            qWarning().noquote() << QStringLiteral("[GUI] 主线程事件循环停摆 %1，疑似卡死（该条为恢复后补报）")
                                        .arg(AppLog::msText(gap));
        } else if (gap >= kWarnGapMs) {
            qInfo().noquote() << QStringLiteral("[GUI] 主线程延迟 %1（同步阻塞中）")
                                     .arg(AppLog::msText(gap));
        }
    }

    QTimer m_timer;
    QElapsedTimer m_last;
};

} // namespace

namespace AppLog {

void install()
{
    if (g_installed)
        return;

    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QCoreApplication::applicationDirPath();
    g_dir = base + QStringLiteral("/logs");
    QDir().mkpath(g_dir);
    pruneOldFiles(g_dir);

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const qint64 pid = QCoreApplication::instance() ? QCoreApplication::applicationPid() : 0;
    g_path = g_dir + QStringLiteral("/neko-%1-%2.log").arg(stamp).arg(pid);

    g_file.setFileName(g_path);
    // Unbuffered：每条日志直接 WriteFile，进程被强杀/崩溃也不会丢最后几行。
    // （之前只对 WARN 以上 flush，结果安静启动时日志文件一直是 0 字节。）
    if (!g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered)) {
        // 打不开就退化成原来的 stderr 行为，不能让日志把启动搞崩
        std::fprintf(stderr, "[AppLog] 无法打开日志文件: %s\n", g_path.toLocal8Bit().constData());
        g_path.clear();
        return;
    }
    g_sinceFlush.start();
    qInstallMessageHandler(messageHandler);
    g_installed = true;

    qInfo().noquote() << QStringLiteral("==== NekoMusic 启动 ====");
    qInfo().noquote() << QStringLiteral("版本 %1  进程 %2  Qt %3")
                             .arg(QCoreApplication::applicationVersion())
                             .arg(pid)
                             .arg(QString::fromLatin1(qVersion()));
    qInfo().noquote() << QStringLiteral("日志文件 %1").arg(g_path);
    flush();
}

QString filePath()
{
    return g_path;
}

QString directory()
{
    return g_dir;
}

void flush()
{
    QMutexLocker locker(&g_mutex);
    if (g_file.isOpen())
        g_file.flush();
}

void startFreezeWatchdog()
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;
    static bool started = false;
    if (started)
        return;
    started = true;
    new FreezeWatchdog(app);
    qInfo().noquote() << QStringLiteral("GUI 卡顿看门狗已启动（间隔 %1ms，超过 %2 记警告）")
                             .arg(kWatchdogIntervalMs)
                             .arg(msText(kHangGapMs));
}

QString safeUrl(const QUrl &url)
{
    QUrl copy = url;
    copy.setUserInfo(QString());
    if (copy.hasQuery())
        copy.setQuery(QStringLiteral("***"));
    return copy.toString();
}

QString msText(qint64 ms)
{
    if (ms < 1000)
        return QStringLiteral("%1ms").arg(ms);
    return QStringLiteral("%1s").arg(QString::number(ms / 1000.0, 'f', 2));
}

} // namespace AppLog
