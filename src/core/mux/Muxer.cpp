#include "mux/Muxer.h"

#include "util/Log.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/mathematics.h>
}

namespace luma::mux {
namespace {

std::string utf8Path(const std::filesystem::path& p)
{
    const std::u8string u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

} // namespace

Muxer::Muxer(const std::filesystem::path& path, const char* formatName)
{
    const std::string file = utf8Path(path);
    ff::check(avformat_alloc_output_context2(&m_fmt, nullptr, formatName, file.c_str()), "avformat_alloc_output_context2");
    const int ret = avio_open(&m_fmt->pb, file.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        avformat_free_context(m_fmt);
        m_fmt = nullptr;
        throw ff::Error("Cannot create output file " + file, ret);
    }
}

Muxer::~Muxer()
{
    if (!m_fmt)
        return;
    if (!m_finished) {
        try {
            finish();
        } catch (const std::exception& e) {
            log::error("Muxer: finishing file failed: {}", e.what());
        }
    }
    if (m_fmt->pb)
        avio_closep(&m_fmt->pb);
    avformat_free_context(m_fmt);
}

int Muxer::addStream(const AVCodecContext* enc, const std::string& title)
{
    std::lock_guard lock(m_mutex);
    AVStream* st = avformat_new_stream(m_fmt, nullptr);
    if (!st)
        throw std::bad_alloc();
    ff::check(avcodec_parameters_from_context(st->codecpar, enc), "avcodec_parameters_from_context");
    st->time_base = enc->time_base;
    if (enc->codec_type == AVMEDIA_TYPE_VIDEO)
        st->avg_frame_rate = enc->framerate;
    if (!title.empty())
        av_dict_set(&st->metadata, "title", title.c_str(), 0);
    return st->index;
}

void Muxer::writeHeader()
{
    std::lock_guard lock(m_mutex);
    av_dict_set(&m_fmt->metadata, "encoder", "LumaCapture", 0);
    // Crash safety: Matroska keeps a whole cluster (up to 5 s) in memory by default and
    // AVIO keeps a 32 KB buffer, so a crash could lose everything written so far. Close
    // clusters at least every second and flush the file (see write()) once per second.
    AVDictionary* opts = nullptr;
    if (std::strcmp(m_fmt->oformat->name, "matroska") == 0) {
        av_dict_set(&opts, "cluster_time_limit", "1000", 0);
        av_dict_set(&opts, "cluster_size_limit", "2097152", 0);
    }
    const int ret = avformat_write_header(m_fmt, &opts);
    av_dict_free(&opts);
    ff::check(ret, "avformat_write_header");
    avio_flush(m_fmt->pb); // the header is on disk before the first frame
    m_lastFlushMs = static_cast<int64_t>(GetTickCount64());
    m_headerWritten = true;
}

void Muxer::write(int stream, AVPacket* pkt, AVRational tb)
{
    std::lock_guard lock(m_mutex);
    if (m_finished)
        return;
    pkt->stream_index = stream;
    m_bytes += static_cast<uint64_t>(pkt->size);
    av_packet_rescale_ts(pkt, tb, m_fmt->streams[stream]->time_base);
    ff::check(av_interleaved_write_frame(m_fmt, pkt), "Writing to the recording file");
    const auto now = static_cast<int64_t>(GetTickCount64());
    if (now - m_lastFlushMs >= 1000) {
        avio_flush(m_fmt->pb); // bounded data loss (~1 s) on a crash or power cut
        m_lastFlushMs = now;
        if (m_fmt->pb->error < 0)
            ff::check(m_fmt->pb->error, "Writing to the recording file");
    }
}

void Muxer::finish()
{
    std::lock_guard lock(m_mutex);
    if (m_finished)
        return;
    m_finished = true;
    if (m_headerWritten)
        ff::check(av_write_trailer(m_fmt), "av_write_trailer");
    ff::check(avio_closep(&m_fmt->pb), "Closing the recording file");
}

uint64_t Muxer::bytesWritten() const
{
    std::lock_guard lock(m_mutex);
    return m_bytes;
}

void remux(const std::filesystem::path& input, const std::filesystem::path& output,
           const std::function<bool(double)>& progress)
{
    const std::string in = utf8Path(input), out = utf8Path(output);
    AVFormatContext* ictx = nullptr;
    ff::check(avformat_open_input(&ictx, in.c_str(), nullptr, nullptr), "Opening recording");
    struct InGuard {
        AVFormatContext*& c;
        ~InGuard() { avformat_close_input(&c); }
    } inGuard{ictx};
    ff::check(avformat_find_stream_info(ictx, nullptr), "Reading stream info");

    AVFormatContext* octx = nullptr;
    ff::check(avformat_alloc_output_context2(&octx, nullptr, nullptr, out.c_str()), "Creating output");
    struct OutGuard {
        AVFormatContext* c;
        ~OutGuard()
        {
            if (c->pb)
                avio_closep(&c->pb);
            avformat_free_context(c);
        }
    } outGuard{octx};

    std::vector<int> map(ictx->nb_streams, -1);
    for (unsigned i = 0; i < ictx->nb_streams; ++i) {
        const AVCodecParameters* par = ictx->streams[i]->codecpar;
        if (par->codec_type != AVMEDIA_TYPE_VIDEO && par->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        AVStream* os = avformat_new_stream(octx, nullptr);
        ff::check(avcodec_parameters_copy(os->codecpar, par), "avcodec_parameters_copy");
        os->codecpar->codec_tag = 0;
        os->time_base = ictx->streams[i]->time_base;
        av_dict_copy(&os->metadata, ictx->streams[i]->metadata, 0);
        map[i] = os->index;
    }
    ff::check(avio_open(&octx->pb, out.c_str(), AVIO_FLAG_WRITE), "Creating output file");
    ff::check(avformat_write_header(octx, nullptr), "Writing output header");

    const double total = ictx->duration > 0 ? static_cast<double>(ictx->duration) / AV_TIME_BASE : 0;
    ff::PacketPtr pkt(av_packet_alloc());
    int lastPercent = -1;
    for (;;) {
        const int ret = av_read_frame(ictx, pkt.get());
        if (ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            // A truncated (interrupted) recording ends with a read error: keep what was read.
            log::warn("Remux: input ended with '{}' - keeping the data read so far", ff::errorString(ret));
            break;
        }
        const int si = pkt->stream_index;
        if (si < 0 || si >= static_cast<int>(map.size()) || map[si] < 0) {
            av_packet_unref(pkt.get());
            continue;
        }
        if (progress && total > 0 && pkt->pts != AV_NOPTS_VALUE) {
            const double t = static_cast<double>(pkt->pts) * av_q2d(ictx->streams[si]->time_base);
            const int percent = static_cast<int>(t / total * 100);
            if (percent != lastPercent) {
                lastPercent = percent;
                if (!progress(std::clamp(t / total, 0.0, 1.0)))
                    throw std::runtime_error("Cancelled");
            }
        }
        av_packet_rescale_ts(pkt.get(), ictx->streams[si]->time_base, octx->streams[map[si]]->time_base);
        pkt->stream_index = map[si];
        pkt->pos = -1;
        ff::check(av_interleaved_write_frame(octx, pkt.get()), "Writing output");
    }
    ff::check(av_write_trailer(octx), "Finishing output");
    if (progress)
        progress(1.0);
}

bool probeMedia(const std::filesystem::path& file, MediaInfo& info)
{
    const std::string in = utf8Path(file);
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, in.c_str(), nullptr, nullptr) < 0)
        return false;
    bool ok = avformat_find_stream_info(ctx, nullptr) >= 0;
    if (ok) {
        info.durationSeconds = ctx->duration > 0 ? static_cast<double>(ctx->duration) / AV_TIME_BASE : 0;
        for (unsigned i = 0; i < ctx->nb_streams; ++i) {
            const AVStream* s = ctx->streams[i];
            if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                info.width = s->codecpar->width;
                info.height = s->codecpar->height;
                info.fps = s->avg_frame_rate.den ? av_q2d(s->avg_frame_rate) : 0;
                break;
            }
        }
    }
    avformat_close_input(&ctx);
    return ok;
}

} // namespace luma::mux
