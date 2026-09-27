#include "CrashHandler.h"

#include "util/Log.h"

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>

namespace luma::app::crash {
namespace {

std::wstring g_dir;
std::atomic<int64_t> g_lastBeatMs{0};
std::atomic<bool> g_watchdogRun{false};
std::thread g_watchdog;

int64_t nowMs() { return static_cast<int64_t>(GetTickCount64()); }

std::wstring dumpPath(const wchar_t* kind)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[128];
    swprintf_s(name, L"\\LumaCapture-%s-%04u%02u%02u-%02u%02u%02u.dmp", kind, t.wYear, t.wMonth, t.wDay, t.wHour,
               t.wMinute, t.wSecond);
    return g_dir + name;
}

bool writeDump(const wchar_t* kind, EXCEPTION_POINTERS* ep)
{
    if (g_dir.empty())
        return false;
    CreateDirectoryW(g_dir.c_str(), nullptr);
    const std::wstring path = dumpPath(kind);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, type, ep ? &mei : nullptr,
                                      nullptr, nullptr);
    CloseHandle(f);
    return ok != FALSE;
}

LONG WINAPI onUnhandled(EXCEPTION_POINTERS* ep)
{
    log::error("FATAL: unhandled exception 0x{:08X} at {} - writing crash dump",
               static_cast<unsigned>(ep->ExceptionRecord->ExceptionCode), ep->ExceptionRecord->ExceptionAddress);
    writeDump(L"crash", ep);
    log::closeFile();
    return EXCEPTION_EXECUTE_HANDLER;
}

void onTerminate()
{
    log::error("FATAL: std::terminate called - writing crash dump");
    writeDump(L"terminate", nullptr);
    log::closeFile();
    std::abort();
}

void onPureCall()
{
    log::error("FATAL: pure virtual function call - writing crash dump");
    writeDump(L"purecall", nullptr);
    std::abort();
}

void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
{
    log::error("FATAL: invalid CRT parameter - writing crash dump");
    writeDump(L"invalidparam", nullptr);
    std::abort();
}

} // namespace

void install(const QString& dumpDir)
{
    g_dir = dumpDir.toStdWString();
    for (auto& c : g_dir)
        if (c == L'/')
            c = L'\\';
    SetUnhandledExceptionFilter(onUnhandled);
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParameter);
}

void heartbeat()
{
    g_lastBeatMs = nowMs();
}

void startHangWatchdog()
{
    if (g_watchdogRun)
        return;
    heartbeat();
    g_watchdogRun = true;
    g_watchdog = std::thread([] {
        SetThreadDescription(GetCurrentThread(), L"luma-hang-watchdog");
        bool reported = false;
        while (g_watchdogRun) {
            Sleep(1000);
            const int64_t stale = nowMs() - g_lastBeatMs.load();
            if (stale > 10000 && !reported) {
                reported = true;
                log::error("UI thread has not responded for {} s - writing a hang dump for diagnosis", stale / 1000);
                writeDump(L"hang", nullptr);
            } else if (stale < 2000 && reported) {
                reported = false;
                log::warn("UI thread responsive again");
            }
        }
    });
}

void stopHangWatchdog()
{
    g_watchdogRun = false;
    if (g_watchdog.joinable())
        g_watchdog.join();
}

} // namespace luma::app::crash
