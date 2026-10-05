#include "util/Log.h"

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace luma::log {
namespace {

// Log lines are formatted by the caller and written to the file by a background
// thread, so no thread - in particular the UI thread - ever waits on the disk to
// log (a stalling HDD used to freeze whichever thread logged). A rate limit stops
// a misbehaving component from flooding the file.
constexpr size_t kMaxQueuedLines = 4000;
constexpr int kMaxLinesPerSecond = 40;

struct State {
    std::mutex mutex;              // queue + rate limit
    std::condition_variable cv;
    std::deque<std::string> queue;
    std::mutex fileMutex;          // the file object (writer thread vs open/close)
    std::ofstream file;
    std::thread writer;
    bool stop = false;
    bool writerDone = false;
    std::condition_variable doneCv;
    Level minLevel = Level::Info;
    ULONGLONG windowStart = 0;
    int linesInWindow = 0;
    size_t suppressed = 0;
    std::mutex consoleMutex;
};

// Intentionally never destroyed: a writer thread may still be running during static
// destruction / process exit, and it must not touch a destroyed mutex.
State& state()
{
    static State* s = new State;
    return *s;
}

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

std::string stamp()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buf;
}

void writerLoop()
{
    State& s = state();
    std::unique_lock lock(s.mutex);
    for (;;) {
        s.cv.wait(lock, [&] { return s.stop || !s.queue.empty(); });
        std::deque<std::string> batch;
        batch.swap(s.queue);
        const bool stopping = s.stop;
        lock.unlock();
        {
            std::lock_guard fileLock(s.fileMutex);
            if (s.file.is_open()) {
                for (const std::string& line : batch)
                    s.file << line << '\n';
                s.file.flush(); // hands the data to Windows; no forced disk flush
            }
        }
        lock.lock();
        if (stopping && s.queue.empty()) {
            s.writerDone = true;
            s.doneCv.notify_all();
            return;
        }
    }
}

} // namespace

void setMinLevel(Level level)
{
    std::lock_guard lock(state().mutex);
    state().minLevel = level;
}

void openFile(const std::filesystem::path& path, bool append)
{
    State& s = state();
    {
        std::lock_guard fileLock(s.fileMutex);
        s.file.close();
        s.file.open(path, std::ios::out | (append ? std::ios::app : std::ios::trunc));
    }
    std::lock_guard lock(s.mutex);
    if (!s.writer.joinable()) {
        s.stop = false;
        s.writerDone = false;
        s.writer = std::thread(writerLoop);
    }
}

void closeFile()
{
    State& s = state();
    std::unique_lock lock(s.mutex);
    if (!s.writer.joinable())
        return;
    s.stop = true;
    s.cv.notify_all();
    // Give the writer a moment to write what is queued; never hang the exit on a dead disk.
    const bool done = s.doneCv.wait_for(lock, std::chrono::seconds(2), [&] { return s.writerDone; });
    lock.unlock();
    if (done) {
        s.writer.join();
        std::lock_guard fileLock(s.fileMutex);
        s.file.close();
    } else {
        s.writer.detach(); // the state is never destroyed, so this is safe
    }
}

void write(Level level, std::string_view message)
{
    State& s = state();
    std::string line;
    {
        std::lock_guard lock(s.mutex);
        if (level < s.minLevel)
            return;
        // Rate limit per second (errors get some extra room).
        const ULONGLONG now = GetTickCount64();
        if (now - s.windowStart >= 1000) {
            if (s.suppressed > 0 && s.queue.size() < kMaxQueuedLines)
                s.queue.push_back(stamp() + tag(Level::Warn) + " " + std::to_string(s.suppressed) +
                                  " log lines were suppressed (rate limit)");
            s.windowStart = now;
            s.linesInWindow = 0;
            s.suppressed = 0;
        }
        const int limit = level == Level::Error ? 2 * kMaxLinesPerSecond : kMaxLinesPerSecond;
        if (++s.linesInWindow > limit || s.queue.size() >= kMaxQueuedLines) {
            ++s.suppressed;
            return;
        }
        line = stamp() + tag(level) + " " + std::string(message);
        if (s.writer.joinable()) {
            s.queue.push_back(line);
            s.cv.notify_one();
        }
    }
    // Console copy (tools and tests); a GUI process has no console and this is a no-op.
    std::lock_guard consoleLock(s.consoleMutex);
    FILE* stream = level >= Level::Warn ? stderr : stdout;
    std::fprintf(stream, "%s\n", line.c_str() + 13); // without the time stamp
    std::fflush(stream);
}

} // namespace luma::log
