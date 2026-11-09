#pragma once

/**
 * @file crashhandler.h
 * @brief 未处理异常（崩溃）时的调用栈落盘。
 *
 * 背景：WER 事件只给出「模块 + 偏移」，能反查出函数但**看不到调用点**。而本项目的
 * 崩溃集中在 UI 特效/动画生命周期（QWidget::setGraphicsEffect、QAbstractAnimation::stop），
 * 排查必须知道是谁调的。
 *
 * 做法：SetUnhandledExceptionFilter + DbgHelp 遍历栈帧，把模块名、RVA、函数名
 * （有导出符号的函数）写到一个独立的文本文件里。**不修改注册表**，不依赖 WER 转储。
 * 只影响 Windows；其它平台为空实现。
 */
namespace CrashHandler {

/** 安装崩溃处理器。应在日志初始化之后、进入事件循环之前调用。 */
void install();

} // namespace CrashHandler
