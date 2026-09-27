#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace luma::audio {

// Optional microphone processing. Every stage is individually bypassable;
// "enabled = false" bypasses the whole chain.
struct MicFilterSettings {
    bool enabled = false;
    float gainDb = 0;              // input gain, -12 .. +24

    bool highPass = false;         // 80 Hz rumble filter

    bool eq = false;               // 3 bands: low shelf 150 Hz, peak 2 kHz, high shelf 7 kHz
    float eqLowDb = 0, eqMidDb = 0, eqHighDb = 0;

    bool gate = false;
    float gateThresholdDb = -45;
    float gateReleaseMs = 150;

    bool compressor = false;
    float compThresholdDb = -20;
    float compRatio = 3;
    float compAttackMs = 8;
    float compReleaseMs = 120;
    float compMakeupDb = 3;

    bool limiter = false;
    float limiterCeilingDb = -1;

    bool noiseSuppression = false; // FFmpeg afftdn (FFT denoiser)
    float noiseReductionDb = 12;   // 3 .. 40
};

// Processes 48 kHz stereo interleaved float audio in blocks on the capture
// thread. Settings can be changed from any thread.
class MicProcessor {
public:
    MicProcessor();
    ~MicProcessor();

    void setSettings(const MicFilterSettings& s);

    // Processes `frames` in place into `out` (which may be longer or shorter
    // than the input when noise suppression buffers audio). Returns the
    // number of output frames. `latencyFrames` receives how many frames of
    // input are still inside the chain *before* this block's output (so the
    // output belongs at input position - latencyFrames).
    size_t process(const float* in, size_t frames, std::vector<float>& out, int64_t& latencyFrames);

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[2] = {}, z2[2] = {};
        float run(float x, int ch)
        {
            const double y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return static_cast<float>(y);
        }
    };
    class Denoiser;

    void configure(const MicFilterSettings& s);

    std::mutex m_mutex;
    MicFilterSettings m_pending;
    bool m_dirty = true;

    MicFilterSettings m_s;
    Biquad m_hp, m_low, m_mid, m_high;
    double m_gateEnv = 0, m_gateGain = 1, m_compEnv = 0, m_limGain = 1;
    std::unique_ptr<Denoiser> m_denoiser;
    int64_t m_inTotal = 0, m_outTotal = 0;
};

} // namespace luma::audio
