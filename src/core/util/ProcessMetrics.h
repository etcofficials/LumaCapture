#pragma once

#include <cstdint>

namespace luma {

struct ProcessSample {
    double processCpuPercent = 0;  // this process, % of the whole machine (all logical CPUs = 100 %)
    double systemCpuPercent = 0;   // whole machine, all processes
    uint64_t workingSetBytes = 0;
    uint64_t privateBytes = 0;
    uint64_t peakWorkingSetBytes = 0;
};

// Measures CPU usage as deltas between successive sample() calls using
// GetProcessTimes/GetSystemTimes, and memory via GetProcessMemoryInfo.
class ProcessMetrics {
public:
    ProcessMetrics();
    ProcessSample sample();

private:
    uint64_t m_lastProcTime = 0;
    uint64_t m_lastSysIdle = 0;
    uint64_t m_lastSysTotal = 0;
    int64_t m_lastQpc = 0;
    unsigned m_cpuCount = 1;
};

// CPU time consumed by the calling thread so far, in seconds.
double currentThreadCpuSeconds();

} // namespace luma
