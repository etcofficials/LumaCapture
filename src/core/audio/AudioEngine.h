#pragma once

#include "audio/AudioRing.h"
#include "audio/MicProcessor.h"
#include "audio/WasapiSource.h"
#include "encode/AudioEncoder.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace luma::mux { class Muxer; }
namespace luma::session { class PauseTimeline; }

namespace luma::audio {

struct AudioEngineConfig {
    bool system = true;
    bool mic = false;
    std::wstring systemDevice; // empty = default playback device
    std::wstring micDevice;    // empty = default recording device
    float systemVolume = 1.f;  // 0 .. 2
    float micVolume = 1.f;     // 0 .. 2
    bool micMuted = false;
    MicFilterSettings micFilters;
    int micDelayMs = 0;        // -500 .. 500: shifts the microphone on the timeline
    int bitrateKbps = 160;
    bool separateTracks = false; // with both sources: track 1 mix, 2 system, 3 microphone
};

// Recording audio pipeline: WASAPI sources -> position-addressed rings ->
// mixer thread (fixed 150 ms behind real time) -> AAC encoders -> muxer.
class AudioEngine {
public:
    enum class TrackKind { Mix, System, Mic };
    struct Track {
        TrackKind kind;
        std::unique_ptr<encode::AudioEncoder> encoder;
        int stream = -1;
    };

    explicit AudioEngine(const AudioEngineConfig& config);
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool hasTracks() const { return !m_tracks.empty(); }
    std::vector<Track>& tracks() { return m_tracks; }

    void start(int64_t startQpc, const session::PauseTimeline* pause, mux::Muxer* muxer);
    // Mixes everything up to stopQpc, flushes the encoders, stops the devices.
    void stop(int64_t stopQpc);

    void setMicMuted(bool muted) { m_micMuted = muted; }
    bool micMuted() const { return m_micMuted.load(); }
    void setVolumes(float system, float mic)
    {
        m_systemVolume = system;
        m_micVolume = mic;
    }
    void setMicFilters(const MicFilterSettings& s) { m_micProcessor.setSettings(s); }

    float takeSystemPeak() { return m_systemPeak.exchange(0.f); }
    float takeMicPeak() { return m_micPeak.exchange(0.f); }
    std::string status() const;
    bool failed() const { return m_failed.load(); }
    std::string error() const;
    uint64_t encodedBytes() const { return m_bytes.load(); }

private:
    void run();
    void mixBlock(int64_t pos, size_t frames);
    void flushEncoders();

    AudioEngineConfig m_config;
    std::vector<Track> m_tracks;
    MicProcessor m_micProcessor;
    std::unique_ptr<AudioRing> m_systemRing, m_micRing;
    std::unique_ptr<WasapiSource> m_systemSource, m_micSource;

    int64_t m_startQpc = 0;
    const session::PauseTimeline* m_pause = nullptr;
    mux::Muxer* m_muxer = nullptr;
    std::thread m_thread;
    std::atomic<int64_t> m_stopQpc{-1};
    std::atomic<bool> m_micMuted{false};
    std::atomic<float> m_systemVolume{1.f}, m_micVolume{1.f};
    std::atomic<float> m_systemPeak{0.f}, m_micPeak{0.f};
    std::atomic<bool> m_failed{false};
    std::atomic<uint64_t> m_bytes{0};
    mutable std::mutex m_mutex;
    std::string m_error;

    // Mixer state (mixer thread only).
    std::vector<float> m_sys, m_mic, m_mix;
    float m_curSysGain = 1.f, m_curMicGain = 1.f;
};

} // namespace luma::audio
