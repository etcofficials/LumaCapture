#include "audio/MicProcessor.h"

#include "encode/FfmpegUtil.h"
#include "util/Log.h"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>

namespace luma::audio {
namespace {

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
double gainToDb(double g) { return 20.0 * std::log10(std::max(g, 1e-9)); }
double coef(double ms) { return std::exp(-1.0 / (std::max(ms, 0.1) * 0.001 * kRate)); }

// RBJ audio EQ cookbook biquads.
template <typename B>
void setShelf(B& f, bool low, double freq, double gainDb)
{
    const double A = std::pow(10.0, gainDb / 40.0), w0 = 2 * kPi * freq / kRate;
    const double cw = std::cos(w0), sw = std::sin(w0), alpha = sw / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * alpha;
    double b0, b1, b2, a0, a1, a2;
    if (low) {
        b0 = A * ((A + 1) - (A - 1) * cw + sa);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - sa);
        a0 = (A + 1) + (A - 1) * cw + sa;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - sa;
    } else {
        b0 = A * ((A + 1) + (A - 1) * cw + sa);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - sa);
        a0 = (A + 1) - (A - 1) * cw + sa;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - sa;
    }
    f.b0 = b0 / a0; f.b1 = b1 / a0; f.b2 = b2 / a0; f.a1 = a1 / a0; f.a2 = a2 / a0;
}

template <typename B>
void setPeak(B& f, double freq, double q, double gainDb)
{
    const double A = std::pow(10.0, gainDb / 40.0), w0 = 2 * kPi * freq / kRate;
    const double alpha = std::sin(w0) / (2 * q), cw = std::cos(w0);
    const double a0 = 1 + alpha / A;
    f.b0 = (1 + alpha * A) / a0; f.b1 = -2 * cw / a0; f.b2 = (1 - alpha * A) / a0;
    f.a1 = -2 * cw / a0; f.a2 = (1 - alpha / A) / a0;
}

template <typename B>
void setHighPass(B& f, double freq)
{
    const double w0 = 2 * kPi * freq / kRate, cw = std::cos(w0), alpha = std::sin(w0) / (2 * 0.7071);
    const double a0 = 1 + alpha;
    f.b0 = (1 + cw) / 2 / a0; f.b1 = -(1 + cw) / a0; f.b2 = (1 + cw) / 2 / a0;
    f.a1 = -2 * cw / a0; f.a2 = (1 - alpha) / a0;
}

} // namespace

// libavfilter graph: abuffer -> afftdn -> abuffersink (48 kHz stereo float).
class MicProcessor::Denoiser {
public:
    explicit Denoiser(float reductionDb)
    {
        m_graph = avfilter_graph_alloc();
        if (!m_graph)
            throw std::bad_alloc();
        const AVFilter* src = avfilter_get_by_name("abuffer");
        const AVFilter* sink = avfilter_get_by_name("abuffersink");
        const AVFilter* dn = avfilter_get_by_name("afftdn");
        if (!src || !sink || !dn)
            throw std::runtime_error("afftdn filter not available in this FFmpeg build");
        const std::string srcArgs = "sample_rate=48000:sample_fmt=flt:channel_layout=stereo:time_base=1/48000";
        ff::check(avfilter_graph_create_filter(&m_src, src, "in", srcArgs.c_str(), nullptr, m_graph), "abuffer");
        ff::check(avfilter_graph_create_filter(&m_sink, sink, "out", nullptr, nullptr, m_graph), "abuffersink");
        const std::string dnArgs = std::format("nr={:.1f}:nf=-50:tn=1", std::clamp(reductionDb, 0.01f, 97.f));
        AVFilterContext* dnCtx = nullptr;
        ff::check(avfilter_graph_create_filter(&dnCtx, dn, "dn", dnArgs.c_str(), nullptr, m_graph), "afftdn");
        ff::check(avfilter_link(m_src, 0, dnCtx, 0), "link src");
        ff::check(avfilter_link(dnCtx, 0, m_sink, 0), "link sink");
        ff::check(avfilter_graph_config(m_graph, nullptr), "avfilter_graph_config");
        m_in.reset(av_frame_alloc());
        m_out.reset(av_frame_alloc());
    }
    ~Denoiser() { avfilter_graph_free(&m_graph); }

