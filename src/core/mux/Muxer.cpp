#include "mux/Muxer.h"

#include "util/Log.h"

#include <windows.h>

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <thread>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
}

namespace luma::mux {
namespace {

std::string utf8Path(const std::filesystem::path& p)
{
    const std::u8string u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

} // namespace

// Writes the muxer's byte stream on its own thread. Every chunk carries its file
// offset, so the muxer's seeks (Matroska rewrites header fields and cues at the end)
// need no synchronisation with the writer. Bounded: above kMaxQueuedBytes the
// producer waits (the encoder then slows down exactly as with direct writes).
class AsyncFileSink {
public:
    explicit AsyncFileSink(const std::filesystem::path& path)
    {
        m_file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_file == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot create output file " + utf8Path(path) + " (Windows error " +
                                     std::to_string(GetLastError()) + ")");
        m_thread = std::thread([this] { run(); });
    }
    ~AsyncFileSink()
    {
        try {
            close();
        } catch (const std::exception& e) {
            log::error("Recording file: {}", e.what());
        }
    }
    AsyncFileSink(const AsyncFileSink&) = delete;
    AsyncFileSink& operator=(const AsyncFileSink&) = delete;

    static int writeCallback(void* opaque, const uint8_t* buf, int size)
    {
        return static_cast<AsyncFileSink*>(opaque)->write(buf, size);
    }
    static int64_t seekCallback(void* opaque, int64_t offset, int whence)
    {
        return static_cast<AsyncFileSink*>(opaque)->seek(offset, whence);
    }

