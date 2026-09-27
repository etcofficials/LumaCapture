#include "webcam/WebcamFilters.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace luma::webcam {
namespace {

inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

inline float smoothstep(float e0, float e1, float x)
{
    const float t = std::clamp((x - e0) / std::max(1e-4f, e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

inline uint32_t pack(int r, int g, int b, int a)
{
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

inline int chR(uint32_t p) { return (p >> 16) & 0xFF; }
inline int chG(uint32_t p) { return (p >> 8) & 0xFF; }
inline int chB(uint32_t p) { return p & 0xFF; }
inline int chA(uint32_t p) { return p >> 24; }

bool sameSettingsForLut(const WebcamFilterSettings& a, const WebcamFilterSettings& b)
{
    return a.enabled == b.enabled && a.brightness == b.brightness && a.contrast == b.contrast && a.gamma == b.gamma &&
           a.exposure == b.exposure && a.temperature == b.temperature && a.tint == b.tint && a.redGain == b.redGain &&
           a.greenGain == b.greenGain && a.blueGain == b.blueGain;
}

} // namespace

FrameStats analyzeFrame(const uint8_t* src, long pitch, unsigned w, unsigned h)
{
    FrameStats st;
    if (!src || w < 8 || h < 8)
        return st;
    // ~5000 samples on a regular grid is plenty for exposure/white balance decisions.
    const unsigned step = std::max(1u, static_cast<unsigned>(std::sqrt(static_cast<double>(w) * h / 5000.0)));
    uint32_t hist[256] = {};
    double sumR = 0, sumG = 0, sumB = 0;
    unsigned mid = 0;
    for (unsigned y = step / 2; y < h; y += step) {
        const auto* row = reinterpret_cast<const uint32_t*>(src + static_cast<long>(y) * pitch);
        for (unsigned x = step / 2; x < w; x += step) {
            const uint32_t p = row[x];
            const int r = chR(p), g = chG(p), b = chB(p);
            const int l = (77 * r + 150 * g + 29 * b) >> 8;
            ++hist[l];
            ++st.samples;
            if (l > 40 && l < 220) { // mid-tones only: highlights/shadows mislead white balance
                sumR += r;
                sumG += g;
                sumB += b;
                ++mid;
            }
        }
    }
    if (st.samples == 0)
        return st;
    auto percentile = [&](double q) {
        const auto target = static_cast<uint64_t>(q * st.samples);
        uint64_t acc = 0;
        for (int i = 0; i < 256; ++i) {
            acc += hist[i];
            if (acc > target)
                return i / 255.0;
        }
        return 1.0;
    };
    double sum = 0;
    for (int i = 0; i < 256; ++i)
        sum += static_cast<double>(hist[i]) * i;
    st.meanLuma = sum / st.samples / 255.0;
    st.p02 = percentile(0.02);
    st.p50 = percentile(0.50);
    st.p98 = percentile(0.98);
    uint64_t hi = 0, lo = 0;
    for (int i = 250; i < 256; ++i)
        hi += hist[i];
    for (int i = 0; i <= 5; ++i)
        lo += hist[i];
    st.clippedHigh = static_cast<double>(hi) / st.samples;
    st.clippedLow = static_cast<double>(lo) / st.samples;
    if (mid > 0) {
        st.meanR = sumR / mid / 255.0;
        st.meanG = sumG / mid / 255.0;
        st.meanB = sumB / mid / 255.0;
    }
    return st;
}

WebcamFilterSettings suggestFilters(const FrameStats& st, const WebcamFilterSettings& current)
{
    WebcamFilterSettings f = current;
    // Reset the image adjustments; keep enable flag, chroma key and background.
    f.brightness = 0;
    f.contrast = 1;
    f.saturation = 1.05f;
    f.gamma = 1;
    f.exposure = 0;
    f.temperature = 0;
    f.tint = 0;
    f.redGain = f.greenGain = f.blueGain = 1;
    f.sharpen = 0.1f;
    f.denoise = 0;
    f.monochrome = false;
    f.enabled = true;
    if (st.samples == 0)
        return f;

    // Exposure: bring the median towards ~0.45 mostly with gamma (keeps blacks and
    // whites anchored); only add linear exposure when highlights have headroom.
    const double target = 0.45;
    const double median = std::clamp(st.p50, 0.02, 0.98);
    if (median < target - 0.05) {
        const double g = std::log(median) / std::log(target); // >1 lifts mid-tones
        f.gamma = static_cast<float>(std::clamp(g, 1.0, 1.8));
        if (st.p98 < 0.8 && st.clippedHigh < 0.01)
            f.exposure = static_cast<float>(std::clamp(std::log2(0.8 / std::max(0.05, st.p98)), 0.0, 1.0));
    } else if (median > target + 0.12) {
        const double g = std::log(median) / std::log(target);
        f.gamma = static_cast<float>(std::clamp(g, 0.75, 1.0));
    }

    // Contrast: gentle stretch when the histogram is flat/hazy, never crush shadows.
    const double spread = st.p98 - st.p02;
    if (spread > 0.05 && spread < 0.8 && st.clippedLow < 0.02)
        f.contrast = static_cast<float>(std::clamp(0.85 / spread, 1.0, 1.25));

    // White balance: grey-world on mid-tones, as RGB gains normalised to green, limited.
    if (st.meanR > 0.02 && st.meanG > 0.02 && st.meanB > 0.02) {
        f.redGain = static_cast<float>(std::clamp(st.meanG / st.meanR, 0.8, 1.25));
        f.blueGain = static_cast<float>(std::clamp(st.meanG / st.meanB, 0.8, 1.25));
    }

    // Low light: sensor noise dominates - mild temporal denoise, slightly more sharpening.
    if (st.meanLuma < 0.25) {
        f.denoise = 0.4f;
        f.sharpen = 0.15f;
    }
    return f;
}

void WebcamProcessor::setSettings(const WebcamFilterSettings& s)
{
    if (!sameSettingsForLut(s, m_s))
        m_lutDirty = true;
    if (s.background != m_s.background)
        m_bgSource = nullptr; // rescale on next frame
    m_s = s;
}

void WebcamProcessor::buildLuts()
{
    m_lutDirty = false;
    const WebcamFilterSettings& s = m_s;
    const float ev = std::exp2(std::clamp(s.exposure, -3.f, 3.f));
    const float gains[3] = {
        ev * s.redGain * (1.f + 0.20f * s.temperature),
        ev * s.greenGain * (1.f - 0.12f * s.tint),
        ev * s.blueGain * (1.f - 0.20f * s.temperature),
    };
    const float invGamma = 1.f / std::clamp(s.gamma, 0.2f, 5.f);
    std::array<uint8_t, 256>* luts[3] = {&m_lutR, &m_lutG, &m_lutB};
    m_lutIdentity = true;
    for (int c = 0; c < 3; ++c) {
        for (int x = 0; x < 256; ++x) {
            float v = x / 255.f * gains[c];
            v = (v - 0.5f) * s.contrast + 0.5f + s.brightness;
            v = std::pow(std::clamp(v, 0.f, 1.f), invGamma);
            const auto out = static_cast<uint8_t>(std::lround(v * 255.f));
            (*luts[c])[x] = out;
            if (out != x)
                m_lutIdentity = false;
        }
    }
}

void WebcamProcessor::scaleBackground(unsigned w, unsigned h)
{
    const ImageBuffer* bg = m_s.background.get();
    m_bgSource = bg;
    m_bgW = w;
    m_bgH = h;
    m_bg.assign(static_cast<size_t>(w) * h, 0xFF000000u);
    if (!bg || bg->width == 0 || bg->height == 0)
        return;
    // "Cover" scaling (fills the frame, crops the excess), nearest neighbour; runs once per change.
    const float scale = std::max(static_cast<float>(w) / bg->width, static_cast<float>(h) / bg->height);
    const float ox = (bg->width * scale - w) * 0.5f, oy = (bg->height * scale - h) * 0.5f;
    for (unsigned y = 0; y < h; ++y) {
        const auto sy = std::min(bg->height - 1, static_cast<unsigned>((y + oy) / scale));
        for (unsigned x = 0; x < w; ++x) {
            const auto sx = std::min(bg->width - 1, static_cast<unsigned>((x + ox) / scale));
            m_bg[static_cast<size_t>(y) * w + x] = bg->pixels[static_cast<size_t>(sy) * bg->width + sx] | 0xFF000000u;
        }
    }
}

void WebcamProcessor::process(const uint8_t* src, long pitch, unsigned w, unsigned h, uint32_t* dst)
{
    const WebcamFilterSettings& s = m_s;
    const size_t n = static_cast<size_t>(w) * h;

    if (!s.enabled) {
        for (unsigned y = 0; y < h; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(src + static_cast<long>(y) * pitch);
            uint32_t* out = dst + static_cast<size_t>(y) * w;
            for (unsigned x = 0; x < w; ++x)
                out[x] = row[x] | 0xFF000000u;
        }
        m_prev.clear();
        return;
    }
    if (m_lutDirty)
        buildLuts();

    // Chroma key parameters (in the Cb/Cr plane, 0..1 distance).
    const int kr = (s.keyColor >> 16) & 0xFF, kg = (s.keyColor >> 8) & 0xFF, kb = s.keyColor & 0xFF;
    const float kcb = (-0.1146f * kr - 0.3854f * kg + 0.5f * kb) / 255.f;
    const float kcr = (0.5f * kr - 0.4542f * kg - 0.0458f * kb) / 255.f;
    const int keyChannel = (kg >= kr && kg >= kb) ? 1 : (kb >= kr ? 2 : 0);
    const float sim = std::clamp(s.similarity, 0.f, 1.f) * 0.5f;
    const float smooth = std::clamp(s.smoothness, 0.001f, 0.5f) * 0.5f;
    const float spill = std::clamp(s.spill, 0.f, 1.f);

    const bool mono = s.monochrome;
    const int sat256 = mono ? 0 : static_cast<int>(std::lround(std::clamp(s.saturation, 0.f, 3.f) * 256));
    const bool doSat = sat256 != 256;

    // Pass 1 fast paths (the common cases): no key, no saturation change.
    if (!s.chromaKey && !doSat) {
        for (unsigned y = 0; y < h; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(src + static_cast<long>(y) * pitch);
            uint32_t* out = dst + static_cast<size_t>(y) * w;
            if (m_lutIdentity) {
                for (unsigned x = 0; x < w; ++x) // vectorises: plain copy with opaque alpha
                    out[x] = row[x] | 0xFF000000u;
            } else {
                const uint8_t* lr = m_lutR.data();
                const uint8_t* lg = m_lutG.data();
                const uint8_t* lb = m_lutB.data();
                for (unsigned x = 0; x < w; ++x) {
                    const uint32_t p = row[x];
                    out[x] = 0xFF000000u | (static_cast<uint32_t>(lr[(p >> 16) & 0xFF]) << 16) |
                             (static_cast<uint32_t>(lg[(p >> 8) & 0xFF]) << 8) | lb[p & 0xFF];
                }
            }
        }
    } else
    // Pass 1 general: key (on the original colours), LUTs, saturation.
    for (unsigned y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(src + static_cast<long>(y) * pitch);
        uint32_t* out = dst + static_cast<size_t>(y) * w;
        for (unsigned x = 0; x < w; ++x) {
            const uint32_t p = row[x];
            int r = chR(p), g = chG(p), b = chB(p);
            int a = 255;
            if (s.chromaKey) {
                const float cb = (-0.1146f * r - 0.3854f * g + 0.5f * b) / 255.f;
                const float cr = (0.5f * r - 0.4542f * g - 0.0458f * b) / 255.f;
                const float d = std::sqrt((cb - kcb) * (cb - kcb) + (cr - kcr) * (cr - kcr));
                const float alpha = smoothstep(sim, sim + smooth, d);
                a = static_cast<int>(alpha * 255.f + 0.5f);
                if (spill > 0 && alpha < 1.f) {
                    // Pull the key-coloured channel down towards the other two (removes green fringes).
                    int* ch[3] = {&r, &g, &b};
                    const int o1 = *ch[(keyChannel + 1) % 3], o2 = *ch[(keyChannel + 2) % 3];
                    const int limit = std::max(o1, o2);
                    int& k = *ch[keyChannel];
                    if (k > limit)
                        k = static_cast<int>(k - (k - limit) * spill);
                }
            }
            if (!m_lutIdentity) {
                r = m_lutR[r];
                g = m_lutG[g];
                b = m_lutB[b];
            }
            if (doSat) {
                const int l = (77 * r + 150 * g + 29 * b) >> 8;
                r = clamp255(l + (((r - l) * sat256) >> 8));
                g = clamp255(l + (((g - l) * sat256) >> 8));
                b = clamp255(l + (((b - l) * sat256) >> 8));
            }
            out[x] = pack(r, g, b, a);
        }
    }

    // Pass 2: motion-adaptive temporal denoise (good for static webcam scenes in low light).
    if (s.denoise > 0.f) {
        if (m_prev.size() != n) {
            m_prev.assign(dst, dst + n);
        } else {
            const int k = 256 - static_cast<int>(std::clamp(s.denoise, 0.f, 1.f) * 200); // weight of the new frame
            const int motion = 28;
            for (size_t i = 0; i < n; ++i) {
                const uint32_t c = dst[i], q = m_prev[i];
                const int dr = chR(c) - chR(q), dg = chG(c) - chG(q), db = chB(c) - chB(q);
                if (std::abs(dr) < motion && std::abs(dg) < motion && std::abs(db) < motion) {
                    dst[i] = pack(chR(q) + ((dr * k) >> 8), chG(q) + ((dg * k) >> 8), chB(q) + ((db * k) >> 8), chA(c));
                }
                m_prev[i] = dst[i];
            }
        }
    } else if (!m_prev.empty()) {
        m_prev.clear();
    }

    // Pass 3: mild unsharp mask (4-neighbour).
    if (s.sharpen > 0.f && w > 2 && h > 2) {
        m_tmp.assign(dst, dst + n);
        const int amt = static_cast<int>(std::clamp(s.sharpen, 0.f, 1.f) * 256);
        for (unsigned y = 1; y + 1 < h; ++y) {
            const uint32_t* c = m_tmp.data() + static_cast<size_t>(y) * w;
            const uint32_t* up = c - w;
            const uint32_t* dn = c + w;
            uint32_t* out = dst + static_cast<size_t>(y) * w;
            for (unsigned x = 1; x + 1 < w; ++x) {
                const uint32_t p = c[x];
                const int br = (chR(up[x]) + chR(dn[x]) + chR(c[x - 1]) + chR(c[x + 1])) >> 2;
                const int bg = (chG(up[x]) + chG(dn[x]) + chG(c[x - 1]) + chG(c[x + 1])) >> 2;
                const int bb = (chB(up[x]) + chB(dn[x]) + chB(c[x - 1]) + chB(c[x + 1])) >> 2;
                out[x] = pack(clamp255(chR(p) + (((chR(p) - br) * amt) >> 8)),
                              clamp255(chG(p) + (((chG(p) - bg) * amt) >> 8)),
                              clamp255(chB(p) + (((chB(p) - bb) * amt) >> 8)), chA(p));
            }
        }
    }

    // Pass 4: replacement background behind the keyed subject.
    if (s.chromaKey && s.background) {
        if (m_bgSource != s.background.get() || m_bgW != w || m_bgH != h)
            scaleBackground(w, h);
        for (size_t i = 0; i < n; ++i) {
            const uint32_t f = dst[i];
            const int a = chA(f);
            if (a == 255)
                continue;
            const uint32_t b = m_bg[i];
            dst[i] = pack((chR(f) * a + chR(b) * (255 - a)) / 255, (chG(f) * a + chG(b) * (255 - a)) / 255,
                          (chB(f) * a + chB(b) * (255 - a)) / 255, 255);
        }
    }
}

} // namespace luma::webcam
