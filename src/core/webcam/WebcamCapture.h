#pragma once

#include "webcam/FrameExchange.h"
#include "webcam/WebcamFilters.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace luma::webcam {

struct WebcamDeviceInfo {
    std::wstring name;
    std::wstring symbolicLink; // stable id
};

struct WebcamMode {
    unsigned width = 0;
    unsigned height = 0;
    unsigned fpsNum = 30, fpsDen = 1;
    std::string format; // "NV12", "YUY2", "MJPG", ...
    double fps() const { return fpsDen ? static_cast<double>(fpsNum) / fpsDen : 0; }
};

// A hardware camera setting exposed by the driver. Only controls the camera
// actually reports are listed.
struct CameraControl {
    enum class Kind {
        ProcAmp,           // IAMVideoProcAmp property (brightness, white balance, gain, ...)
        Camera,            // IAMCameraControl property (exposure, focus, ...)
        PowerLine,         // anti-flicker: 0 off, 1 = 50 Hz, 2 = 60 Hz (UVC power-line frequency)
        ExposurePriority,  // 1 = camera may lower the frame rate to expose longer (dim light)
    };
    Kind kind = Kind::ProcAmp;
    long property = 0;
    std::string name;
    long min = 0, max = 0, step = 1, defaultValue = 0, value = 0;
    bool autoSupported = false;
    bool autoEnabled = false;
};

struct CameraStats {
    double fps = 0;          // delivered frames per second (last second)
    double maxGapMs = 0;     // longest gap between frames in the last second
    double processMs = 0;    // average CPU time of the filter chain per frame
    unsigned width = 0, height = 0;
    uint64_t frames = 0;
};

// Media Foundation camera enumeration (fast, no device is opened).
std::vector<WebcamDeviceInfo> enumerateWebcams();
// Opens the camera briefly and lists the modes it really supports (deduplicated,
// preferring uncompressed formats). Throws on failure.
std::vector<WebcamMode> enumerateWebcamModes(const std::wstring& symbolicLink);

// Runs one camera on a dedicated thread with an ASYNCHRONOUS source reader:
// the thread waits for frames with a timeout, so stopping is always prompt and
// the device is shut down by its own thread (a synchronous ReadSample never
// returns once the source is shut down from another thread - that caused the
// UI hangs in 1.0.0). Frames: MF -> BGRX -> WebcamProcessor -> FrameExchange.
class WebcamCapture {
public:
    enum class State { Stopped, Starting, Running, Error };

    WebcamCapture();
    ~WebcamCapture();
    WebcamCapture(const WebcamCapture&) = delete;
    WebcamCapture& operator=(const WebcamCapture&) = delete;

    // Asynchronous: returns immediately; watch state().
    void start(const std::wstring& symbolicLink, const WebcamMode& mode);
    // Non-blocking stop request; finished() turns true once the thread is done.
    void requestStop();
    bool finished() const { return m_finished.load(); }
    // Blocking stop (bounded: the thread polls the stop flag every 100 ms).
    void stop();

    void setFilters(const WebcamFilterSettings& filters);
    void setControl(const CameraControl& control); // applied on the camera thread
    // Analyses the next raw frame; poll takeAnalysis() for the result.
    void requestAnalysis();
    bool takeAnalysis(FrameStats& out);
    // Also publish a small unprocessed copy (before/after comparison).
    void setCompareEnabled(bool on) { m_compare = on; }

    State state() const { return m_state.load(); }
    std::string message() const;
    std::vector<CameraControl> controls() const;
    CameraStats stats() const;
    double measuredFps() const { return stats().fps; }
    FrameExchange& frames() { return m_frames; }
    FrameExchange& rawPreview() { return m_raw; }

private:
    void run(std::wstring link, WebcamMode mode);
    void setError(const std::string& msg);

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_finished{true};
    std::atomic<State> m_state{State::Stopped};
    std::atomic<bool> m_compare{false};
    std::atomic<bool> m_analysisRequested{false};
    FrameExchange m_frames;
    FrameExchange m_raw;

    mutable std::mutex m_mutex; // guards the fields below (never held across MF calls)
    std::string m_message;
    std::vector<CameraControl> m_controls;
    std::vector<CameraControl> m_pendingControls;
    WebcamFilterSettings m_filters;
    bool m_filtersChanged = true;
    bool m_analysisReady = false;
    FrameStats m_analysis;
    CameraStats m_stats;
};

} // namespace luma::webcam
