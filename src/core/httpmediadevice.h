#pragma once

/**
 * @file httpmediadevice.h
 * @brief 走应用统一网络栈的可 seek HTTP 音频流设备
 *
 * Qt 的 FFmpeg 后端在播放 QUrl 形式的 http(s) 源时，会绕过 Qt Network 直接由
 * libavformat 发起请求，默认 User-Agent 为 `Lavf/<版本>`：这不带客户端的
 * `User-Agent`，避免落到库默认值。
 * SEO 页面，最终报 “Invalid data found when processing input”。
 *
 * 把本设备通过 QMediaPlayer::setSourceDevice() 交给播放器后，所有媒体字节都从
 * NekoNetworkAccessManager 取，User-Agent 与其它接口严格一致；同时保持边下边播
 * 与随机 seek 能力（FFmpeg 需要回退 seek 做探测）。
 *
 * ## 线程模型（务必保留）
 *
 * Qt 的 FFmpeg 后端在打开 QIODevice 形式的媒体源时，会在**调用线程（GUI 线程）**
 * 上同步等待打开完成（`QFutureInterfaceBase::waitForFinished`）。因此网络回调
 * 绝不能再依赖 GUI 线程的事件循环，否则必然死锁：
 *
 *     GUI 线程等媒体打开
 *       -> FFmpeg 解复用线程在 readData() 里等数据
 *         -> 数据只能由 GUI 线程投递 readyRead（但 GUI 线程已被阻塞）
 *
 * 所以统一网络管理器（NekoNetworkAccessManager）与它的请求对象被放在一个
 * **独立工作线程**
 * （m_netThread）上，readyRead/finished/downloadProgress 以 DirectConnection
 * 在该工作线程上直接执行。设备对象本身仍留在创建它的线程（GUI 线程），
 * 于是 `size()` 的“非本线程才等待”判断依旧成立，GUI 线程永不阻塞；
 * 而 readData()/size()/seek() 等 QIODevice 接口方法可被解复用线程任意调用，
 * 内部状态一律由 m_mutex 保护。
 */

#include <QIODevice>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QUrl>
#include <QWaitCondition>

#include <memory>

#include "nekonetworkaccessmanager.h"

class QNetworkReply;
class QTemporaryFile;

class HttpMediaDevice : public QIODevice
{
    Q_OBJECT
public:
    explicit HttpMediaDevice(const QUrl &url, QObject *parent = nullptr);
    ~HttpMediaDevice() override;

    /** 发起请求；需先 open(QIODevice::ReadOnly)。 */
    void start();
    /** 中止请求：让阻塞中的读取立即返回，并停止后台下载。 */
    void abort();

    QUrl url() const { return m_url; }
    QString errorString() const;
    bool hasFailed() const;

    bool isSequential() const override { return false; }
    qint64 size() const override;
    qint64 bytesAvailable() const override;
    bool atEnd() const override;
    qint64 pos() const override;
    bool seek(qint64 pos) override;

signals:
    void fetchError(const QString &error);

protected:
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    /** 追加一段下载数据，成功返回空字符串。 */
    QString appendChunk(const QByteArray &chunk);
    void onReadyRead();
    void onReplyFinished();
    /** 设备所在线程内清理请求并广播错误。 */
    void fail(const QString &error);
    /** 任意线程上报错误：仅置位并唤醒，网络清理切回设备线程。 */
    void failFromAnyThread(const QString &error);

    /** 读取当前请求指针（m_replyMutex 保护）。 */
    QNetworkReply *peekReply();
    /** 取走当前请求并置空（m_replyMutex 保护）。 */
    QNetworkReply *takeReply();
    /** 在请求所属线程上断开并释放请求，避免跨线程调用 QNetworkReply。 */
    void disposeReply(QNetworkReply *reply, bool blocking);

    QUrl m_url;
    /**
     * 只承载 m_nam / m_reply 的工作线程：让网络回调与 GUI 事件循环解耦。
     * 必须声明在 m_nam 之前——成员按声明逆序析构，线程对象要在
     * 网络管理器之后销毁，否则 QObject 会活过它的线程。
     */
    QThread m_netThread;
    NekoNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    mutable QMutex m_replyMutex;
    std::unique_ptr<QTemporaryFile> m_file;

    mutable QMutex m_mutex;
    mutable QWaitCondition m_cond;
    qint64 m_total = -1;
    qint64 m_downloaded = 0;
    qint64 m_position = 0;
    bool m_finished = false;
    bool m_failed = false;
    bool m_errorEmitted = false;
    QString m_error;
};
