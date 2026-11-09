#include "httpmediadevice.h"
#include "linuxtmpfscache.h"

#include "core/applog.h"

#include <QCoreApplication>
#include <QDir>
#include <QMetaObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <QThread>

namespace {

constexpr int kReadTimeoutMs = 20000;
constexpr int kSizeWaitMs = 4000;
constexpr int kThreadStopWaitMs = 3000;

/** Content-Range: bytes 0-123/456 → 456；解析失败返回 -1。 */
qint64 totalFromContentRange(const QByteArray &value)
{
    const int slash = value.lastIndexOf('/');
    if (slash < 0)
        return -1;
    bool ok = false;
    const qint64 total = value.mid(slash + 1).trimmed().toLongLong(&ok);
    return ok && total > 0 ? total : -1;
}

/** 当前线程是否为 GUI（主）线程：它永远不会被允许阻塞等待音频数据。 */
bool isGuiThread()
{
    const QCoreApplication *app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}

} // namespace

HttpMediaDevice::HttpMediaDevice(const QUrl &url, QObject *parent)
    : QIODevice(parent)
    , m_url(url)
{
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    // 网络 I/O 必须脱离 GUI 线程：GUI 线程在 FFmpeg 打开媒体源时会同步阻塞，
    // 若 readyRead 投递到 GUI 线程，解复用线程将永远等不到数据（详见头文件）。
    m_netThread.setObjectName(QStringLiteral("neko-http-media"));
    m_netThread.start();
    m_nam.moveToThread(&m_netThread);

    // 线程真正退出后再释放临时文件：退出前它可能还在 readyRead 里写文件。
    // 用队列连接回到本对象所在线程（GUI），不阻塞任何调用点。
    connect(&m_netThread, &QThread::finished, this, [this]() {
        QMutexLocker locker(&m_mutex);
        m_file.reset();
        m_finished = true;
        m_cond.wakeAll();
    }, Qt::QueuedConnection);
}

HttpMediaDevice::~HttpMediaDevice()
{
    // 析构只发生在 PlayerEngine/播放器销毁时（此时后端线程已 join），
    // 因此这里等待网络线程退出是可以接受的；运行期一律走 shutdown()。
    m_shutdown = true;
    abort();
    m_netThread.quit();
    if (m_netThread.isRunning() && !m_netThread.wait(kThreadStopWaitMs)) {
        // terminate() 会在任意位置打断线程，可能留下被持有的 Qt 内部锁；
        // 只有确实卡死时才用，并务必在日志里留痕。
        qCritical().noquote() << QStringLiteral("[Stream] 网络线程 %1 内未退出，强制终止（可能处于异常状态）")
                                     .arg(AppLog::msText(kThreadStopWaitMs));
        m_netThread.terminate();
        m_netThread.wait(1000);
    }
    QMutexLocker locker(&m_mutex);
    m_file.reset();
}

void HttpMediaDevice::shutdown()
{
    if (m_shutdown)
        return;
    m_shutdown = true;

    QElapsedTimer timer;
    timer.start();
    abort();
    m_netThread.quit();
    // 不在 GUI 线程上 wait()：切歌时每次都要退役旧设备，等满 3 秒就等于卡死。
    // 线程真正退出后会经由 m_netThread.finished 回到 GUI 线程清理临时文件（见构造函数）。
    qInfo().noquote() << QStringLiteral("[Stream] 设备退役 耗时%1 %2")
                             .arg(AppLog::msText(timer.elapsed()), AppLog::safeUrl(m_url));
}

QNetworkReply *HttpMediaDevice::peekReply()
{
    QMutexLocker locker(&m_replyMutex);
    return m_reply;
}

QNetworkReply *HttpMediaDevice::takeReply()
{
    QMutexLocker locker(&m_replyMutex);
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    return reply;
}

void HttpMediaDevice::disposeReply(QNetworkReply *reply, bool blocking)
{
    if (!reply)
        return;

    const auto teardown = [reply]() {
        reply->disconnect();
        reply->abort();
        reply->deleteLater();
    };

    // QNetworkReply 不是线程安全的：一定要在它自己的线程上收尾。
    if (QThread::currentThread() == reply->thread()) {
        teardown();
        return;
    }
    if (!m_netThread.isRunning())
        return;
    if (blocking && isGuiThread()) {
        // 理论上不会走到这里（调用点已按线程区分），留一条断言式日志防止回归。
        qWarning().noquote() << QStringLiteral("[Stream] 试图在 GUI 线程阻塞等待网络线程，已降级为异步");
        blocking = false;
    }
    QMetaObject::invokeMethod(reply, teardown,
                              blocking ? Qt::BlockingQueuedConnection : Qt::QueuedConnection);
}

