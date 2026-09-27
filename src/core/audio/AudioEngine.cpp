#include "audio/AudioEngine.h"

#include "mux/Muxer.h"
#include "session/PauseTimeline.h"
#include "util/Log.h"
#include "util/QpcClock.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace luma::audio {
namespace {

constexpr int kRate = 48000;
constexpr size_t kBlock = 480;                 // 10 ms
constexpr int64_t kLatencyFrames = kRate * 15 / 100; // mix 150 ms behind real time
constexpr size_t kRingFrames = kRate * 2;      // 2 s per source

// Soft saturation above 0.9 so a hot mix never wraps or clips hard.
inline float softClip(float x)
{
    const float a = std::abs(x);
    if (a <= 0.9f)
        return x;
    const float y = 0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f);
    return x < 0 ? -y : y;
}

void raisePeak(std::atomic<float>& target, float v)
{
    float cur = target.load();
    while (v > cur && !target.compare_exchange_weak(cur, v)) {
    }
}

} // namespace

AudioEngine::AudioEngine(const AudioEngineConfig& config) : m_config(config)
{
    m_micMuted = config.micMuted;
    m_systemVolume = config.systemVolume;
    m_micVolume = config.micVolume;
    m_micProcessor.setSettings(config.micFilters);

    const int kbps = std::clamp(config.bitrateKbps, 64, 320);
    if (config.system && config.mic) {
        m_tracks.push_back({TrackKind::Mix, std::make_unique<encode::AudioEncoder>(kbps, "Mixed audio"), -1});
        if (config.separateTracks) {
            m_tracks.push_back({TrackKind::System, std::make_unique<encode::AudioEncoder>(kbps, "System audio"), -1});
            m_tracks.push_back({TrackKind::Mic, std::make_unique<encode::AudioEncoder>(kbps, "Microphone"), -1});
        }
    } else if (config.system) {
        m_tracks.push_back({TrackKind::Mix, std::make_unique<encode::AudioEncoder>(kbps, "System audio"), -1});
    } else if (config.mic) {
        m_tracks.push_back({TrackKind::Mix, std::make_unique<encode::AudioEncoder>(kbps, "Microphone"), -1});
    }
}

AudioEngine::~AudioEngine()
{
    if (m_thread.joinable()) {
        m_stopQpc = qpc::now();
        m_thread.join();
    }
}

void AudioEngine::start(int64_t startQpc, const session::PauseTimeline* pause, mux::Muxer* muxer)
{
    m_startQpc = startQpc;
    m_pause = pause;
    m_muxer = muxer;
    if (m_config.system) {
        m_systemRing = std::make_unique<AudioRing>(kRingFrames);
        m_systemSource = std::make_unique<WasapiSource>(AudioSourceConfig{true, m_config.systemDevice, 0},
                                                        m_systemRing.get(), startQpc);
        m_systemSource->start();
    }
    if (m_config.mic) {
        m_micRing = std::make_unique<AudioRing>(kRingFrames);
        m_micSource = std::make_unique<WasapiSource>(AudioSourceConfig{false, m_config.micDevice, m_config.micDelayMs},
                                                     m_micRing.get(), startQpc, &m_micProcessor);
        m_micSource->start();
    }
    m_sys.assign(kBlock * 2, 0.f);
    m_mic.assign(kBlock * 2, 0.f);
    m_mix.assign(kBlock * 2, 0.f);
    m_curSysGain = m_systemVolume;
    m_curMicGain = m_micMuted ? 0.f : m_micVolume.load();
    m_thread = std::thread(&AudioEngine::run, this);
}

void AudioEngine::stop(int64_t stopQpc)
{
    m_stopQpc = stopQpc;
    if (m_thread.joinable())
        m_thread.join();
    if (m_systemSource)
        m_systemSource->stop();
    if (m_micSource)
        m_micSource->stop();
}

std::string AudioEngine::status() const
{
    std::string s;
    if (m_systemSource)
        s = m_systemSource->status();
    if (m_micSource) {
        const std::string m = m_micSource->status();
        if (!m.empty())
            s += (s.empty() ? "" : " | ") + m;
    }
    return s;
}

std::string AudioEngine::error() const
{
    std::lock_guard lock(m_mutex);
    return m_error;
}

