#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace luma::audio {

// Stereo float ring addressed by absolute sample position on the session
// timeline (position 0 = recording start). Writers place audio where its QPC
// timestamp says it belongs; the mixer reads a fixed distance behind real time.
// Positions nobody wrote read back as silence, so device gaps (e.g. loopback
// with nothing playing) need no special handling. Memory is fixed.
class AudioRing {
public:
    static constexpr int kChannels = 2;

    explicit AudioRing(size_t capacityFrames) : m_cap(capacityFrames), m_buf(capacityFrames * kChannels, 0.f) {}

    // Writes frames at `pos`. Frames already consumed by the reader, or too far
    // ahead to fit, are dropped (returned count = frames actually stored).
    size_t write(int64_t pos, const float* data, size_t frames)
    {
        std::lock_guard lock(m_mutex);
        size_t skip = 0;
        if (pos < m_readPos)
            skip = static_cast<size_t>(std::min<int64_t>(m_readPos - pos, static_cast<int64_t>(frames)));
        int64_t start = pos + static_cast<int64_t>(skip);
        size_t n = frames - skip;
        const int64_t limit = m_readPos + static_cast<int64_t>(m_cap);
        if (start + static_cast<int64_t>(n) > limit)
            n = start >= limit ? 0 : static_cast<size_t>(limit - start);
        const float* src = data + skip * kChannels;
        while (n > 0) {
            const size_t at = static_cast<size_t>(start % static_cast<int64_t>(m_cap));
            const size_t chunk = std::min(n, m_cap - at);
            std::memcpy(&m_buf[at * kChannels], src, chunk * kChannels * sizeof(float));
            src += chunk * kChannels;
            start += static_cast<int64_t>(chunk);
            n -= chunk;
        }
        return static_cast<size_t>(start - pos) - skip;
    }

    // Reads (and clears) frames at `pos`; advances the read position.
    void read(int64_t pos, float* out, size_t frames)
    {
        std::lock_guard lock(m_mutex);
        int64_t p = pos;
        size_t n = frames;
        while (n > 0) {
            const size_t at = static_cast<size_t>(((p % static_cast<int64_t>(m_cap)) + static_cast<int64_t>(m_cap)) %
                                                  static_cast<int64_t>(m_cap));
            const size_t chunk = std::min(n, m_cap - at);
            float* src = &m_buf[at * kChannels];
            std::memcpy(out, src, chunk * kChannels * sizeof(float));
            std::memset(src, 0, chunk * kChannels * sizeof(float));
            out += chunk * kChannels;
            p += static_cast<int64_t>(chunk);
            n -= chunk;
        }
        m_readPos = pos + static_cast<int64_t>(frames);
    }

private:
    size_t m_cap;
    std::vector<float> m_buf;
    std::mutex m_mutex;
    int64_t m_readPos = 0;
};

} // namespace luma::audio
