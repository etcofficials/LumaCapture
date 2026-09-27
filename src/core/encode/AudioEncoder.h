#pragma once

#include "encode/FfmpegUtil.h"

#include <functional>
#include <string>
#include <vector>

namespace luma::encode {

// AAC-LC (FFmpeg native encoder), 48 kHz stereo. Input: interleaved float in
// any block size; output packets carry pts in 1/48000 units counted from the
// first sample pushed (the stream is continuous; pauses are cut upstream).
class AudioEncoder {
public:
    using PacketSink = std::function<void(AVPacket*)>;

    AudioEncoder(int bitrateKbps, std::string title);

    void push(const float* interleaved, size_t frames, const PacketSink& sink);
    void flush(const PacketSink& sink);

    const AVCodecContext* context() const { return m_ctx.get(); }
    const std::string& title() const { return m_title; }

private:
    void encodeFrame(size_t frames, const PacketSink& sink);
    void drain(const PacketSink& sink);

    ff::CodecContextPtr m_ctx;
    ff::FramePtr m_frame;
    ff::PacketPtr m_pkt;
    std::vector<float> m_pending; // interleaved samples not yet encoded
    int64_t m_nextPts = 0;
    std::string m_title;
};

} // namespace luma::encode
