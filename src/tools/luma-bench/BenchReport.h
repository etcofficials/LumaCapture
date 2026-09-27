#pragma once

#include "BenchArgs.h"

#include "session/RecordingSession.h"
#include "util/GpuMetrics.h"
#include "util/ProcessMetrics.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace luma::bench {

// One measurement per wall-clock second while recording.
struct SecondSample {
    double elapsedSeconds = 0;
    double captureFps = 0;  // frames entering the encoder queue during this second
    double encodeFps = 0;   // packets leaving the encoder during this second
    double bitrateMbps = 0; // encoded video bytes produced during this second
    int64_t queueDepth = 0; // at the sample instant
    ProcessSample process;
    std::optional<GpuSample> gpu;
};

class BenchMonitor {
public:
    explicit BenchMonitor(const session::RecordingSession& session);

    // Takes one sample (call about once per second).
    const SecondSample& sample();
    void printLive(const SecondSample& s) const;

    const std::vector<SecondSample>& samples() const { return m_samples; }
    const GpuMetrics& gpu() const { return m_gpu; }

private:
    const session::RecordingSession& m_session;
    ProcessMetrics m_process;
    GpuMetrics m_gpu;
    std::vector<SecondSample> m_samples;
    int64_t m_lastQpc = 0;
    int64_t m_lastEnqueued = 0;
    int64_t m_lastEncoded = 0;
    int64_t m_lastBytes = 0;
};

struct RunResult {
    double wallSeconds = 0;       // start -> stop request
    uint64_t fileBytes = 0;
    bool failed = false;
    std::string error;
};

void printSummary(const BenchArgs& args, const session::RecordingSession& session, const BenchMonitor& monitor,
                  const RunResult& result, const std::filesystem::path& file);

} // namespace luma::bench
