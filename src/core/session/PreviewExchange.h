#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace luma::session {

// Latest live-preview picture (BGRA, top-down) from the capture thread to the UI.
// Only the newest picture is kept: a slow consumer never builds up a backlog and the
// producer only holds the lock for one memcpy of a small image.
class PreviewExchange {
public:
    // UI: the size it wants (0 x 0 = no preview needed right now, e.g. window minimised).
    void requestSize(unsigned width, unsigned height)
    {
        m_reqW.store(width, std::memory_order_relaxed);
        m_reqH.store(height, std::memory_order_relaxed);
    }
    void requestedSize(unsigned& width, unsigned& height) const
    {
        width = m_reqW.load(std::memory_order_relaxed);
        height = m_reqH.load(std::memory_order_relaxed);
    }

    // Producer (capture thread).
    void publish(const uint8_t* bgra, unsigned pitch, unsigned width, unsigned height)
    {
        std::lock_guard lock(m_mutex);
        m_pixels.resize(static_cast<size_t>(width) * height);
        for (unsigned y = 0; y < height; ++y)
            std::memcpy(m_pixels.data() + static_cast<size_t>(y) * width, bgra + static_cast<size_t>(y) * pitch,
                        static_cast<size_t>(width) * 4);
        m_width = width;
        m_height = height;
        ++m_sequence;
    }

    // Consumer: copies the latest picture if it is newer than `sequence` (updated on success).
    bool take(uint64_t& sequence, std::vector<uint32_t>& pixels, unsigned& width, unsigned& height) const
    {
        std::lock_guard lock(m_mutex);
        if (m_sequence == sequence || m_pixels.empty())
            return false;
        pixels = m_pixels;
        width = m_width;
        height = m_height;
        sequence = m_sequence;
        return true;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<uint32_t> m_pixels;
    unsigned m_width = 0, m_height = 0;
    uint64_t m_sequence = 0;
    std::atomic<unsigned> m_reqW{0}, m_reqH{0};
};

} // namespace luma::session
