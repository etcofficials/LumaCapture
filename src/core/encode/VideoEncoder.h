#pragma once

#include "encode/FfmpegUtil.h"

#include <functional>
#include <string>

namespace luma::encode {

struct VideoEncoderConfig {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    std::string preset = "superfast";
    int crf = 23;
    double keyframeSeconds = 2.0;
    int threads = 3;
    std::string x264Params; // optional "key=value:key=value" passthrough
};

// libx264 through libavcodec. Input: NV12 frames with pts in 1/fps units
// (gaps in pts are allowed and represent dropped frames).
class VideoEncoder {
public:
    using PacketSink = std::function<void(AVPacket*)>;

    explicit VideoEncoder(const VideoEncoderConfig& config);

    // Encodes one frame and hands every packet produced to `sink`.
    void encode(AVFrame* frame, const PacketSink& sink);
    // Drains all delayed frames (lookahead / frame threads).
    void flush(const PacketSink& sink);

    const AVCodecContext* context() const { return m_ctx.get(); }
    std::string description() const;

private:
    void drain(const PacketSink& sink);

    ff::CodecContextPtr m_ctx;
    ff::PacketPtr m_pkt;
    VideoEncoderConfig m_config;
};

} // namespace luma::encode