void AudioEngine::run()
{
    SetThreadDescription(GetCurrentThread(), L"luma-audio-mix");
    const int64_t freq = qpc::frequency();
    int64_t mixPos = 0;
    try {
        for (;;) {
            const int64_t stopQpc = m_stopQpc.load();
            const int64_t nowPos = (qpc::now() - m_startQpc) * kRate / freq;
            int64_t target = nowPos - kLatencyFrames;
            if (stopQpc >= 0) {
                const int64_t stopPos = (stopQpc - m_startQpc) * kRate / freq;
                target = std::min(target, stopPos);
                if (mixPos >= stopPos)
                    break;
                // Once the stop position is behind the latency window, finish immediately.
                if (nowPos - kLatencyFrames >= stopPos)
                    target = stopPos;
            }
            while (mixPos + static_cast<int64_t>(kBlock) <= target) {
                mixBlock(mixPos, kBlock);
                mixPos += kBlock;
            }
            if (stopQpc >= 0) {
                const int64_t stopPos = (stopQpc - m_startQpc) * kRate / freq;
                if (nowPos - kLatencyFrames >= stopPos) {
                    if (stopPos > mixPos)
                        mixBlock(mixPos, static_cast<size_t>(stopPos - mixPos));
                    break;
                }
            }
            Sleep(10);
        }
        flushEncoders();
    } catch (const std::exception& e) {
        {
            std::lock_guard lock(m_mutex);
            m_error = e.what();
        }
        m_failed = true;
        log::error("Audio mixer failed: {}", e.what());
    } catch (...) {
        {
            std::lock_guard lock(m_mutex);
            m_error = "unexpected error in the audio mixer";
        }
        m_failed = true;
        log::error("Audio mixer failed with a non-standard exception");
    }
}

void AudioEngine::mixBlock(int64_t pos, size_t frames)
{
    const size_t n = frames * 2;
    if (m_sys.size() < n) {
        m_sys.resize(n);
        m_mic.resize(n);
        m_mix.resize(n);
    }
    std::fill(m_sys.begin(), m_sys.begin() + static_cast<ptrdiff_t>(n), 0.f);
    std::fill(m_mic.begin(), m_mic.begin() + static_cast<ptrdiff_t>(n), 0.f);
    if (m_systemRing)
        m_systemRing->read(pos, m_sys.data(), frames);
    if (m_micRing)
        m_micRing->read(pos, m_mic.data(), frames);

    // Gains ramp linearly across the block: volume changes and mute never click.
    const float sysTarget = m_systemVolume;
    const float micTarget = m_micMuted ? 0.f : m_micVolume.load();
    float sysPeak = 0.f, micPeak = 0.f;
    for (size_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i + 1) / static_cast<float>(frames);
        const float gs = m_curSysGain + (sysTarget - m_curSysGain) * t;
        const float gm = m_curMicGain + (micTarget - m_curMicGain) * t;
        for (int c = 0; c < 2; ++c) {
            const size_t k = 2 * i + static_cast<size_t>(c);
            const float s = m_sys[k] * gs;
            const float m = m_mic[k] * gm;
            m_sys[k] = softClip(s);
            m_mic[k] = softClip(m);
            m_mix[k] = softClip(s + m);
            sysPeak = std::max(sysPeak, std::abs(s));
            micPeak = std::max(micPeak, std::abs(m));
        }
    }
    m_curSysGain = sysTarget;
    m_curMicGain = micTarget;
    raisePeak(m_systemPeak, sysPeak);
    raisePeak(m_micPeak, micPeak);

    // Paused blocks are cut from the recording (video does the same with the same timeline).
    const int64_t blockQpc = m_startQpc + pos * qpc::frequency() / kRate;
    if (m_pause && m_pause->isPausedAt(blockQpc))
        return;

    for (Track& t : m_tracks) {
        const float* src = t.kind == TrackKind::System ? m_sys.data()
                         : t.kind == TrackKind::Mic    ? m_mic.data()
                                                       : (m_config.system && m_config.mic ? m_mix.data()
                                                          : m_config.system          ? m_sys.data()
                                                                                     : m_mic.data());
        const int stream = t.stream;
        const AVRational tb = t.encoder->context()->time_base;
        t.encoder->push(src, frames, [&](AVPacket* pkt) {
            m_bytes += static_cast<uint64_t>(pkt->size);
            m_muxer->write(stream, pkt, tb);
        });
    }
}

void AudioEngine::flushEncoders()
{
    for (Track& t : m_tracks) {
        const int stream = t.stream;
        const AVRational tb = t.encoder->context()->time_base;
        t.encoder->flush([&](AVPacket* pkt) {
            m_bytes += static_cast<uint64_t>(pkt->size);
            m_muxer->write(stream, pkt, tb);
        });
    }
}

} // namespace luma::audio
