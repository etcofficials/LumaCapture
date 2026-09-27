#pragma once

#include "encode/FramePool.h"
#include "encode/VideoEncoder.h"
#include "mux/Muxer.h"
#include "session/BoundedQueue.h"
#include "session/SessionTypes.h"

#include <array>

namespace luma::session {

// The encoder thread body: bounded queue -> libx264 -> muxer (video stream).
// Runs until the queue is closed and empty, then flushes the encoder.
class EncodeLoop {
public:
    EncodeLoop(encode::FramePool& pool, BoundedQueue<FrameTicket>& queue, encode::VideoEncoder& encoder,
               mux::Muxer& muxer, int stream, SessionCounters& counters, std::vector<float>& encodeLatencyMs);

    void run();
    double flushMs() const { return m_flushMs; }

private:
    void onPacket(AVPacket* pkt);

    encode::FramePool& m_pool;
    BoundedQueue<FrameTicket>& m_queue;
    encode::VideoEncoder& m_encoder;
    mux::Muxer& m_muxer;
    int m_stream;
    SessionCounters& m_counters;
    std::vector<float>& m_encodeLatencyMs;

    // Enqueue time per pts, to match packets (which leave x264 frames later) to their input.
    static constexpr size_t kTrack = 1024;
    std::array<int64_t, kTrack> m_trackPts{};
    std::array<int64_t, kTrack> m_trackQpc{};
    int64_t m_muxMicrosInCall = 0;
    double m_flushMs = 0;
};

} // namespace luma::session
