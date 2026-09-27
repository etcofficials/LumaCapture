#pragma once

#include "webcam/FrameExchange.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace luma::webcam {

// Software image improvement for inexpensive webcams. Runs on the webcam
// thread at camera resolution, once per frame; the result feeds both the
// preview and the recording (no double processing). Cheap per-channel
// adjustments are folded into lookup tables; sharpen, denoise and chroma key
// are separate passes that cost CPU only when enabled.
struct WebcamFilterSettings {
    bool enabled = true;
    float brightness = 0;   // -0.5 .. 0.5
    float contrast = 1;     // 0.5 .. 1.5
    float saturation = 1;   // 0 .. 2
    float gamma = 1;        // 0.5 .. 2.5 (>1 lifts shadows)
    float exposure = 0;     // EV, -2 .. 2
    float temperature = 0;  // -1 cool .. 1 warm
    float tint = 0;         // -1 green .. 1 magenta
    float redGain = 1, greenGain = 1, blueGain = 1; // colour correction 0.5 .. 1.5
    float sharpen = 0;      // 0 .. 1
    float denoise = 0;      // 0 .. 1 (motion-adaptive temporal smoothing)
    bool monochrome = false;

    bool chromaKey = false;
    uint32_t keyColor = 0x00FF00; // 0xRRGGBB
    float similarity = 0.30f;     // 0 .. 1
    float smoothness = 0.08f;     // 0 .. 0.5
    float spill = 0.5f;           // 0 .. 1
    // Optional replacement background (any size; scaled to the camera frame).
    std::shared_ptr<const ImageBuffer> background;
};

// Image statistics of a raw camera frame (sampled on a sparse grid, cheap).
struct FrameStats {
    unsigned samples = 0;
    double meanLuma = 0;              // 0..1
    double p02 = 0, p50 = 0, p98 = 0; // luma percentiles, 0..1
    double meanR = 0, meanG = 0, meanB = 0; // mid-tone channel means, 0..1 (for white balance)
    double clippedHigh = 0;           // fraction of samples >= 250/255
    double clippedLow = 0;            // fraction of samples <= 5/255
};

FrameStats analyzeFrame(const uint8_t* bgrx, long pitch, unsigned width, unsigned height);

// One-shot "Auto adjust": derives exposure/gamma/contrast/white balance (and a
// little denoise/sharpen in low light) from the statistics. Chroma key and the
// background are preserved. Deterministic - applying it once never oscillates.
WebcamFilterSettings suggestFilters(const FrameStats& stats, const WebcamFilterSettings& current);

class WebcamProcessor {
public:
    void setSettings(const WebcamFilterSettings& s);

    // src: BGRX rows (pitch in bytes, may be negative for bottom-up images).
    // dst: width*height BGRA pixels.
    void process(const uint8_t* src, long pitch, unsigned width, unsigned height, uint32_t* dst);

private:
    void buildLuts();
    void scaleBackground(unsigned width, unsigned height);

    WebcamFilterSettings m_s;
    bool m_lutDirty = true;
    bool m_lutIdentity = true;
    std::array<uint8_t, 256> m_lutR{}, m_lutG{}, m_lutB{};
    std::vector<uint32_t> m_prev; // previous output (temporal denoise)
    std::vector<uint32_t> m_tmp;  // sharpen source
    std::vector<uint32_t> m_bg;   // background scaled to the camera size
    const ImageBuffer* m_bgSource = nullptr;
    unsigned m_bgW = 0, m_bgH = 0;
};

} // namespace luma::webcam
