#include "session/EncodeLoop.h"

#include "util/QpcClock.h"

namespace luma::session {

EncodeLoop::EncodeLoop(encode::FramePool& pool, BoundedQueue<FrameTicket>& queue, encode::VideoEncoder& encoder,
                       mux::Muxer& muxer, int stream, SessionCounters& counters, std::vector<float>& encodeLatencyMs)
    : m_pool(pool), m_queue(queue), m_encoder(encoder), m_muxer(muxer), m_stream(stream), m_counters(counters),
      m_encodeLatencyMs(encodeLatencyMs)
{
    m_trackPts.fill(-1);
}

void EncodeLoop::run()
{
    const auto sink = [this](AVPacket* pkt) { onPacket(pkt); };

    FrameTicket t;
    while (m_queue.pop(t)) {
        const size_t idx = static_cast<size_t>(t.pts) % kTrack;
        m_trackPts[idx] = t.pts;
        m_trackQpc[idx] = t.enqueueQpc;

        AVFrame* frame = m_pool.frame(t.slot);
        frame->pts = t.pts;
        m_muxMicrosInCall = 0;
        const int64_t t0 = qpc::now();
        try {
            m_encoder.encode(frame, sink); // x264 copies the picture; the buffer is free afterwards
        } catch (...) {
            m_pool.release(t.slot);
            throw;
        }
        const int64_t t1 = qpc::now();
        m_pool.release(t.slot);
        m_counters.encoderBusyMicros += static_cast<int64_t>(qpc::toMs(t1 - t0) * 1000.0) - m_muxMicrosInCall;
    }

    const int64_t f0 = qpc::now();
    m_encoder.flush(sink);
    m_flushMs = qpc::toMs(qpc::now() - f0);
}

void EncodeLoop::onPacket(AVPacket* pkt)
{
    const int64_t now = qpc::now();
    const size_t idx = static_cast<size_t>(pkt->pts) % kTrack;
    if (m_trackPts[idx] == pkt->pts && m_encodeLatencyMs.size() < m_encodeLatencyMs.capacity())
        m_encodeLatencyMs.push_back(static_cast<float>(qpc::toMs(now - m_trackQpc[idx])));

    ++m_counters.encoded;
    m_counters.encodedBytes += pkt->size;
    if (pkt->flags & AV_PKT_FLAG_KEY)
        ++m_counters.keyframes;

    const int64_t w0 = qpc::now();
    m_muxer.write(m_stream, pkt, m_encoder.context()->time_base);
    const auto micros = static_cast<int64_t>(qpc::toMs(qpc::now() - w0) * 1000.0);
    m_muxMicrosInCall += micros;
    m_counters.muxWriteMicros += micros;
    int64_t cur = m_counters.muxWriteMaxMicros.load(std::memory_order_relaxed);
    while (micros > cur && !m_counters.muxWriteMaxMicros.compare_exchange_weak(cur, micros)) {
    }
}

} // namespace luma::session
