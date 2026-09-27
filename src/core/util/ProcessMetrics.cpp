#include "util/ProcessMetrics.h"

#include "util/QpcClock.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>

namespace luma {
namespace {

uint64_t toU64(const FILETIME& ft)
{
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

uint64_t processCpu100ns()
{
    FILETIME creation, exit, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
    return toU64(kernel) + toU64(user);
}

void systemTimes(uint64_t& idle, uint64_t& total)
{
    FILETIME i, k, u;
    GetSystemTimes(&i, &k, &u);
    idle = toU64(i);
    total = toU64(k) + toU64(u); // kernel time includes idle time
}

} // namespace

ProcessMetrics::ProcessMetrics()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    m_cpuCount = std::max<unsigned>(1, si.dwNumberOfProcessors);
    m_lastProcTime = processCpu100ns();
    systemTimes(m_lastSysIdle, m_lastSysTotal);
    m_lastQpc = qpc::now();
}

ProcessSample ProcessMetrics::sample()
{
    ProcessSample s;

    const uint64_t procTime = processCpu100ns();
    uint64_t sysIdle, sysTotal;
    systemTimes(sysIdle, sysTotal);
    const int64_t nowQpc = qpc::now();

    const double wall100ns = qpc::toSeconds(nowQpc - m_lastQpc) * 1e7;
    if (wall100ns > 0)
        s.processCpuPercent = 100.0 * static_cast<double>(procTime - m_lastProcTime) / (wall100ns * m_cpuCount);
    const uint64_t dTotal = sysTotal - m_lastSysTotal;
    if (dTotal > 0)
        s.systemCpuPercent = 100.0 * static_cast<double>(dTotal - (sysIdle - m_lastSysIdle)) / static_cast<double>(dTotal);

    m_lastProcTime = procTime;
    m_lastSysIdle = sysIdle;
    m_lastSysTotal = sysTotal;
    m_lastQpc = nowQpc;

    PROCESS_MEMORY_COUNTERS_EX mem{};
    mem.cb = sizeof(mem);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem), sizeof(mem))) {
        s.workingSetBytes = mem.WorkingSetSize;
        s.privateBytes = mem.PrivateUsage;
        s.peakWorkingSetBytes = mem.PeakWorkingSetSize;
    }
    return s;
}

double currentThreadCpuSeconds()
{
    FILETIME creation, exit, kernel, user;
    GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user);
    return static_cast<double>(toU64(kernel) + toU64(user)) / 1e7;
}

} // namespace luma