void HttpMediaDevice::start()
{
    if (peekReply())
        return;

    m_started.start();
    m_firstChunkMs = -1;
    qInfo().noquote() << QStringLiteral("[Stream] 发起请求 %1").arg(AppLog::safeUrl(m_url));

    m_file = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/nekomusic-stream-XXXXXX"));
    if (!m_file->open()) {
        fail(QStringLiteral("无法创建音频流临时文件"));
        return;
    }

    QNetworkRequest req(m_url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    // 在工作线程上发起请求：QNetworkReply 归属该线程，其信号也就在该线程触发，
    // 不再依赖可能被 FFmpeg 阻塞的 GUI 事件循环。DirectConnection 保证回调
    // 直接在工作线程执行（回调内部由 m_mutex 保护）。
    QMetaObject::invokeMethod(&m_nam, [this, req]() {
        QNetworkReply *reply = m_nam.get(req);
        if (!reply)
            return;
        {
            QMutexLocker locker(&m_replyMutex);
            m_reply = reply;
        }
        connect(reply, &QNetworkReply::readyRead, this, &HttpMediaDevice::onReadyRead,
                Qt::DirectConnection);
        connect(reply, &QNetworkReply::finished, this, &HttpMediaDevice::onReplyFinished,
                Qt::DirectConnection);
        connect(reply, &QNetworkReply::downloadProgress, this,
                [this](qint64, qint64 total) {
                    if (total <= 0)
                        return;
                    QMutexLocker locker(&m_mutex);
                    if (m_total < 0)
                        m_total = total;
                    m_cond.wakeAll();
                },
                Qt::DirectConnection);
    }, Qt::QueuedConnection);
}

void HttpMediaDevice::abort()
{
    QElapsedTimer timer;
    timer.start();

    QNetworkReply *reply = takeReply();

    {
        QMutexLocker locker(&m_mutex);
        m_finished = true;
        m_cond.wakeAll();
    }

    // GUI 线程绝不能在这里阻塞等待网络线程收尾：WER 里那种 Cross-thread 挂起
    // 就是主线程等一个不回到事件循环的线程造成的。非 GUI 线程保留阻塞语义，
    // 以便调用点之后可以确定请求已经收尾。
    disposeReply(reply, !isGuiThread());

    const qint64 cost = timer.elapsed();
    if (cost >= 100)
        qWarning().noquote() << QStringLiteral("[Stream] abort 耗时 %1（%2）")
                                    .arg(AppLog::msText(cost), AppLog::safeUrl(m_url));
}

qint64 HttpMediaDevice::size() const
{
    QMutexLocker locker(&m_mutex);
    // 解复用线程等响应头到达再回答，避免 FFmpeg 认为长度未知而算错时长；
    // GUI 线程绝不能阻塞，否则又会与媒体打开互相等待。
    if (m_total < 0 && QThread::currentThread() != thread() && !isGuiThread()) {
        const int loops = kSizeWaitMs / 100;
        for (int i = 0; i < loops && m_total < 0 && !m_finished && !m_failed; ++i)
            m_cond.wait(&m_mutex, 100);
    }
    return m_total;
}

qint64 HttpMediaDevice::bytesAvailable() const
{
    QMutexLocker locker(&m_mutex);
    return qMax<qint64>(0, m_downloaded - m_position) + QIODevice::bytesAvailable();
}

bool HttpMediaDevice::atEnd() const
{
    QMutexLocker locker(&m_mutex);
    return m_finished && m_position >= m_downloaded;
}

qint64 HttpMediaDevice::pos() const
{
    QMutexLocker locker(&m_mutex);
    return m_position;
}

bool HttpMediaDevice::seek(qint64 position)
{
    QMutexLocker locker(&m_mutex);
    if (position < 0 || m_failed)
        return false;
    m_position = position;
    return true;
}

QString HttpMediaDevice::errorString() const
{
    QMutexLocker locker(&m_mutex);
    return m_error;
}

bool HttpMediaDevice::hasFailed() const
{
    QMutexLocker locker(&m_mutex);
    return m_failed;
}

QString HttpMediaDevice::appendChunk(const QByteArray &chunk)
{
    if (chunk.isEmpty())
        return QString();

    QMutexLocker locker(&m_mutex);
    if (m_failed || m_finished)
        return QString();
    if (m_downloaded + chunk.size() > LinuxTmpfsCache::kMaxAudioFileBytes)
        return QStringLiteral("音频体积超过 500 MiB，已取消播放");
    if (!m_file || !m_file->seek(m_downloaded) || m_file->write(chunk) != chunk.size())
        return QStringLiteral("写入音频流临时文件失败");
    m_file->flush();
    m_downloaded += chunk.size();
    if (m_firstChunkMs < 0 && m_started.isValid()) {
        m_firstChunkMs = m_started.elapsed();
        qInfo().noquote() << QStringLiteral("[Stream] 首包到达 耗时%1（%2）")
                                 .arg(AppLog::msText(m_firstChunkMs), AppLog::safeUrl(m_url));
    }
    m_cond.wakeAll();
    return QString();
}

qint64 HttpMediaDevice::readData(char *data, qint64 maxlen)
{
    if (maxlen <= 0)
        return 0;

    QMutexLocker locker(&m_mutex);
    while (!m_failed && !m_finished && m_position >= m_downloaded) {
        if (!m_cond.wait(&m_mutex, kReadTimeoutMs)) {
            locker.unlock();
            failFromAnyThread(QStringLiteral("音频流读取超时"));
            return -1;
        }
    }

    if (m_position >= m_downloaded) {
        if (m_finished && !m_failed)
            return 0; // 正常读完
        return -1;
    }

    if (!m_file || !m_file->seek(m_position))
        return -1;

    const qint64 toRead = qMin(maxlen, m_downloaded - m_position);
    const qint64 read = m_file->read(data, toRead);
    if (read > 0)
        m_position += read;
    return read;
}

void HttpMediaDevice::onReadyRead()
{
    // 运行在工作线程（DirectConnection）：即使 GUI 线程正被 FFmpeg 阻塞，
    // 这里依旧能把数据写进缓冲并唤醒解复用线程。
    QNetworkReply *reply = peekReply();
    if (!reply)
        return;

    {
        QMutexLocker locker(&m_mutex);
        if (m_total < 0) {
            const qint64 fromRange = totalFromContentRange(reply->rawHeader("Content-Range"));
            const qint64 fromHeader = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            m_total = fromRange > 0 ? fromRange : (fromHeader > 0 ? fromHeader : -1);
        }
    }

    const QString error = appendChunk(reply->readAll());
    if (!error.isEmpty())
        failFromAnyThread(error);
}

void HttpMediaDevice::onReplyFinished()
{
    QNetworkReply *reply = takeReply();
    if (!reply)
        return;

    const QString appendError = appendChunk(reply->readAll());
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorText = reply->errorString();
    reply->deleteLater(); // 与请求同线程，安全

    if (!appendError.isEmpty()) {
        failFromAnyThread(appendError);
        return;
    }

    if (error == QNetworkReply::NoError) {
        QMutexLocker locker(&m_mutex);
        m_finished = true;
        m_cond.wakeAll();
        return;
    }

    if (error != QNetworkReply::OperationCanceledError)
        failFromAnyThread(QStringLiteral("音频流请求失败: %1").arg(errorText));
}

void HttpMediaDevice::fail(const QString &error)
{
    qWarning().noquote() << QStringLiteral("[Stream] 失败：%1（%2）")
                                .arg(error, AppLog::safeUrl(m_url));
    {
        QMutexLocker locker(&m_mutex);
        if (!m_failed) {
            m_failed = true;
            m_error = error;
        }
        m_finished = true;
        m_cond.wakeAll();
    }

    disposeReply(takeReply(), false);

    if (!m_errorEmitted) {
        m_errorEmitted = true;
        emit fetchError(error);
    }
}

void HttpMediaDevice::failFromAnyThread(const QString &error)
{
    {
        QMutexLocker locker(&m_mutex);
        if (!m_failed) {
            m_failed = true;
            m_error = error;
        }
        m_finished = true;
        m_cond.wakeAll();
    }
    // 仅错误信号与网络收尾切回设备线程；读取路径已在上面被唤醒。
    QMetaObject::invokeMethod(this, [this, error]() { fail(error); }, Qt::QueuedConnection);
}
