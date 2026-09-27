#pragma once

#include "encode/FfmpegUtil.h"

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace luma::mux {

// Thread-safe multi-stream muxer (one video + N audio tracks). Packets from
// the encoder threads are interleaved by FFmpeg. Matroska is the recording
// container: an interrupted file stays playable.
class Muxer {
public:
    Muxer(const std::filesystem::path& path, const char* formatName = "matroska");
    ~Muxer();
    Muxer(const Muxer&) = delete;
    Muxer& operator=(const Muxer&) = delete;

    // Before writeHeader(): adds a stream matching an opened encoder. Returns its index.
    int addStream(const AVCodecContext* encoder, const std::string& title = {});
    void writeHeader();

    // Thread-safe. `pkt` timestamps are in `encoderTimeBase`. Throws ff::Error on write failure.
    void write(int stream, AVPacket* pkt, AVRational encoderTimeBase);
    // Writes the trailer and closes the file (idempotent).
    void finish();

    uint64_t bytesWritten() const;

private:
    mutable std::mutex m_mutex;
    AVFormatContext* m_fmt = nullptr;
    bool m_headerWritten = false;
    bool m_finished = false;
    uint64_t m_bytes = 0;
    int64_t m_lastFlushMs = 0;
};

// Stream-copies every stream of `input` into `output` (container chosen from
// the extension). Used for MKV -> MP4 export and to repair an MKV whose
// recording was interrupted (rebuilds the index and duration).
// `progress` receives 0..1 and may return false to cancel. Throws on failure.
void remux(const std::filesystem::path& input, const std::filesystem::path& output,
           const std::function<bool(double)>& progress = {});

// Duration in seconds and first video stream size/rate of a media file (for history).
struct MediaInfo {
    double durationSeconds = 0;
    int width = 0, height = 0;
    double fps = 0;
};
bool probeMedia(const std::filesystem::path& file, MediaInfo& info);

} // namespace luma::mux
