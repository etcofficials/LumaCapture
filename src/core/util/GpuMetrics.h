#pragma once

#include <optional>
#include <string>
#include <vector>

namespace luma {

struct GpuSample {
    double busiestEnginePercent = 0; // max over engines of the summed per-process usage (Task Manager "GPU" figure)
    double engine3dPercent = 0;      // all processes, 3D engine(s)
    double copyEnginePercent = 0;    // all processes, copy engine(s)
    double thisProcessPercent = 0;   // this process, busiest engine
};

// Reads the WDDM "GPU Engine(*)\Utilization Percentage" performance counters
// through PDH. These are the same counters Task Manager uses; they are
// produced by the Windows graphics kernel (WDDM 2.0+), not estimated.
class GpuMetrics {
public:
    GpuMetrics();
    ~GpuMetrics();
    GpuMetrics(const GpuMetrics&) = delete;
    GpuMetrics& operator=(const GpuMetrics&) = delete;

    bool available() const { return m_query != nullptr; }
    const std::string& unavailableReason() const { return m_reason; }

    // Returns nothing when the counters are unavailable or not yet primed.
    std::optional<GpuSample> sample();

private:
    void* m_query = nullptr;   // PDH_HQUERY
    void* m_counter = nullptr; // PDH_HCOUNTER
    std::vector<unsigned char> m_buffer;
    std::string m_reason;
    unsigned long m_pid = 0;
};

} // namespace luma
