#include "util/Log.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <mutex>

namespace luma::log {
namespace {

std::mutex g_mutex;
Level g_minLevel = Level::Info;
std::ofstream g_file;

const char* tag(Level level)
{
    switch (level) {
    case Level::Debug: return "[DEBUG]";
    case Level::Info:  return "[INFO] ";
    case Level::Warn:  return "[WARN] ";
    case Level::Error: return "[ERROR]";
    }
    return "[?]";
}

} // namespace

void setMinLevel(Level level)
{
    std::lock_guard lock(g_mutex);
    g_minLevel = level;
}

void openFile(const std::filesystem::path& path, bool append)
{
    std::lock_guard lock(g_mutex);
    g_file.close();
    g_file.open(path, std::ios::out | (append ? std::ios::app : std::ios::trunc));
}

void closeFile()
{
    std::lock_guard lock(g_mutex);
    g_file.close();
}

void write(Level level, std::string_view message)
{
    std::lock_guard lock(g_mutex);
    if (level < g_minLevel)
        return;
    FILE* stream = level >= Level::Warn ? stderr : stdout;
    std::fprintf(stream, "%s %.*s\n", tag(level), static_cast<int>(message.size()), message.data());
    std::fflush(stream);
    if (g_file.is_open()) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        char stamp[32];
        std::snprintf(stamp, sizeof(stamp), "%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        g_file << stamp << tag(level) << ' ' << message << '\n' << std::flush;
    }
}

} // namespace luma::log
