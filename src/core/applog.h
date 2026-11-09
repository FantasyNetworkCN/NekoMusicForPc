#pragma once

#include <QString>

class QUrl;

/**
 * @file applog.h
 * @brief 应用文件日志：把 qDebug/qInfo/qWarning/qCritical 写到磁盘，便于事后定位卡死与崩溃。
 *
 * 为什么需要它：本项目此前只用 qDebug() 输出到 stderr，从快捷方式（WIN32 子系统）启动时
 * 没有控制台，日志直接丢弃。卡死（WER 里表现成 Application Hang）或崩溃（0xc0000005）
 * 之后无从查起，只能靠猜。
 *
 * 设计要点：
 *  - 每次启动写一个新文件，放在 QStandardPaths::AppDataLocation/logs 下
 *    （Windows 上是 %APPDATA%\\NekoMusic\\NekoMusic\\logs），保留最近 kMaxLogFiles 个；
 *  - 每行带 时间 / 线程 ID / 级别 / 调用位置，跨线程排障时能分清是 GUI 线程还是解码线程；
 *  - 警告及以上等级立即 flush —— 崩溃前最后一条日志必须留在盘上；
 *  - 附带 GUI 事件循环看门狗：主线程被同步阻塞时会留下一条 [GUI] 记录。
 */
namespace AppLog {

/** 安装消息处理器并打开本次运行的日志文件。须在 QApplication 构造、设置组织/应用名之后调用。 */
void install();

/** 当前会话日志文件的绝对路径；未安装时为空。 */
QString filePath();

/** 日志目录（可能尚未创建）。 */
QString directory();

/** 立即把缓冲写盘。 */
void flush();

/**
 * 启动 GUI 卡顿看门狗。
 *
 * 主线程定时器的触发间隔会被同步阻塞拉长，据此可以判断“卡死”发生在什么时刻、
 * 持续了多久。注意它只能在主线程恢复后补报，用来定位时间点而不是抢救界面。
 */
void startFreezeWatchdog();

/** 把 URL 的查询串抹掉，避免把 token/sign 之类的凭证写进日志。 */
QString safeUrl(const QUrl &url);

/** 毫秒数转人类可读文本（>=1000ms 显示为秒）。 */
QString msText(qint64 ms);

} // namespace AppLog
