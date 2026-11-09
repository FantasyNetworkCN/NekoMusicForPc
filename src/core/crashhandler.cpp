#include "core/crashhandler.h"

#include "core/applog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QString>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#endif

namespace {

#ifdef Q_OS_WIN

/** 递归保护：崩溃处理过程中再次崩溃就直接放行，避免无限循环。 */
std::atomic<bool> g_handling{false};

/** 把一行文本追加到崩溃文件（用 Win32 API，尽量不依赖 Qt 的堆分配）。 */
void writeRaw(HANDLE file, const char *text)
{
    if (!file || file == INVALID_HANDLE_VALUE || !text)
        return;
    DWORD written = 0;
    ::WriteFile(file, text, static_cast<DWORD>(::lstrlenA(text)), &written, nullptr);
}

/** 取模块路径的末段作为简短模块名。 */
QString moduleShortName(HANDLE process, DWORD64 address)
{
    IMAGEHLP_MODULE64 info{};
    info.SizeOfStruct = sizeof(info);
    if (::SymGetModuleInfo64(process, address, &info)) {
        const QString path = QString::fromUtf8(info.ImageName);
        const int slash = path.lastIndexOf(QLatin1Char('\\'));
        return slash >= 0 ? path.mid(slash + 1) : path;
    }
    return QStringLiteral("?");
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS *info)
{
    if (g_handling.exchange(true))
        return EXCEPTION_CONTINUE_SEARCH;

    const DWORD pid = ::GetCurrentProcessId();
    const QString dir = AppLog::directory();
    if (!dir.isEmpty())
        QDir().mkpath(dir);

    QString name = dir.isEmpty()
                       ? QStringLiteral("crash-%1.txt").arg(pid)
                       : QStringLiteral("%1/crash-%2-%3.txt")
                             .arg(dir, QDateTime::currentDateTime().toString(
                                           QStringLiteral("yyyyMMdd-HHmmss")))
                             .arg(pid);

    HANDLE file = ::CreateFileW(reinterpret_cast<LPCWSTR>(name.utf16()), GENERIC_WRITE,
                                FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);

    char line[1024];
    writeRaw(file, "==== NekoMusic 崩溃 ====\r\n");
    const QByteArray head = QStringLiteral("时间: %1\r\n进程: %2\r\n\r\n")
                                .arg(QDateTime::currentDateTime().toString(Qt::ISODate))
                                .arg(pid)
                                .toUtf8();
    writeRaw(file, head.constData());

    if (info && info->ExceptionRecord) {
        const EXCEPTION_RECORD *rec = info->ExceptionRecord;
        const QByteArray ex = QStringLiteral("异常: 0x%1  地址: %2\r\n\r\n")
                                  .arg(static_cast<quint32>(rec->ExceptionCode), 8, 16,
                                       QLatin1Char('0'))
                                  .arg(reinterpret_cast<quintptr>(rec->ExceptionAddress), 0, 16)
                                  .toUtf8();
        writeRaw(file, ex.constData());
    }

    const HANDLE process = ::GetCurrentProcess();
    ::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES
                    | SYMOPT_FAIL_CRITICAL_ERRORS);
    if (!::SymInitialize(process, nullptr, TRUE))
        writeRaw(file, "[warn] SymInitialize 失败，栈里只有模块+偏移\r\n");

    HANDLE thread = ::GetCurrentThread();
    CONTEXT context{};
    if (info && info->ContextRecord)
        context = *info->ContextRecord;
    else
        ::RtlCaptureContext(&context);

    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    writeRaw(file, "调用栈（#0 为崩溃点）:\r\n");
    for (int i = 0; i < 64; ++i) {
        if (!::StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
                           nullptr, ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0)
            break;

        const DWORD64 pc = frame.AddrPC.Offset;
        const QString module = moduleShortName(process, pc);
        const DWORD64 base = ::SymGetModuleBase64(process, pc);
        const DWORD64 rva = base ? pc - base : pc;

        char symbol[512] = {0};
        DWORD64 displacement = 0;
        BYTE buffer[sizeof(SYMBOL_INFO) + sizeof(symbol)] = {0};
        auto *sym = reinterpret_cast<SYMBOL_INFO *>(buffer);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = sizeof(symbol) - 1;

        QString funcText;
        QString lineText;
        if (::SymFromAddr(process, pc, &displacement, sym)) {
            funcText = QString::fromUtf8(sym->Name).left(200);
            IMAGEHLP_LINE64 srcLine{};
            srcLine.SizeOfStruct = sizeof(srcLine);
            DWORD lineDisplacement = 0;
            if (::SymGetLineFromAddr64(process, pc, &lineDisplacement, &srcLine)) {
                lineText = QStringLiteral("  %1:%2")
                               .arg(QString::fromUtf8(srcLine.FileName))
                               .arg(srcLine.LineNumber);
            }
        }

        if (funcText.isEmpty()) {
            std::snprintf(line, sizeof(line), "#%02d %p  %s+0x%llx\r\n", i,
                          reinterpret_cast<void *>(pc), module.toUtf8().constData(),
                          static_cast<unsigned long long>(rva));
        } else {
            std::snprintf(line, sizeof(line), "#%02d %p  %s+0x%llx  %s%s\r\n", i,
                          reinterpret_cast<void *>(pc), module.toUtf8().constData(),
                          static_cast<unsigned long long>(rva), funcText.toUtf8().constData(),
                          lineText.toUtf8().constData());
        }
        writeRaw(file, line);
    }

    writeRaw(file, "==== 结束 ====\r\n");
    ::SymCleanup(process);
    if (file != INVALID_HANDLE_VALUE)
        ::CloseHandle(file);

    AppLog::flush();
    return EXCEPTION_CONTINUE_SEARCH;
}

#endif // Q_OS_WIN

} // namespace

namespace CrashHandler {

void install()
{
#ifdef Q_OS_WIN
    ::SetUnhandledExceptionFilter(&onUnhandledException);
    qInfo().noquote() << QStringLiteral("崩溃抓栈已启用（输出到 %1/crash-*.txt）")
                             .arg(AppLog::directory());
#endif
}

} // namespace CrashHandler
