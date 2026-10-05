#pragma once

#include "capture/CaptureSource.h"
#include "encode/FfmpegUtil.h"
#include "encode/FramePool.h"
#include "gpu/Compositor.h"
#include "gpu/D3D11Device.h"
#include "gpu/ReadbackRing.h"
#include "session/BoundedQueue.h"
#include "session/PauseTimeline.h"
#include "session/SessionTypes.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace luma::session {

// Composition settings shared between the UI (writer) and the capture thread.
class CompositionState {
public:
    void set(const gpu::CompositionSettings& s)
    {
        std::lock_guard lock(m_mutex);
        m_settings = s;
        ++m_version;
    }
    // Copies the settings if they changed since `version`.
    bool fetch(uint64_t& version, gpu::CompositionSettings& out) const
    {
        std::lock_guard lock(m_mutex);
        if (version == m_version)
            return false;
        version = m_version;
        out = m_settings;
        return true;
    }

private:
    mutable std::mutex m_mutex;
    gpu::CompositionSettings m_settings;
    uint64_t m_version = 1;
};

// The capture thread body: source -> GPU compositor (NV12) -> staging readback
// ring -> FramePool -> bounded queue.
//
// Timing: capture ticks are placed on an ideal QPC grid (start + k/fps). The
// output timestamp is derived from the tick time minus paused time, so
// timestamps stay exact when frames are dropped and across pauses.
//
// Drop policy (the capture thread never waits for the encoder):
//  * queue full           -> the newest frame is dropped (droppedQueueFull);
//                            latency is therefore capped at queueCapacity/fps.
//  * no free frame buffer -> the tick is dropped (droppedPoolEmpty).
//  * tick periods missed because this thread was late -> droppedLateTicks;
//    the grid skips ahead instead of bursting to catch up.
// An unchanged picture repeats the previous frame buffer without any GPU work.
//
// Live preview: when SessionConfig::preview is set, a small BGRA copy of the composed
// frame is rendered at most every previewIntervalMs and read back one tick later
// (never waiting on the GPU). In previewOnly mode nothing is encoded: the loop only
// composes changed pictures and feeds the preview.
class CaptureLoop {
public:
    CaptureLoop(const SessionConfig& config, encode::FramePool& pool, BoundedQueue<FrameTicket>& queue,
                SessionCounters& counters, std::vector<float>& captureLatencyMs, const PauseTimeline& pause,
                const CompositionState& composition);
    ~CaptureLoop();

    // Creates the D3D device, the source and the compositor. Call on the capture thread.
    void init();
    void run(const std::atomic<bool>& stop, int64_t startQpc);

    unsigned sourceWidth() const { return m_source ? m_source->width() : 0; }
    unsigned sourceHeight() const { return m_source ? m_source->height() : 0; }
    std::wstring adapterName() const { return m_device ? m_device->adapterName() : std::wstring(); }
    D3D_FEATURE_LEVEL featureLevel() const { return m_device ? m_device->featureLevel() : D3D_FEATURE_LEVEL{}; }
    double threadCpuSeconds() const { return m_threadCpuSeconds; }
    std::string status() const;

private:
    struct Pending {
        bool readback = false; // true: waits for GPU copy in the readback ring; false: repeat last frame
        int slot = -1;
        int64_t pts = 0;
        int64_t tickQpc = 0;
    };
    struct Click {
        int64_t qpc = 0;
        POINT pos{};
        int button = 0;
    };
    static constexpr unsigned kMaxPending = 64;
    static constexpr unsigned kReadbackPollMs = 2;

    void buildPipeline();
    void buildCompositor();
    void destroyPipeline();
    int64_t tickTime(int64_t k) const;

    void waitForSource(unsigned timeoutMs);
    void tryReopen(unsigned timeoutMs);
    void updateInputs();
    void pollMouseButtons();
    void processTick(int64_t pts, int64_t tickQpc);
    void pushPending(const Pending& p);
    void drainPending(bool forceOne);
    void drainAll();
    void copyToFrame(int slot, const gpu::ReadbackRing::Mapped& mapped);
    void enqueue(int slot, int64_t pts, int64_t tickQpc, bool unique);
    gpu::FrameInputs frameInputs();
    void servicePreview();
    void resetPreview();
    void handleDeviceLost(const std::exception& e);
    void setStatus(const std::string& s);

    SessionConfig m_config;
    encode::FramePool& m_pool;
    BoundedQueue<FrameTicket>& m_queue;
    SessionCounters& m_counters;
    std::vector<float>& m_captureLatencyMs;
    const PauseTimeline& m_pause;
    const CompositionState& m_compositionState;

    std::unique_ptr<gpu::D3D11Device> m_device;
    std::unique_ptr<capture::CaptureSource> m_source;
    std::unique_ptr<gpu::Compositor> m_compositor;
    std::unique_ptr<gpu::ReadbackRing> m_ring;
    ff::SwsPtr m_sws;
    RECT m_crop{};

    gpu::CompositionSettings m_composition;
    uint64_t m_compositionVersion = 0;
    uint64_t m_webcamSeq = 0;
    bool m_webcamShown = false;

    POINT m_lastMouse{-100000, -100000};
    bool m_mouseValid = false;
    std::array<Click, 4> m_clicks{};
    bool m_buttonDown[2] = {};

    std::array<Pending, kMaxPending> m_pending{};
    unsigned m_pendingHead = 0, m_pendingCount = 0;

    // Live preview: one small staging copy, mapped without waiting on a later tick.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_previewStaging;
    unsigned m_previewW = 0, m_previewH = 0;
    bool m_previewPending = false; // a copy is in flight in m_previewStaging
    bool m_previewDirty = false;   // a newer composite exists than the last preview
    int64_t m_lastPreviewQpc = 0;

    int m_lastSlot = -1;     // frame holding the most recent picture (held with one reference)
    bool m_dirty = true;     // picture changed since the last submitted frame
    int64_t m_lastPts = -1;
    int64_t m_startQpc = 0;
    int64_t m_lastReopenQpc = 0;
    double m_threadCpuSeconds = 0;

    mutable std::mutex m_statusMutex;
    std::string m_status;
};

} // namespace luma::session
