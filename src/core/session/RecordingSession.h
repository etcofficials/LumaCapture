#pragma once

#include "session/SessionTypes.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace luma::encode { class FramePool; class VideoEncoder; }
namespace luma::mux { class Muxer; }
namespace luma::audio { class AudioEngine; }

namespace luma::session {

class CaptureLoop;
class CompositionState;
class EncodeLoop;
class PauseTimeline;
template <typename T> class BoundedQueue;

struct SessionInfo {
    std::wstring adapterName;
    unsigned sourceWidth = 0;
    unsigned sourceHeight = 0;
    int width = 0, height = 0, fps = 0;
    int featureLevel = 0;         // D3D_FEATURE_LEVEL value
    std::string encoderDescription;
    int audioTracks = 0;
    unsigned framePoolSize = 0;
    size_t framePoolBytes = 0;
    // Kept for luma-bench output.
    std::wstring outputName;
    unsigned desktopWidth = 0;
    unsigned desktopHeight = 0;
    double refreshHz = 0;
};

// Cheap snapshot for the UI (poll a few times per second).
struct SessionStatus {
    double elapsedSeconds = 0;  // recorded time (pauses excluded)
    bool paused = false;
    int64_t capturedFrames = 0;
    int64_t droppedFrames = 0;
    int64_t encodedFrames = 0;
    uint64_t bytes = 0;         // encoded audio + video
    size_t queueDepth = 0;
    std::string sourceStatus;   // e.g. "window minimized"
    std::string audioStatus;
    bool failed = false;
    std::string error;
    float systemPeak = 0, micPeak = 0; // since the previous snapshot
    bool micMuted = false;
};

// One recording: capture thread + encoder thread around a bounded queue,
// plus the audio engine, all writing into one muxer. Qt-free: the GUI and
// luma-bench share exactly this code.
class RecordingSession {
public:
    explicit RecordingSession(SessionConfig config);
    ~RecordingSession();
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    // Native (even) pixel size of a source, before any scaling.
    static std::pair<int, int> nativeSize(const SourceConfig& source);

    // Initialises capture, encoders and the file, then starts all threads.
    // Throws if anything fails to initialise (no file is left half-open).
    void start();
    // Stops capture, drains queues, flushes encoders and finalises the file.
    // Thread-safe and idempotent: later calls return once the first one is done.
    void stop();

    void pause();
    void resume();
    bool paused() const;
    void setMicMuted(bool muted);
    void setAudioVolumes(float system, float mic);
    void setMicFilters(const audio::MicFilterSettings& s);
    void setComposition(const gpu::CompositionSettings& s);

    SessionStatus status();
    bool failed() const { return m_failed.load(); }
    std::string errorMessage() const;

    const SessionInfo& info() const { return m_info; }
    const SessionConfig& config() const { return m_config; }
    const SessionCounters& counters() const { return m_counters; }
    size_t queueDepth() const;
    int64_t startQpc() const { return m_startQpc.load(); }

    // Valid after stop().
    const LatencySamples& latency() const { return m_latency; }
    double captureThreadCpuSeconds() const;
    double encoderFlushMs() const;
    double stopDurationMs() const { return m_stopMs; }

private:
    void setError(const std::string& message);
    void writeRecoveryMarker();
    void removeRecoveryMarker();

    SessionConfig m_config;
    SessionInfo m_info;
    SessionCounters m_counters;
    LatencySamples m_latency;

    std::unique_ptr<PauseTimeline> m_pause;
    std::unique_ptr<CompositionState> m_composition;
    std::unique_ptr<encode::FramePool> m_pool;
    std::unique_ptr<BoundedQueue<FrameTicket>> m_queue;
    std::unique_ptr<encode::VideoEncoder> m_encoder;
    std::unique_ptr<audio::AudioEngine> m_audio;
    std::unique_ptr<mux::Muxer> m_muxer;
    std::unique_ptr<CaptureLoop> m_capture;
    std::unique_ptr<EncodeLoop> m_encode;

    std::thread m_captureThread;
    std::thread m_encodeThread;
    std::atomic<bool> m_stop{false};
    std::atomic<int64_t> m_startQpc{0};
    std::atomic<bool> m_failed{false};
    std::mutex m_lifecycle;        // serialises start()/stop() (stop may be requested from several places)
    mutable std::mutex m_errorMutex;
    std::string m_error;
    bool m_running = false;
    bool m_timerPeriodSet = false;
    double m_stopMs = 0;
};

} // namespace luma::session
