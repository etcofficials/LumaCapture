#pragma once

#include <QString>

#include <functional>

namespace luma::app {

// One background thread for the app's small file writes (settings, history,
// thumbnail cache). The UI thread never waits on the disk: on a slow or failing
// HDD a flush can block for seconds (v1 froze in QSettings::sync on exit).
// Jobs posted with the same key replace a not-yet-started job with that key, so
// only the newest settings/history snapshot is written.
namespace diskworker {

void post(const QString& key, std::function<void()> job);
// Waits (bounded) until all posted jobs have run; returns false on timeout.
bool waitIdle(int timeoutMs);
// Stops the worker after the queue is empty or the timeout expired (app exit).
void shutdown(int timeoutMs);

} // namespace diskworker
} // namespace luma::app
