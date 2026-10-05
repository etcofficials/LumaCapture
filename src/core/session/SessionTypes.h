#pragma once

#include "audio/AudioEngine.h"
#include "encode/VideoEncoder.h"
#include "gpu/CompositionSettings.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace luma::webcam { class FrameExchange; }

namespace luma::session {

class PreviewExchange;

enum class SourceKind { Display, Window, Region };

struct SourceConfig {
    SourceKind kind = SourceKind::Display;
    unsigned outputIndex = 0;  // Display: index into gpu::enumerateOutputs()
    HWND window = nullptr;     // Window
    RECT region{};             // Region: screen coordinates (inside one monitor)
    bool captureCursor = true;
    RECT crop{};               // optional crop in source pixels (Display/Window); empty = none
};

struct SessionConfig {
    SourceConfig source;
    int width = 0;             // encoded size; 0 = native source size
    int height = 0;
    int fps = 30;
    bool cpuConvert = false;   // BGRA readback + libswscale instead of GPU NV12 planes
    unsigned queueCapacity = 8;
    unsigned readbackSlots = 3;
    encode::VideoEncoderConfig encoder;
    bool recordAudio = true;
    audio::AudioEngineConfig audio;
    gpu::CompositionSettings composition;
    webcam::FrameExchange* webcamFrames = nullptr; // null = no webcam in the recording
    std::filesystem::path outputFile;
    std::string container = "matroska";
    unsigned expectedFrames = 0; // sizes the latency sample buffers (0 = no latency samples)

    // Live preview (optional): the capture thread renders a small BGRA copy of the composed
    // frame at most every previewIntervalMs and publishes it (GPU NV12 path only).
    std::shared_ptr<PreviewExchange> preview;
    unsigned previewIntervalMs = 100;
    // Preview only: capture + composition for the preview; no encoder, audio or file.
    bool previewOnly = false;
};

// One captured frame travelling from the capture thread to the encoder.
struct FrameTicket {
    int slot = -1;           // FramePool index
    int64_t pts = 0;         // output timestamp (1/fps units, pauses removed)
    int64_t tickQpc = 0;     // ideal sampling time of this tick
    int64_t enqueueQpc = 0;  // when it entered the queue
};

// Live counters, written by the pipeline threads and read by anyone.
struct SessionCounters {
    std::atomic<int64_t> ticks{0};             // capture ticks processed
    std::atomic<int64_t> enqueued{0};          // frames handed to the encoder queue
    std::atomic<int64_t> uniqueFrames{0};      // enqueued frames with new content (GPU readback)
    std::atomic<int64_t> repeatedFrames{0};    // enqueued repeats of an unchanged screen
    std::atomic<int64_t> droppedQueueFull{0};  // bounded queue full -> newest frame dropped
    std::atomic<int64_t> droppedPoolEmpty{0};  // no free frame buffer
    std::atomic<int64_t> droppedLateTicks{0};  // capture thread missed whole tick periods
    std::atomic<int64_t> droppedNoImage{0};    // no source image available (start-up / access lost)
    std::atomic<int64_t> droppedDeviceLost{0}; // in-flight frames lost with the GPU device
    std::atomic<int64_t> pausedTicks{0};       // ticks skipped while paused (not drops)
    std::atomic<int64_t> gpuStalls{0};         // readback had to block on the GPU
    std::atomic<int64_t> gpuStallMicros{0};
    std::atomic<int64_t> accessLostEvents{0};
    std::atomic<int64_t> deviceLostEvents{0};
    std::atomic<int64_t> desktopUpdates{0};    // ticks that carried a new source image
    std::atomic<int64_t> desktopCopies{0};     // GPU copies of acquired source images (can exceed ticks)
    std::atomic<int64_t> queueDepthSum{0};     // sampled once per tick
    std::atomic<int64_t> queueDepthSamples{0};
    std::atomic<int64_t> queueDepthMax{0};
    std::atomic<int64_t> cpuConvertMicros{0};  // time spent in libswscale (cpu-convert path)
    std::atomic<int64_t> readbackCopyMicros{0};// time copying mapped staging memory into frames

    std::atomic<int64_t> encoded{0};           // video packets produced (== frames encoded)
    std::atomic<int64_t> encodedBytes{0};      // video bytes
    std::atomic<int64_t> keyframes{0};
    std::atomic<int64_t> encoderBusyMicros{0}; // time inside send_frame/receive_packet
    std::atomic<int64_t> muxWriteMicros{0};
    std::atomic<int64_t> muxWriteMaxMicros{0};

    // Most recent per-frame latencies (live diagnostics; the full samples are in LatencySamples).
    std::atomic<int64_t> lastCaptureLatencyUs{0}; // tick -> frame in the encoder queue
    std::atomic<int64_t> lastEncodeLatencyUs{0};  // queue entry -> packet out of the encoder

    int64_t dropped() const
    {
        return droppedQueueFull + droppedPoolEmpty + droppedLateTicks + droppedNoImage + droppedDeviceLost;
    }
};

// Per-frame latency samples, filled by one thread each, read after stop().
struct LatencySamples {
    std::vector<float> captureMs; // tick -> frame in the encoder queue
    std::vector<float> encodeMs;  // queue entry -> packet out of the encoder
};

} // namespace luma::session
