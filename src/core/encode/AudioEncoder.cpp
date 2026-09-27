#include "encode/AudioEncoder.h"

extern "C" {
#include <libavutil/channel_layout.h>
}

namespace luma::encode {

AudioEncoder::AudioEncoder(int bitrateKbps, std::string title) : m_title(std::move(title))
{
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec)
        throw std::runtime_error("AAC encoder not present in this FFmpeg build");
    m_ctx.reset(avcodec_alloc_context3(codec));
    m_frame.reset(av_frame_alloc());
    m_pkt.reset(av_packet_alloc());
    if (!m_ctx || !m_frame || !m_pkt)
        throw std::bad_alloc();

    AVCodecContext* c = m_ctx.get();
    c->sample_fmt = AV_SAMPLE_FMT_FLTP;
    c->sample_rate = 48000;
    av_channel_layout_default(&c->ch_layout, 2);
    c->bit_rate = static_cast<int64_t>(bitrateKbps) * 1000;
    c->time_base = AVRational{1, 48000};
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    ff::check(avcodec_open2(c, codec, nullptr), "avcodec_open2(aac)");

    m_frame->format = AV_SAMPLE_FMT_FLTP;
    m_frame->sample_rate = 48000;
    m_frame->nb_samples = c->frame_size;
    av_channel_layout_copy(&m_frame->ch_layout, &c->ch_layout);
    ff::check(av_frame_get_buffer(m_frame.get(), 0), "av_frame_get_buffer(aac)");
    m_pending.reserve(static_cast<size_t>(c->frame_size) * 2 * 8);
}

void AudioEncoder::push(const float* interleaved, size_t frames, const PacketSink& sink)
{
    m_pending.insert(m_pending.end(), interleaved, interleaved + frames * 2);
    const auto fs = static_cast<size_t>(m_ctx->frame_size);
    size_t offset = 0;
    while (m_pending.size() - offset >= fs * 2) {
        ff::check(av_frame_make_writable(m_frame.get()), "av_frame_make_writable");
        auto* l = reinterpret_cast<float*>(m_frame->data[0]);
        auto* r = reinterpret_cast<float*>(m_frame->data[1]);
        const float* src = m_pending.data() + offset;
        for (size_t i = 0; i < fs; ++i) {
            l[i] = src[2 * i];
            r[i] = src[2 * i + 1];
        }
        offset += fs * 2;
        encodeFrame(fs, sink);
    }
    m_pending.erase(m_pending.begin(), m_pending.begin() + static_cast<ptrdiff_t>(offset));
}

void AudioEncoder::encodeFrame(size_t frames, const PacketSink& sink)
{
    m_frame->nb_samples = static_cast<int>(frames);
    m_frame->pts = m_nextPts;
    m_nextPts += static_cast<int64_t>(frames);
    ff::check(avcodec_send_frame(m_ctx.get(), m_frame.get()), "avcodec_send_frame(aac)");
    drain(sink);
}

void AudioEncoder::flush(const PacketSink& sink)
{
    const size_t rest = m_pending.size() / 2;
    if (rest > 0) {
        // Last (short) frame: the AAC encoder accepts a smaller final frame.
        ff::check(av_frame_make_writable(m_frame.get()), "av_frame_make_writable");
        auto* l = reinterpret_cast<float*>(m_frame->data[0]);
        auto* r = reinterpret_cast<float*>(m_frame->data[1]);
        for (size_t i = 0; i < rest; ++i) {
            l[i] = m_pending[2 * i];
            r[i] = m_pending[2 * i + 1];
        }
        m_pending.clear();
        encodeFrame(rest, sink);
    }
    const int ret = avcodec_send_frame(m_ctx.get(), nullptr);
    if (ret != AVERROR_EOF)
        ff::check(ret, "avcodec_send_frame(aac flush)");
    drain(sink);
}

void AudioEncoder::drain(const PacketSink& sink)
{
    for (;;) {
        const int ret = avcodec_receive_packet(m_ctx.get(), m_pkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        ff::check(ret, "avcodec_receive_packet(aac)");
        sink(m_pkt.get());
        av_packet_unref(m_pkt.get());
    }
}

} // namespace luma::encode
