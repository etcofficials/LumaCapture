#pragma once

#include "audio/AudioRing.h"
#include "audio/MicProcessor.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace luma::audio {

struct AudioSourceConfig {
    bool loopback = false;  // true: system audio from a playback device
    std::wstring deviceId;  // empty = Windows default device (follows default changes)
    int delayMs = 0;        // positive = the source is placed later on the timeline
};

// One WASAPI endpoint on its own thread (shared mode, 10 ms polling).
// Audio is converted to 48 kHz stereo float and written into an AudioRing at
// the position its QPC timestamp maps to (position 0 = startQpc). Small clock
// drift is corrected smoothly (swresample compensation); large jumps resync.
// With ring == nullptr the source only measures levels (meters).
class WasapiSource {
public:
    WasapiSource(AudioSourceConfig config, AudioRing* ring, int64_t startQpc, MicProcessor* processor = nullptr);
    ~WasapiSource();
    WasapiSource(const WasapiSource&) = delete;
    WasapiSource& operator=(const WasapiSource&) = delete;

    void start();
    void stop();

    // Peak level since the last call (linear 0..1+), for meters.
    float takePeak() { return m_peak.exchange(0.f); }
    bool running() const { return m_running.load(); }
    std::string status() const;
    std::wstring deviceName() const;
    int64_t resyncs() const { return m_resyncs.load(); }

private:
    void run();
    void setStatus(const std::string& s);

    AudioSourceConfig m_config;
    AudioRing* m_ring;
    int64_t m_startQpc;
    MicProcessor* m_processor;

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_reopen{false};
    std::atomic<float> m_peak{0.f};
    std::atomic<int64_t> m_resyncs{0};
    mutable std::mutex m_mutex;
    std::string m_status;
    std::wstring m_deviceName;

    friend class DefaultDeviceWatcher;
};

} // namespace luma::audio