    // Pushes frames, appends everything the filter has ready to `out`.
    void process(const float* in, size_t frames, std::vector<float>& out)
    {
        AVFrame* f = m_in.get();
        av_frame_unref(f);
        f->format = AV_SAMPLE_FMT_FLT;
        f->sample_rate = 48000;
        f->nb_samples = static_cast<int>(frames);
        av_channel_layout_default(&f->ch_layout, 2);
        f->pts = m_pts;
        m_pts += static_cast<int64_t>(frames);
        ff::check(av_frame_get_buffer(f, 0), "av_frame_get_buffer(audio)");
        std::memcpy(f->data[0], in, frames * 2 * sizeof(float));
        ff::check(av_buffersrc_add_frame(m_src, f), "av_buffersrc_add_frame");
        for (;;) {
            const int ret = av_buffersink_get_frame(m_sink, m_out.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            ff::check(ret, "av_buffersink_get_frame");
            const auto* d = reinterpret_cast<const float*>(m_out->data[0]);
            out.insert(out.end(), d, d + static_cast<size_t>(m_out->nb_samples) * 2);
            av_frame_unref(m_out.get());
        }
    }

private:
    AVFilterGraph* m_graph = nullptr;
    AVFilterContext* m_src = nullptr;
    AVFilterContext* m_sink = nullptr;
    ff::FramePtr m_in, m_out;
    int64_t m_pts = 0;
};

MicProcessor::MicProcessor() = default;
MicProcessor::~MicProcessor() = default;

void MicProcessor::setSettings(const MicFilterSettings& s)
{
    std::lock_guard lock(m_mutex);
    m_pending = s;
    m_dirty = true;
}

void MicProcessor::configure(const MicFilterSettings& s)
{
    const bool denoiseChanged = s.noiseSuppression != m_s.noiseSuppression ||
                                s.noiseReductionDb != m_s.noiseReductionDb || s.enabled != m_s.enabled;
    m_s = s;
    setHighPass(m_hp, 80);
    setShelf(m_low, true, 150, s.eqLowDb);
    setPeak(m_mid, 2000, 0.9, s.eqMidDb);
    setShelf(m_high, false, 7000, s.eqHighDb);
    if (denoiseChanged || (!m_denoiser && s.enabled && s.noiseSuppression)) {
        m_denoiser.reset();
        if (s.enabled && s.noiseSuppression) {
            try {
                m_denoiser = std::make_unique<Denoiser>(s.noiseReductionDb);
            } catch (const std::exception& e) {
                log::warn("Noise suppression unavailable: {}", e.what());
            }
        }
        // Restart latency accounting for the new chain.
        m_inTotal = m_outTotal = 0;
    }
}

size_t MicProcessor::process(const float* in, size_t frames, std::vector<float>& out, int64_t& latencyFrames)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_dirty) {
            configure(m_pending);
            m_dirty = false;
        }
    }
    out.clear();
    latencyFrames = m_inTotal - m_outTotal;
    if (!m_s.enabled) {
        out.assign(in, in + frames * 2);
        return frames;
    }

    if (m_denoiser) {
        try {
            m_denoiser->process(in, frames, out);
        } catch (const std::exception& e) {
            log::warn("Noise suppression failed, bypassing: {}", e.what());
            m_denoiser.reset();
            out.assign(in, in + frames * 2);
        }
        m_inTotal += static_cast<int64_t>(frames);
        m_outTotal += static_cast<int64_t>(out.size() / 2);
        if (!m_denoiser)
            m_inTotal = m_outTotal;
    } else {
        out.assign(in, in + frames * 2);
    }

    const size_t n = out.size() / 2;
    const double inGain = dbToGain(m_s.gainDb);
    const double gateRel = coef(m_s.gateReleaseMs), gateAtt = coef(1.0);
    const double compAtt = coef(m_s.compAttackMs), compRel = coef(m_s.compReleaseMs);
    const double makeup = dbToGain(m_s.compMakeupDb);
    const double ceiling = dbToGain(m_s.limiterCeilingDb);
    const double limRel = coef(80);
    const double gateThr = dbToGain(m_s.gateThresholdDb);

    for (size_t i = 0; i < n; ++i) {
        float l = out[2 * i], r = out[2 * i + 1];
        if (m_s.highPass) {
            l = m_hp.run(l, 0);
            r = m_hp.run(r, 1);
        }
        if (m_s.eq) {
            l = m_high.run(m_mid.run(m_low.run(l, 0), 0), 0);
            r = m_high.run(m_mid.run(m_low.run(r, 1), 1), 1);
        }
        double gl = l * inGain, gr = r * inGain;
        const double level = std::max(std::abs(gl), std::abs(gr));

        if (m_s.gate) {
            m_gateEnv = level > m_gateEnv ? level : m_gateEnv * gateRel + level * (1 - gateRel);
            const double target = m_gateEnv >= gateThr ? 1.0 : 0.0;
            const double c = target > m_gateGain ? gateAtt : gateRel;
            m_gateGain = target + (m_gateGain - target) * c;
            gl *= m_gateGain;
            gr *= m_gateGain;
        }
        if (m_s.compressor) {
            const double lv = std::max(std::abs(gl), std::abs(gr));
            const double c = lv > m_compEnv ? compAtt : compRel;
            m_compEnv = lv + (m_compEnv - lv) * c;
            const double over = gainToDb(m_compEnv) - m_s.compThresholdDb;
            const double reduction = over > 0 ? over * (1.0 - 1.0 / std::max(1.0f, m_s.compRatio)) : 0.0;
            const double g = dbToGain(-reduction) * makeup;
            gl *= g;
            gr *= g;
        }
        if (m_s.limiter) {
            const double lv = std::max(std::abs(gl), std::abs(gr));
            const double needed = lv > ceiling ? ceiling / lv : 1.0;
            m_limGain = needed < m_limGain ? needed : needed + (m_limGain - needed) * limRel;
            gl *= m_limGain;
            gr *= m_limGain;
        }
        out[2 * i] = static_cast<float>(gl);
        out[2 * i + 1] = static_cast<float>(gr);
    }
    return n;
}

} // namespace luma::audio
