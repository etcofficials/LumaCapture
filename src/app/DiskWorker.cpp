#include "DiskWorker.h"

#include "util/Log.h"

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

namespace luma::app::diskworker {
namespace {

struct Job {
    QString key;
    std::function<void()> fn;
};

struct Worker {
    std::mutex mutex;
    std::condition_variable cv;     // work available / stop
    std::condition_variable idleCv; // queue drained
    std::deque<Job> queue;
    std::thread thread;
    bool running = false; // a job is executing
    bool stop = false;
    bool started = false;
};

// Never destroyed: the thread may still be finishing a write during process exit.
Worker& worker()
{
    static Worker* w = new Worker;
    return *w;
}

void loop()
{
    SetThreadDescription(GetCurrentThread(), L"luma-disk");
    Worker& w = worker();
    std::unique_lock lock(w.mutex);
    for (;;) {
        w.cv.wait(lock, [&] { return w.stop || !w.queue.empty(); });
        if (w.queue.empty()) {
            if (w.stop)
                return;
            continue;
        }
        Job job = std::move(w.queue.front());
        w.queue.pop_front();
        w.running = true;
        lock.unlock();
        try {
            job.fn();
        } catch (const std::exception& e) {
            log::error("Background write '{}' failed: {}", job.key.toStdString(), e.what());
        } catch (...) {
            log::error("Background write '{}' failed", job.key.toStdString());
        }
        lock.lock();
        w.running = false;
        if (w.queue.empty())
            w.idleCv.notify_all();
    }
}

} // namespace

void post(const QString& key, std::function<void()> job)
{
    Worker& w = worker();
    {
        std::lock_guard lock(w.mutex);
        if (w.stop)
            return;
        if (!w.started) {
            w.started = true;
            w.thread = std::thread(loop);
        }
        bool replaced = false;
        if (!key.isEmpty()) {
            for (Job& j : w.queue) {
                if (j.key == key) {
                    j.fn = std::move(job); // newest snapshot wins
                    replaced = true;
                    break;
                }
            }
        }
        if (!replaced)
            w.queue.push_back(Job{key, std::move(job)});
    }
    w.cv.notify_one();
}

bool waitIdle(int timeoutMs)
{
    Worker& w = worker();
    std::unique_lock lock(w.mutex);
    return w.idleCv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                             [&] { return w.queue.empty() && !w.running; });
}

void shutdown(int timeoutMs)
{
    const bool idle = waitIdle(timeoutMs);
    Worker& w = worker();
    {
        std::lock_guard lock(w.mutex);
        w.stop = true;
    }
    w.cv.notify_all();
    if (!w.thread.joinable())
        return;
    if (idle)
        w.thread.join();
    else
        w.thread.detach(); // a write is stuck on the disk; never hang the exit (state is never destroyed)
}

} // namespace luma::app::diskworker
