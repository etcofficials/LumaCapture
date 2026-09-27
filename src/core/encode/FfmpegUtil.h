#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <memory>
#include <stdexcept>
#include <string>

namespace luma::ff {

inline std::string errorString(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

class Error : public std::runtime_error {
public:
    Error(const std::string& what, int code) : std::runtime_error(what + ": " + errorString(code)), m_code(code) {}
    int code() const noexcept { return m_code; }

private:
    int m_code;
};

inline int check(int ret, const char* what)
{
    if (ret < 0)
        throw Error(what, ret);
    return ret;
}

struct CodecContextDeleter { void operator()(AVCodecContext* c) const { avcodec_free_context(&c); } };
struct FrameDeleter { void operator()(AVFrame* f) const { av_frame_free(&f); } };
struct PacketDeleter { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct SwsDeleter { void operator()(SwsContext* s) const { sws_freeContext(s); } };

using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using SwsPtr = std::unique_ptr<SwsContext, SwsDeleter>;

} // namespace luma::ff
