#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace luma::webcam {

struct ImageBuffer {
    unsigned width = 0;
    unsigned height = 0;
    std::vector<uint32_t> pixels; // BGRA, straight alpha, top-down, tightly packed
    uint64_t sequence = 0;
};

// Latest-frame exchange between one writer (webcam thread) and a few readers
// (capture thread, UI preview). Four preallocated buffers are recycled, so
// nothing is allocated per frame once sizes are stable, and readers never block
// the writer (if every spare buffer is being read, that frame is skipped).
class FrameExchange {
public:
    ImageBuffer* beginWrite(unsigned width, unsigned height)
    {
        std::lock_guard lock(m_mutex);
        for (int i = 0; i < kBuffers; ++i) {
            if (i != m_published && m_readers[i] == 0) {
                m_writing = i;
                ImageBuffer& b = m_buffers[i];
                if (b.width != width || b.height != height) {
                    b.width = width;
                    b.height = height;
                    b.pixels.resize(static_cast<size_t>(width) * height);
                }
                return &b;
            }
        }
        return nullptr;
    }

    void endWrite(ImageBuffer* buffer)
    {
        std::lock_guard lock(m_mutex);
        if (!buffer || m_writing < 0)
            return;
        buffer->sequence = ++m_seq;
        m_published = m_writing;
        m_writing = -1;
    }

    // Calls fn(const ImageBuffer&) with the newest frame. Returns false if none.
    template <typename F>
    bool read(F&& fn)
    {
        int idx;
        {
            std::lock_guard lock(m_mutex);
            if (m_published < 0)
                return false;
            idx = m_published;
            ++m_readers[idx];
        }
        fn(static_cast<const ImageBuffer&>(m_buffers[idx]));
        std::lock_guard lock(m_mutex);
        --m_readers[idx];
        return true;
    }

    uint64_t sequence() const { return m_seq.load(); }

    bool hasFrame()
    {
        std::lock_guard lock(m_mutex);
        return m_published >= 0;
    }

    void clear()
    {
        std::lock_guard lock(m_mutex);
        m_published = -1;
    }

private:
    static constexpr int kBuffers = 4;
    std::mutex m_mutex;
    std::array<ImageBuffer, kBuffers> m_buffers;
    std::array<int, kBuffers> m_readers{};
    int m_published = -1;
    int m_writing = -1;
    std::atomic<uint64_t> m_seq{0};
};

} // namespace luma::webcam
