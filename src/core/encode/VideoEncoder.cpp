#include "encode/VideoEncoder.h"

extern "C" {
#include <libavutil/opt.h>
}

#include <cmath>
#include <format>

namespace luma::encode {

VideoEncoder::VideoEncoder(const VideoEncoderConfig& config) : m_config(config)
{
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec)
        throw std::runtime_error("libx264 encoder not present in this FFmpeg build");

    m_ctx.reset(avcodec_alloc_context3(codec));
    m_pkt.reset(av_packet_alloc());
    if (!m_ctx || !m_pkt)
        throw std::bad_alloc();

    AVCodecContext* c = m_ctx.get();
    c->width = config.width;
    c->height = config.height;
    c->pix_fmt = AV_PIX_FMT_NV12;
    c->time_base = AVRational{1, config.fps};
    c->framerate = AVRational{config.fps, 1};
    c->gop_size = std::max(1, static_cast<int>(std::lround(config.keyframeSeconds * config.fps)));
    c->thread_count = config.threads;
    c->color_range = AVCOL_RANGE_MPEG;
    c->colorspace = AVCOL_SPC_BT709;
    c->color_primaries = AVCOL_PRI_BT709;
    c->color_trc = AVCOL_TRC_BT709;
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; // Matroska stores SPS/PPS in CodecPrivate

    ff::check(av_opt_set(c->priv_data, "preset", config.preset.c_str(), 0), "set x264 preset");
    ff::check(av_opt_set_double(c->priv_data, "crf", config.crf, 0), "set x264 crf");
    if (!config.x264Params.empty())
        ff::check(av_opt_set(c->priv_data, "x264-params", config.x264Params.c_str(), 0), "set x264-params");

    ff::check(avcodec_open2(c, codec, nullptr), "avcodec_open2(libx264)");
}

std::string VideoEncoder::description() const
{
    return std::format("libx264 preset={} crf={} keyint={} ({:.1f}s) threads={}{}{}", m_config.preset, m_config.crf,
                       m_ctx->gop_size, m_config.keyframeSeconds, m_config.threads,
                       m_config.x264Params.empty() ? "" : " x264-params=", m_config.x264Params);
}

void VideoEncoder::encode(AVFrame* frame, const PacketSink& sink)
{
    ff::check(avcodec_send_frame(m_ctx.get(), frame), "avcodec_send_frame");
    drain(sink);
}

void VideoEncoder::flush(const PacketSink& sink)
{
    const int ret = avcodec_send_frame(m_ctx.get(), nullptr);
    if (ret != AVERROR_EOF)
        ff::check(ret, "avcodec_send_frame(flush)");
    drain(sink);
}

void VideoEncoder::drain(const PacketSink& sink)
{
    for (;;) {
        const int ret = avcodec_receive_packet(m_ctx.get(), m_pkt.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        ff::check(ret, "avcodec_receive_packet");
        sink(m_pkt.get());
        av_packet_unref(m_pkt.get());
    }
}

} // namespace luma::encode
