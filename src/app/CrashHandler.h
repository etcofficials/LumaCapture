#pragma once

#include <QString>

namespace luma::app {

// Writes a minidump (all thread stacks) to <dataDir>/crashdumps on:
//  * unhandled SEH exceptions / crashes,
//  * std::terminate, pure virtual calls, invalid CRT parameters,
//  * UI hangs: a watchdog thread notices when the UI thread stops responding
//    for 10 s and writes one "hang" dump per episode (the process keeps running).
// Nothing leaves the PC; dumps stay on G: next to the application.
namespace crash {

void install(const QString& dumpDir);
// Called by a UI-thread timer; the watchdog compares against it.
void heartbeat();
void startHangWatchdog();
void stopHangWatchdog();

} // namespace crash
} // namespace luma::app