    // Waits until everything is written, then closes the file. Throws if a write failed.
    void close()
    {
        {
            std::lock_guard lock(m_mutex);
            if (m_closed)
                return;
            m_closed = true;
            m_stop = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
        if (m_file != INVALID_HANDLE_VALUE) {
            CloseHandle(m_file);
            m_file = INVALID_HANDLE_VALUE;
        }
        if (m_error != 0)
            throw std::runtime_error("Writing the recording file failed (Windows error " + std::to_string(m_error) + ")");
    }

private:
    struct Chunk {
        int64_t offset = 0;
        std::vector<uint8_t> data;
    };
    static constexpr size_t kMaxQueuedBytes = size_t{64} << 20; // ~10+ s of a 1080p recording
    static constexpr size_t kChunkBytes = size_t{1} << 20;

    int write(const uint8_t* data, int size)
    {
        if (size <= 0)
            return 0;
        std::unique_lock lock(m_mutex);
        if (m_queuedBytes > kMaxQueuedBytes) {
            if (!m_warnedFull) {
                m_warnedFull = true;
                log::warn("Recording file: the disk is not keeping up ({} MB waiting to be written)", m_queuedBytes >> 20);
            }
            m_space.wait(lock, [&] { return m_queuedBytes <= kMaxQueuedBytes || m_error != 0; });
        }
        if (m_error != 0)
            return AVERROR(EIO);
        // AVIO hands over its buffer in pieces; append contiguous data to the last chunk.
        bool appended = false;
        if (!m_queue.empty()) {
            Chunk& last = m_queue.back();
            if (last.offset + static_cast<int64_t>(last.data.size()) == m_pos && last.data.size() < kChunkBytes) {
                last.data.insert(last.data.end(), data, data + size);
                appended = true;
            }
        }
        if (!appended)
            m_queue.push_back(Chunk{m_pos, std::vector<uint8_t>(data, data + size)});
        m_queuedBytes += static_cast<size_t>(size);
        m_pos += size;
        m_size = std::max(m_size, m_pos);
        lock.unlock();
        m_cv.notify_one();
        return size;
    }

    int64_t seek(int64_t offset, int whence)
    {
        std::lock_guard lock(m_mutex);
        switch (whence & ~AVSEEK_FORCE) {
        case AVSEEK_SIZE: return m_size;
        case SEEK_SET: m_pos = offset; break;
        case SEEK_CUR: m_pos += offset; break;
        case SEEK_END: m_pos = m_size + offset; break;
        default: return AVERROR(EINVAL);
        }
        return m_pos;
    }

    DWORD writeAt(int64_t offset, const uint8_t* data, size_t size)
    {
        LARGE_INTEGER li{};
        li.QuadPart = offset;
        if (!SetFilePointerEx(m_file, li, nullptr, FILE_BEGIN))
            return GetLastError();
        while (size > 0) {
            const DWORD n = static_cast<DWORD>(std::min<size_t>(size, kChunkBytes));
            DWORD written = 0;
            if (!WriteFile(m_file, data, n, &written, nullptr))
                return GetLastError();
            if (written == 0)
                return ERROR_WRITE_FAULT;
            data += written;
            size -= written;
        }
        return 0;
    }

    void run()
    {
        SetThreadDescription(GetCurrentThread(), L"luma-file-writer");
        std::unique_lock lock(m_mutex);
        for (;;) {
            m_cv.wait(lock, [&] { return m_stop || !m_queue.empty(); });
            if (m_queue.empty()) {
                if (m_stop)
                    return; // everything written
                continue;
            }
            Chunk chunk = std::move(m_queue.front());
            m_queue.pop_front();
            const bool failed = m_error != 0;
            lock.unlock();
            const DWORD err = failed ? 0 : writeAt(chunk.offset, chunk.data.data(), chunk.data.size());
            lock.lock();
            m_queuedBytes -= chunk.data.size();
            if (err != 0 && m_error == 0) {
                m_error = err;
                log::error("Recording file: write failed (Windows error {})", err);
            }
            m_space.notify_all();
        }
    }

    HANDLE m_file = INVALID_HANDLE_VALUE;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;    // writer: work available / stop
    std::condition_variable m_space; // producer: queue below the limit
    std::deque<Chunk> m_queue;
    size_t m_queuedBytes = 0;
    int64_t m_pos = 0;  // the muxer's current position
    int64_t m_size = 0; // logical file size (highest byte handed over)
    DWORD m_error = 0;
    bool m_stop = false;
    bool m_closed = false;
    bool m_warnedFull = false;
};

Muxer::Muxer(const std::filesystem::path& path, const char* formatName)
{
    const std::string file = utf8Path(path);
    ff::check(avformat_alloc_output_context2(&m_fmt, nullptr, formatName, file.c_str()), "avformat_alloc_output_context2");
    try {
        m_sink = std::make_unique<AsyncFileSink>(path);
    } catch (...) {
        avformat_free_context(m_fmt);
        m_fmt = nullptr;
        throw;
    }
    constexpr int kIoBuffer = 256 * 1024;
    auto* buffer = static_cast<unsigned char*>(av_malloc(kIoBuffer));
    AVIOContext* pb = buffer ? avio_alloc_context(buffer, kIoBuffer, 1, m_sink.get(), nullptr,
                                                  &AsyncFileSink::writeCallback, &AsyncFileSink::seekCallback)
                             : nullptr;
    if (!pb) {
        av_free(buffer);
        m_sink.reset();
        avformat_free_context(m_fmt);
        m_fmt = nullptr;
        throw std::bad_alloc();
    }
    m_fmt->pb = pb;
    m_fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
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
    try {
        closeIo();
    } catch (const std::exception& e) {
        log::error("Muxer: closing file failed: {}", e.what());
    }
    avformat_free_context(m_fmt);
}

void Muxer::closeIo()
{
    if (m_fmt && m_fmt->pb) {
        avio_flush(m_fmt->pb);
        av_freep(&m_fmt->pb->buffer);
        avio_context_free(&m_fmt->pb);
    }
    if (m_sink) {
        const auto sink = std::move(m_sink);
        sink->close(); // drains the queue; throws on a write error
    }
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
    closeIo();
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
